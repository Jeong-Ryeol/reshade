import json
import os

import pytest
from fastapi.testclient import TestClient

from app.content import load_json_object


def test_load_json_object_reads_dict(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"schema": 1, "version": "1.4.0"}), encoding="utf-8")
    cache = {}
    assert load_json_object(str(p), cache) == {"schema": 1, "version": "1.4.0"}


def test_load_json_object_missing_file(tmp_path):
    assert load_json_object(str(tmp_path / "nope.json"), {}) == {}


def test_load_json_object_broken_json(tmp_path):
    p = tmp_path / "update.json"
    p.write_text("{not json", encoding="utf-8")
    assert load_json_object(str(p), {}) == {}


def test_load_json_object_rejects_non_dict(tmp_path):
    # 배열을 주면 빈 dict — load_manifest 의 거울상
    p = tmp_path / "update.json"
    p.write_text(json.dumps([1, 2, 3]), encoding="utf-8")
    assert load_json_object(str(p), {}) == {}


def test_load_json_object_mtime_cache(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"version": "1.0.0"}), encoding="utf-8")
    cache = {}
    first = load_json_object(str(p), cache)
    assert first["version"] == "1.0.0"
    # 파일을 바꾸되 mtime 을 되돌리면 캐시가 살아 있어야 한다
    st = os.stat(str(p))
    p.write_text(json.dumps({"version": "9.9.9"}), encoding="utf-8")
    os.utime(str(p), (st.st_atime, st.st_mtime))
    assert load_json_object(str(p), cache)["version"] == "1.0.0"
    # mtime 이 진짜로 바뀌면 다시 읽는다
    os.utime(str(p), (st.st_atime, st.st_mtime + 10))
    assert load_json_object(str(p), cache)["version"] == "9.9.9"


def test_load_json_object_clears_cache_when_file_disappears(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"version": "1.0.0"}), encoding="utf-8")
    cache = {}
    load_json_object(str(p), cache)
    os.remove(str(p))
    assert load_json_object(str(p), cache) == {}
    assert "data" not in cache


@pytest.fixture
def client(tmp_path, monkeypatch):
    from app import update as upd
    from app.main import app
    p = tmp_path / "update.json"
    p.write_text(json.dumps({
        "schema": 1,
        "version": "1.4.0",
        "min_version": "1.0.0",
        "allow_downgrade": False,
        "notice": "",
        "notes": "OSD 가로 배치 추가\n로그인 만료 버그 수정",
        "builds": {
            "x64": {
                "url": "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll",
                "size": 4312576,
                "sha256": "3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80",
            },
            "x86": {
                "url": "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade32.dll",
                "size": 3837952,
                "sha256": "9d40b1000000000000000000000000000000000000000000000000000000005a",
            },
        },
    }), encoding="utf-8")
    monkeypatch.setattr(upd, "UPDATE_PATH", str(p))
    upd.UPDATE_CACHE.clear()
    return TestClient(app)


def test_manifest_x64(client):
    r = client.get("/update/manifest", params={"arch": "x64", "cur": "1.3.0"})
    assert r.status_code == 200
    b = r.json()
    assert b["arch"] == "x64"
    assert b["version"] == "1.4.0"
    assert b["url"].endswith("ReShade64.dll")


def test_all_scalars_are_strings_except_allow_downgrade(client):
    """⚠️ 이 테스트가 이 라우터의 존재 이유다.
    클라의 json_string 은 따옴표 있는 문자열만 읽는다. 숫자를 그대로 내면
    클라는 에러 없이 '업데이트 없음' 으로 넘어가고 로그에도 안 남는다."""
    b = client.get("/update/manifest", params={"arch": "x64"}).json()
    for key in ("schema", "arch", "version", "min_version", "sha256", "url", "size", "notes", "notice"):
        assert isinstance(b[key], str), f"{key} 는 문자열이어야 한다 (실제 {type(b[key]).__name__})"
    assert isinstance(b["allow_downgrade"], bool)
    assert b["schema"] == "1"
    assert b["size"] == "4312576"


