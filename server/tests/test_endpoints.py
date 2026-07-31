import httpx
import respx
from fastapi.testclient import TestClient

import app.main as main_module
from app.config import get_settings
from app.main import app
from app.store import PendingStore
from app.tokens import issue_token

API = "https://discord.com/api/v10"


def _override(settings):
    app.dependency_overrides[get_settings] = lambda: settings


def _reset():
    app.dependency_overrides.clear()
    main_module.store = PendingStore()  # 테스트 간 상태 격리


def test_health(settings):
    _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/health").json() == {"status": "ok"}
    finally:
        _reset()


def test_auth_start_generates_state_and_registers_hwid(settings):
    _override(settings)
    try:
        _reset(); _override(settings)
        client = TestClient(app)
        caller_supplied = "attacker-chosen-state"
        r = client.post("/auth/start", json={"hwid": "HW1"})
        assert r.status_code == 200
        body = r.json()
        state = body["state"]
        assert isinstance(state, str) and state != ""
        assert state != caller_supplied  # 서버 생성 state (호출자가 고를 수 없음)
        assert state != "HW1"
        assert "discord.com/oauth2/authorize" in body["authorize_url"]
        assert state in body["authorize_url"]
        assert "prompt=none" not in body["authorize_url"]
        # 반환된 state로 hwid가 등록됨
        assert main_module.store.get_hwid(state) == "HW1"
    finally:
        _reset()


@respx.mock
def test_callback_and_verify_relay_display_name(settings):
    # 공용 DLL 이름 각인: 콜백이 디스코드 표시이름을 토큰에 넣고 poll/verify 가 되돌려준다
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s-name", "HWN")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(200, json={"access_token": "at"}))
        respx.get(f"{API}/users/@me").mock(
            return_value=httpx.Response(200, json={"id": "user-n", "username": "jr", "global_name": "정렬"}))
        respx.get(f"{API}/guilds/guild-1/members/user-n").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        client = TestClient(app)
        client.get("/auth/callback", params={"code": "c", "state": "s-name"})
        poll = client.get("/auth/poll", params={"state": "s-name"}).json()
        assert poll["status"] == "ready"
        assert poll["name"] == "정렬"  # global_name 우선
        # 같은 토큰으로 verify → name 유지
        respx.get(f"{API}/guilds/guild-1/members/user-n").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        v = client.post("/auth/verify", json={"token": poll["token"], "hwid": "HWN"}).json()
        assert v["valid"] is True and v["name"] == "정렬"
    finally:
        _reset()


@respx.mock
def test_callback_issues_token_for_buyer(settings):
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s2", "HW2")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(200, json={"access_token": "at"}))
        respx.get(f"{API}/users/@me").mock(
            return_value=httpx.Response(200, json={"id": "user-9"}))
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        client = TestClient(app)
        r = client.get("/auth/callback", params={"code": "c", "state": "s2"})
        assert r.status_code == 200
        poll = client.get("/auth/poll", params={"state": "s2"}).json()
        assert poll["status"] == "ready"
        assert poll["token"]
    finally:
        _reset()


@respx.mock
def test_callback_denies_non_buyer(settings):
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s3", "HW3")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(200, json={"access_token": "at"}))
        respx.get(f"{API}/users/@me").mock(
            return_value=httpx.Response(200, json={"id": "user-x"}))
        respx.get(f"{API}/guilds/guild-1/members/user-x").mock(
            return_value=httpx.Response(200, json={"roles": ["role-other"]}))
        client = TestClient(app)
        client.get("/auth/callback", params={"code": "c", "state": "s3"})
        poll = client.get("/auth/poll", params={"state": "s3"}).json()
        assert poll["status"] == "denied"
        assert poll["reason"] == "no_buyer_role"
    finally:
        _reset()


def test_poll_unknown_state_404(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/auth/poll", params={"state": "ghost"}).status_code == 404
    finally:
        _reset()


@respx.mock
def test_verify_valid_token_rechecks_roles(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])  # 실제 현재 시각 발급(살아있는 토큰)
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-x"]}))
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "HW9"})
        body = r.json()
        assert body["valid"] is True
        assert body["sub"] == "user-9"
        assert "sherbet-buyer" in body["roles"]
    finally:
        _reset()


def test_verify_bad_hwid(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"], now=1000)
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "WRONG"})
        assert r.json() == {"valid": False}
    finally:
        _reset()


