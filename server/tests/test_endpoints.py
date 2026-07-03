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
        ids = [t["id"] for t in r.json()["themes"]]
        assert ids == ["free", "gold"]
        assert all("role" not in t for t in r.json()["themes"])
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
        assert [p["id"] for p in body["presets"]] == ["free-p", "gold-p"]
        assert [e["id"] for e in body["effects"]] == ["gold-fx"]
        assert all("role" not in p for p in body["presets"])
        assert all("role" not in e for e in body["effects"])
    finally:
        _reset()