def test_key_needles_appear_exactly_once(client):
    """클라의 has_unique_keys 는 바디 원문에서 각 니들의 등장 횟수를 센다.
    2회 이상이면 매니페스트 전체를 거부한다."""
    raw = client.get("/update/manifest", params={"arch": "x64"}).text
    for needle in ('"schema"', '"arch"', '"version"', '"min_version"',
                   '"sha256"', '"url"', '"size"', '"allow_downgrade"'):
        assert raw.count(needle) == 1, f"{needle} 가 {raw.count(needle)} 회 등장한다"


def test_notes_with_quotes_do_not_shadow(client, tmp_path, monkeypatch):
    """정직한 릴리스 노트에 따옴표가 들어가도 키가 가려지면 안 된다."""
    from app import update as upd
    data = json.loads(open(upd.UPDATE_PATH, encoding="utf-8").read())
    data["notes"] = '"url" 버튼과 "size" 표시를 고쳤어요'
    open(upd.UPDATE_PATH, "w", encoding="utf-8").write(json.dumps(data))
    upd.UPDATE_CACHE.clear()
    raw = client.get("/update/manifest", params={"arch": "x64"}).text
    assert raw.count('"url"') == 1
    assert raw.count('"size"') == 1


def test_x86_and_x64_do_not_leak(client):
    a = client.get("/update/manifest", params={"arch": "x64"}).json()
    b = client.get("/update/manifest", params={"arch": "x86"}).json()
    assert a["url"].endswith("ReShade64.dll") and b["url"].endswith("ReShade32.dll")
    assert a["sha256"] != b["sha256"] and a["size"] != b["size"]


def test_bad_arch(client):
    assert client.get("/update/manifest", params={"arch": "arm64"}).status_code == 400
    assert client.get("/update/manifest").status_code == 422  # arch 필수


def test_missing_file_is_503(client, monkeypatch):
    from app import update as upd
    monkeypatch.setattr(upd, "UPDATE_PATH", "/nonexistent/update.json")
    upd.UPDATE_CACHE.clear()
    assert client.get("/update/manifest", params={"arch": "x64"}).status_code == 503


def test_missing_arch_slot_is_503(client):
    from app import update as upd
    data = json.loads(open(upd.UPDATE_PATH, encoding="utf-8").read())
    del data["builds"]["x86"]
    open(upd.UPDATE_PATH, "w", encoding="utf-8").write(json.dumps(data))
    upd.UPDATE_CACHE.clear()
    assert client.get("/update/manifest", params={"arch": "x86"}).status_code == 503
    assert client.get("/update/manifest", params={"arch": "x64"}).status_code == 200


def test_no_auth_required(client):
    """인증을 요구하면 '로그인을 망가뜨린 빌드' 를 영원히 고칠 수 없다(스펙 §3.1)."""
    r = client.get("/update/manifest", params={"arch": "x64"})
    assert r.status_code == 200  # Authorization 헤더 없이도


# ── min_version: '없음' 과 '빈 문자열' 은 전혀 다른 것이다 ────────────────────
# ⚠️ 이 블록이 놓쳤던 사고를 재현한다. 라우터가 `str(doc.get("min_version", ""))` 을
# 내보내면, min_version 을 안 쓰는 평범한 릴리스가 `"min_version": ""` 을 실어 보내고
# 클라의 parse_manifest 는 그 매니페스트를 **통째로 거부**한다(키가 있는데 파싱 불가).
# 즉 **첫 실배포에서 전 고객이 업데이트를 한 번도 못 받고, 아무 데도 에러가 안 남는다.**
# 기존 픽스처가 min_version 을 채워 둔 탓에 이 조합이 한 번도 실행되지 않았다.
# 그래서 여기서는 HTTP 모양이 아니라 **진짜 클라 파서 왕복**을 단언한다.