@respx.mock
def test_callback_discord_error_denies(settings):
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s-err", "HWE")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(500, json={"error": "server_error"}))
        client = TestClient(app)
        r = client.get("/auth/callback", params={"code": "c", "state": "s-err"})
        assert r.status_code == 200  # 500이 아니라 친절한 안내 페이지
        poll = client.get("/auth/poll", params={"state": "s-err"}).json()
        assert poll["status"] == "denied"
        assert poll["reason"] == "discord_error"
    finally:
        _reset()


@respx.mock
def test_callback_malformed_discord_body_denies(settings):
    # 디스코드가 200이지만 본문이 JSON이 아닌 경우 (JSONDecodeError) → 500이 아니라 denied
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s-bad", "HWB")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(200, text="not json at all"))
        client = TestClient(app)
        r = client.get("/auth/callback", params={"code": "c", "state": "s-bad"})
        assert r.status_code == 200
        poll = client.get("/auth/poll", params={"state": "s-bad"}).json()
        assert poll["status"] == "denied"
        assert poll["reason"] == "discord_error"
    finally:
        _reset()


@respx.mock
def test_verify_malformed_member_body_returns_503(settings):
    # 멤버 조회가 200이지만 JSON 깨짐 → valid:false가 아니라 재시도 신호(503)
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, text="not json"))
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "HW9"})
        assert r.status_code == 503
        assert r.json()["error"] == "upstream_unavailable"
    finally:
        _reset()


def test_callback_oauth_error_param_denies(settings):
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s-den", "HWD")
        client = TestClient(app)
        r = client.get("/auth/callback",
                       params={"error": "access_denied", "state": "s-den"})
        assert r.status_code == 200  # 422 아님
        poll = client.get("/auth/poll", params={"state": "s-den"}).json()
        assert poll["status"] == "denied"
        assert poll["reason"] == "oauth_denied"
    finally:
        _reset()


@respx.mock
def test_verify_upstream_error_returns_503(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(500, json={"error": "server_error"}))
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "HW9"})
        assert r.status_code == 503
        body = r.json()
        assert body["valid"] is None
        assert body["error"] == "upstream_unavailable"
    finally:
        _reset()


@respx.mock
def test_verify_non_buyer_returns_valid_false(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        # 멤버는 있으나 buyer 역할 없음
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-other"]}))
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "HW9"})
        assert r.status_code == 200
        assert r.json() == {"valid": False}
    finally:
        _reset()


@respx.mock
def test_verify_member_404_returns_valid_false(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(404, json={"message": "Unknown Member"}))
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "HW9"})
        assert r.status_code == 200
        assert r.json() == {"valid": False}
    finally:
        _reset()


def _themes_file(tmp_path, data):
    import json
    p = tmp_path / "themes.json"
    p.write_text(json.dumps(data), encoding="utf-8")
    return str(p)


