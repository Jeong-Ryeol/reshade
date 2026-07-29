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