def _rewrite_manifest(monkeypatch, tmp_path, **fields):
    """픽스처 update.json 을 복사해 doc 수준 필드를 갈아끼운다. 값이 ... 면 키를 지운다."""
    from app import update as upd
    data = json.loads(open(upd.UPDATE_PATH, encoding="utf-8").read())
    for k, v in fields.items():
        if v is ...:
            data.pop(k, None)
        else:
            data[k] = v
    p = tmp_path / "rewritten.json"
    p.write_text(json.dumps(data), encoding="utf-8")
    monkeypatch.setattr(upd, "UPDATE_PATH", str(p))
    upd.UPDATE_CACHE.clear()


def test_min_version_absent_is_omitted_not_empty(client, client_parser, tmp_path, monkeypatch):
    """update.json 에 min_version 이 아예 없는 경우 — 가장 흔한 평범한 릴리스."""
    _rewrite_manifest(monkeypatch, tmp_path, min_version=...)
    r = client.get("/update/manifest", params={"arch": "x64"})
    assert r.status_code == 200
    assert "min_version" not in r.json(), "값이 없으면 키 자체를 빼야 한다(빈 문자열 금지)"
    assert '"min_version"' not in r.text
    u = client_parser(r.text, "x64")
    assert u["ok"], "min_version 이 없다고 매니페스트가 거부되면 전 고객이 업데이트를 못 받는다"
    assert u["min_version"] == ""      # 선택 필드 미사용 = 강제 업데이트 아님
    assert u["version"] == "1.4.0"


def test_min_version_empty_string_is_omitted(client, client_parser, tmp_path, monkeypatch):
    """운영자가 `"min_version": ""` 로 '없음' 을 표현한 경우(update.example.json 이 그렇다)."""
    _rewrite_manifest(monkeypatch, tmp_path, min_version="")
    r = client.get("/update/manifest", params={"arch": "x64"})
    assert r.status_code == 200
    assert "min_version" not in r.json()
    assert client_parser(r.text, "x64")["ok"]


def test_min_version_whitespace_is_omitted(client, client_parser, tmp_path, monkeypatch):
    """공백뿐인 값도 '없음' 이다 — 클라의 parse_version 은 앞뒤 공백을 거부한다."""
    _rewrite_manifest(monkeypatch, tmp_path, min_version="   ")
    r = client.get("/update/manifest", params={"arch": "x64"})
    assert r.status_code == 200
    assert "min_version" not in r.json()
    assert client_parser(r.text, "x64")["ok"]


def test_min_version_valid_is_passed_through(client, client_parser, tmp_path, monkeypatch):
    """진짜 강제 업데이트 — 값이 그대로 실려 클라가 읽을 수 있어야 한다."""
    _rewrite_manifest(monkeypatch, tmp_path, min_version="1.2.3")
    r = client.get("/update/manifest", params={"arch": "x64"})
    assert r.status_code == 200
    assert r.json()["min_version"] == "1.2.3"
    u = client_parser(r.text, "x64")
    assert u["ok"] and u["min_version"] == "1.2.3"
    # 앞뒤 공백은 잘라서 내보낸다 — 그대로 실으면 parse_version 이 거부해 전체가 죽는다
    _rewrite_manifest(monkeypatch, tmp_path, min_version="  1.2.3\n")
    r = client.get("/update/manifest", params={"arch": "x64"})
    assert r.json()["min_version"] == "1.2.3"
    assert client_parser(r.text, "x64")["ok"]


def test_min_version_as_number_is_loud_503(client, tmp_path, monkeypatch):
    """따옴표를 빠뜨린 `"min_version": 1.0` — 조용히 넘어가면 안 된다.

    빼면 강제 업데이트가 소리 없이 무력화되고, 그대로 내보내면 클라가 전체를 거부해
    전 고객의 업데이트가 죽는다. 어느 쪽이든 채널이 멈추므로 **알아챌 수 있는 쪽**을 고른다.
    """
    for bad in (1.0, 1, True, ["1.0.0"], {"v": "1.0.0"}):
        _rewrite_manifest(monkeypatch, tmp_path, min_version=bad)
        r = client.get("/update/manifest", params={"arch": "x64"})
        assert r.status_code == 503, f"{bad!r} 를 조용히 통과시켰다"
        assert r.json()["error"] == "bad_min_version"