@respx.mock
def test_content_me_returns_entitled(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        path = _themes_file(tmp_path, [
            {"id": "free", "role": None, "display_name": "F", "colors": {}, "particle": "leaf", "hue_cycle": False},
            {"id": "gold", "role": "role-gold", "display_name": "G", "colors": {}, "particle": "spark", "hue_cycle": False},
            {"id": "plat", "role": "role-plat", "display_name": "P", "colors": {}, "particle": "heart", "hue_cycle": False},
        ])
        monkeypatch.setattr(main_module, "THEMES_PATH", path)
        main_module.THEMES_CACHE.clear()
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-gold"]}))
        client = TestClient(app)
        r = client.get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200
        themes = r.json()["themes"]
        # 테마는 잠긴 것(plat)도 진열용으로 모두 내려주되 unlocked 로 구분, role 은 숨김
        ids = [t["id"] for t in themes]
        assert ids == ["free", "gold", "plat"]
        assert {t["id"]: t["unlocked"] for t in themes} == {"free": True, "gold": True, "plat": False}
        assert all("role" not in t for t in themes)
    finally:
        _reset()


def test_content_me_no_bearer_401(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/content/me").status_code == 401
    finally:
        _reset()


def test_content_me_bad_token_401(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        r = client.get("/content/me", headers={"Authorization": "Bearer not.a.jwt"})
        assert r.status_code == 401
    finally:
        _reset()


@respx.mock
def test_content_me_discord_down_503(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        path = _themes_file(tmp_path, [{"id": "free", "role": None}])
        monkeypatch.setattr(main_module, "THEMES_PATH", path)
        main_module.THEMES_CACHE.clear()
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(500, json={"error": "boom"}))
        client = TestClient(app)
        r = client.get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 503
    finally:
        _reset()


def _write_json(tmp_path, name, data):
    import json
    p = tmp_path / name
    p.write_text(json.dumps(data), encoding="utf-8")
    return str(p)


def _files_dir(tmp_path, mapping):
    d = tmp_path / "files"
    d.mkdir()
    for fid, content in mapping.items():
        (d / fid).write_bytes(content)
    return str(d)


@respx.mock
def test_content_me_includes_presets_and_effects(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "THEMES_PATH", _write_json(tmp_path, "themes.json", []))
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", [
            {"id": "free-p", "role": None, "filename": "free.ini", "display_name": "F"},
            {"id": "gold-p", "role": "role-gold", "filename": "gold.ini", "display_name": "G"},
            {"id": "plat-p", "role": "role-plat", "filename": "plat.ini", "display_name": "P"},
        ]))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", [
            {"id": "gold-fx", "role": "role-gold", "filename": "gold.fx", "display_name": "GFX"},
        ]))
        main_module.THEMES_CACHE.clear(); main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-gold"]}))
        client = TestClient(app)
        r = client.get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200
        body = r.json()
        # 프리셋 = 판매 상품이므로 **잠긴 것도 진열**한다(안 산 사람 화면에 상품이 존재해야 한다).
        assert [p["display_name"] for p in body["presets"]] == ["F", "G", "P"]
        assert [p["unlocked"] for p in body["presets"]] == [True, True, False]
        # ⚠️ 계약의 핵심: **잠긴 항목에는 id 가 없다.** id 는 /content/file/<id> 다운로드 키이고,
        #    구버전 클라(id 없으면 건너뜀)가 잠긴 상품을 받으려 시도하지 않게 하는 하위호환 장치다.
        assert [p["id"] for p in body["presets"] if "id" in p] == ["free-p", "gold-p"]
        assert "id" not in [p for p in body["presets"] if p["display_name"] == "P"][0]
        # ⚠️ unlocked 는 진짜 JSON 불리언이어야 한다(클라는 json_bool 로 읽는다).
        assert all(isinstance(p["unlocked"], bool) for p in body["presets"])
        # 이펙트는 상품이 아니라 프리셋이 쓰는 셰이더 파일이다 — 진열하지 않고 게이트를 유지한다.
        assert [e["id"] for e in body["effects"]] == ["gold-fx"]
        assert all("unlocked" not in e for e in body["effects"])
        assert all("role" not in p for p in body["presets"])
        assert all("role" not in e for e in body["effects"])
    finally:
        _reset()


@respx.mock
def test_content_me_features_role_gated(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "THEMES_PATH", _write_json(tmp_path, "themes.json", []))
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", []))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FEATURES_PATH", _write_json(tmp_path, "features.json", [
            {"id": "custompicture", "role": "role-pic"},
            {"id": "freefeature", "role": None},
        ]))
        main_module.THEMES_CACHE.clear(); main_module.PRESETS_CACHE.clear()
        main_module.EFFECTS_CACHE.clear(); main_module.FEATURES_CACHE.clear()
        # role-pic 없는 유저 → custompicture 미포함, 무료 기능만
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        r = TestClient(app).get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.json()["features"] == ["freefeature"]
    finally:
        _reset()


@respx.mock
def test_content_me_features_unlocked_with_role(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "THEMES_PATH", _write_json(tmp_path, "themes.json", []))
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", []))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FEATURES_PATH", _write_json(tmp_path, "features.json", [
            {"id": "custompicture", "role": "role-pic"},
        ]))
        main_module.THEMES_CACHE.clear(); main_module.PRESETS_CACHE.clear()
        main_module.EFFECTS_CACHE.clear(); main_module.FEATURES_CACHE.clear()
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-pic"]}))
        r = TestClient(app).get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.json()["features"] == ["custompicture"]
    finally:
        _reset()