def test_min_version_malformed_string_is_loud_503(client, tmp_path, monkeypatch):
    """"1.0" / "v1.4.0" / "1.4.0.1" — 클라 parse_version 이 못 읽는 형식은 전부 503."""
    for bad in ("1.0", "v1.4.0", "1.4.0.1", "1.4.x", "9999999.0.0"):
        _rewrite_manifest(monkeypatch, tmp_path, min_version=bad)
        r = client.get("/update/manifest", params={"arch": "x64"})
        assert r.status_code == 503, f"{bad!r} 를 조용히 통과시켰다"
        assert r.json()["error"] == "bad_min_version"


# ── 서버↔클라 상설 크로스체크 ────────────────────────────────────────────────
# 위 min_version 테스트들과 같은 하네스를 쓰되, 여기서는 '정상 응답이 정말로 통과하는가'
# 와 '피닝을 어긴 응답이 정말로 거부되는가' 양쪽을 못 박는다. 한쪽만 있으면
# 아무거나 거부하는 파서(또는 아무거나 통과시키는 파서)도 초록불이 된다.

def test_real_response_passes_real_client_parser(client, client_parser):
    for arch, dll in (("x64", "ReShade64.dll"), ("x86", "ReShade32.dll")):
        r = client.get("/update/manifest", params={"arch": arch, "cur": "1.3.0"})
        assert r.status_code == 200
        u = client_parser(r.text, arch)
        assert u["ok"], f"{arch}: 라우터의 실제 응답을 클라가 거부한다"
        assert u["version"] == "1.4.0"
        assert u["url"].endswith(dll)
        assert u["size"] == str(r.json()["size"])
        assert u["sha256"] == r.json()["sha256"]


def test_arch_echo_mismatch_is_rejected_by_client(client, client_parser):
    """x64 응답을 x86 클라가 읽으면 거부돼야 한다(아키텍처 교차 설치 금지)."""
    r = client.get("/update/manifest", params={"arch": "x64"})
    assert not client_parser(r.text, "x86")["ok"]


def test_url_pin_violation_is_rejected_by_client(client, client_parser, tmp_path, monkeypatch):
    """홈서버가 털려 매니페스트의 url 이 바뀌어도 클라는 설치하지 않는다(스펙 §3.4).

    이 단언이 없으면 '서버가 내보낸 것은 무엇이든 클라가 받는다' 는 가정이 검증 없이 남는다.
    """
    from app import update as upd
    evil = [
        "https://github.com/attacker/evil/releases/download/x/ReShade64.dll",
        "https://github.com.evil.kr/Jeong-Ryeol/reshade/releases/download/x/a.dll",
        "http://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll",
        "https://github.com/Jeong-Ryeol/reshade/releases/download/%2e%2e/%2e%2e/attacker/e.dll",
        # 전각 ．．／ — 베스트핏 매핑으로 ../ 로 접힐 수 있다
        "https://github.com/Jeong-Ryeol/reshade/releases/download/．．／attacker/e.dll",
    ]
    for url in evil:
        data = json.loads(open(upd.UPDATE_PATH, encoding="utf-8").read())
        data["builds"]["x64"]["url"] = url
        p = tmp_path / "evil.json"
        p.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        monkeypatch.setattr(upd, "UPDATE_PATH", str(p))
        upd.UPDATE_CACHE.clear()
        r = client.get("/update/manifest", params={"arch": "x64"})
        assert r.status_code == 200          # 서버는 url 을 심사하지 않는다
        assert not client_parser(r.text, "x64")["ok"], f"클라가 {url} 을 통과시켰다"