@respx.mock
def test_content_file_serves_entitled_bytes(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", [
            {"id": "gold-p", "role": "role-gold", "filename": "gold.ini"},
        ]))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FILES_DIR", _files_dir(tmp_path, {"gold-p": b"Techniques=X\n"}))
        main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "u1", "HW", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/u1").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-gold"]}))
        client = TestClient(app)
        r = client.get("/content/file/gold-p", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200
        assert r.content == b"Techniques=X\n"
    finally:
        _reset()


@respx.mock
def test_content_file_403_when_role_missing(settings, tmp_path, monkeypatch):
    # 매니페스트엔 있지만 사용자가 그 role 미보유 → 403 (id 추측 방어)
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", [
            {"id": "gold-p", "role": "role-gold", "filename": "gold.ini"},
        ]))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FILES_DIR", _files_dir(tmp_path, {"gold-p": b"secret"}))
        main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "u2", "HW", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/u2").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))  # role-gold 없음
        client = TestClient(app)
        r = client.get("/content/file/gold-p", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 403
    finally:
        _reset()


@respx.mock
def test_content_file_404_unknown_id(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", []))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FILES_DIR", _files_dir(tmp_path, {}))
        main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "u3", "HW", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/u3").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        client = TestClient(app)
        r = client.get("/content/file/ghost", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 404
    finally:
        _reset()


def test_content_file_no_bearer_401(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/content/file/whatever").status_code == 401
    finally:
        _reset()


@respx.mock
def test_content_file_free_item_no_role(settings, tmp_path, monkeypatch):
    # role:null 무료 아이템 → 인증만으로 200
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", [
            {"id": "free-p", "role": None, "filename": "free.ini"},
        ]))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FILES_DIR", _files_dir(tmp_path, {"free-p": b"free-bytes"}))
        main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "u4", "HW", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/u4").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        client = TestClient(app)
        r = client.get("/content/file/free-p", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200 and r.content == b"free-bytes"
    finally:
        _reset()


def _xh_paths(tmp_path, monkeypatch, crosshairs):
    """조준점만 보려는 테스트용 — 나머지 매니페스트는 전부 비우고 캐시를 지운다."""
    monkeypatch.setattr(main_module, "THEMES_PATH", _write_json(tmp_path, "themes.json", []))
    monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", []))
    monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
    monkeypatch.setattr(main_module, "FEATURES_PATH", _write_json(tmp_path, "features.json", []))
    monkeypatch.setattr(main_module, "CROSSHAIRS_PATH", _write_json(tmp_path, "crosshairs.json", crosshairs))
    main_module.THEMES_CACHE.clear(); main_module.PRESETS_CACHE.clear()
    main_module.EFFECTS_CACHE.clear(); main_module.FEATURES_CACHE.clear()
    main_module.CROSSHAIRS_CACHE.clear()


@respx.mock
def test_content_me_crosshairs_role_gated(settings, tmp_path, monkeypatch):
    """조준점 판매 = 역할 부여. 잠긴 항목은 진열되지만 코드는 응답에 없다."""
    _reset(); _override(settings)
    try:
        _xh_paths(tmp_path, monkeypatch, [
            {"id": "free-x", "role": None, "display_name": "무료 점", "code": "0;P;d;1;0b;0;1b;0"},
            {"id": "pro-x", "role": "role-pro", "display_name": "프로 조준점",
             "author": "TenZ", "tag": "프로", "code": "0;P;c;5;0l;4;0o;2"},
        ])
        tok = issue_token(settings, "user-x", "HWX", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-x").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))  # role-pro 없음
        r = TestClient(app).get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200
        xh = r.json()["crosshairs"]
        assert [c["id"] for c in xh] == ["free-x", "pro-x"]
        assert xh[0]["unlocked"] is True and xh[0]["code"] == "0;P;d;1;0b;0;1b;0"
        assert xh[1]["unlocked"] is False
        assert "code" not in xh[1]
        # 응답 본문 어디에도 잠긴 코드가 없어야 한다(캐시 파일에 남는 것이 곧 유출이다)
        assert "0;P;c;5;0l;4;0o;2" not in r.text
        assert all("role" not in c for c in xh)
    finally:
        _reset()


@respx.mock
def test_content_me_crosshairs_unlocked_with_role(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        _xh_paths(tmp_path, monkeypatch, [
            {"id": "pro-x", "role": "role-pro", "display_name": "프로 조준점", "code": "0;P;c;5;0l;4;0o;2"},
        ])
        tok = issue_token(settings, "user-y", "HWY", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-y").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-pro"]}))
        r = TestClient(app).get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        xh = r.json()["crosshairs"]
        assert xh[0]["unlocked"] is True and xh[0]["code"] == "0;P;c;5;0l;4;0o;2"
    finally:
        _reset()


@respx.mock
def test_content_me_crosshairs_absent_manifest_is_empty_list(settings, tmp_path, monkeypatch):
    """crosshairs.json 이 아직 없는 서버 — 키는 있고 빈 배열이어야 한다(클라가 조용히 빈 마켓)."""
    _reset(); _override(settings)
    try:
        _xh_paths(tmp_path, monkeypatch, [])
        monkeypatch.setattr(main_module, "CROSSHAIRS_PATH", str(tmp_path / "does-not-exist.json"))
        main_module.CROSSHAIRS_CACHE.clear()
        tok = issue_token(settings, "user-z", "HWZ", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-z").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        r = TestClient(app).get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.json()["crosshairs"] == []
    finally:
        _reset()


@respx.mock
def test_content_me_crosshairs_parse_in_real_client(settings, tmp_path, monkeypatch, crosshair_parser):
    """라우터의 **실제 응답 바디**를 진짜 클라 파서에 먹인다.

    파이썬 단언은 "문자열인가" 까지만 본다. 클라가 그 코드를 실제로 적용할 수 있는지는
    여기서만 확인된다 — 코드 한 글자가 틀리면 카드가 '사용 불가' 로 뜨는데
    서버 쪽에서는 아무 오류도 나지 않는다.
    """
    _reset(); _override(settings)
    try:
        _xh_paths(tmp_path, monkeypatch, [
            {"id": "free-x", "role": None, "display_name": "무료 점", "author": "Sherbet",
             "tag": "점", "code": "0;P;c;5;o;1;d;1;z;3;0b;0;1b;0"},
            {"id": "pro-x", "role": "role-pro", "display_name": "프로 조준점", "code": "0;P;c;5;0l;4;0o;2"},
            {"id": "bad-x", "role": None, "display_name": "깨진 코드", "code": "1;나쁜코드"},
        ])
        tok = issue_token(settings, "user-p", "HWP", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-p").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        r = TestClient(app).get("/content/me", headers={"Authorization": f"Bearer {tok}"})

        entries = crosshair_parser(r.text)
        assert [e["id"] for e in entries] == ["free-x", "pro-x", "bad-x"]
        # 1) 무료 항목은 클라에서 바로 적용 가능해야 한다
        assert entries[0]["state"] == "ready" and entries[0]["applicable"]
        assert entries[0]["name"] == "무료 점" and entries[0]["author"] == "Sherbet"
        assert entries[0]["code"] == "0;P;c;5;o;1;d;1;z;3;0b;0;1b;0"
        # 2) 잠긴 항목은 진열되지만 코드가 없고 적용도 불가
        assert entries[1]["state"] == "locked" and not entries[1]["applicable"]
        assert entries[1]["code"] == ""
        assert entries[1]["name"] == "프로 조준점"  # 이름은 보여야 "사고 싶다" 가 된다
        # 3) 매니페스트에 잘못 적힌 코드는 카드로 남되 적용 불가(조용히 사라지지 않는다)
        assert entries[2]["state"] == "broken" and not entries[2]["applicable"]
    finally:
        _reset()


@respx.mock
def test_content_me_shipped_crosshair_manifest_is_valid(settings, monkeypatch, crosshair_parser):
    """리포에 커밋된 server/content/crosshairs.json 이 실제로 적용 가능한지.

    운영자가 여기에 코드를 붙여넣다 한 글자를 틀리면 전 고객의 카드가 '사용 불가' 가
    되는데, 그 사실을 알 방법이 이 테스트 말고는 없다.
    """
    _reset(); _override(settings)
    try:
        for cache in (main_module.THEMES_CACHE, main_module.PRESETS_CACHE,
                      main_module.EFFECTS_CACHE, main_module.FEATURES_CACHE,
                      main_module.CROSSHAIRS_CACHE):
            cache.clear()
        tok = issue_token(settings, "user-s", "HWS", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-s").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        r = TestClient(app).get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        entries = crosshair_parser(r.text)
        assert entries, "출하 매니페스트에 조준점이 하나도 없다"
        for e in entries:
            # 커밋된 항목은 전부 무료(role:null)이므로 전부 적용 가능해야 한다
            assert e["state"] == "ready", f"{e['id']} 의 코드가 클라 파서를 통과하지 못한다"
            assert e["applicable"]
            assert e["name"]
    finally:
        _reset()
