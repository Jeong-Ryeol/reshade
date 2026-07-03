import json

from app.content import load_themes, entitled_themes


def _write(tmp_path, data):
    p = tmp_path / "themes.json"
    p.write_text(json.dumps(data), encoding="utf-8")
    return str(p)


def test_load_missing_returns_empty(tmp_path):
    assert load_themes(str(tmp_path / "nope.json"), {}) == []


def test_load_and_mtime_cache(tmp_path):
    path = _write(tmp_path, [{"id": "a", "role": None}])
    cache = {}
    first = load_themes(path, cache)
    assert first == [{"id": "a", "role": None}]
    # 파일 안 바뀌면 같은 (캐시된) 객체 반환
    assert load_themes(path, cache) is first


def test_load_broken_json_returns_empty(tmp_path):
    p = tmp_path / "themes.json"
    p.write_text("{not json", encoding="utf-8")
    assert load_themes(str(p), {}) == []


def test_entitled_free_and_role_gated():
    themes = [
        {"id": "free", "role": None, "display_name": "F"},
        {"id": "gold", "role": "111", "display_name": "G"},
        {"id": "plat", "role": "222", "display_name": "P"},
    ]
    out = entitled_themes(themes, ["111"])
    ids = [t["id"] for t in out]
    assert ids == ["free", "gold"]
    assert all("role" not in t for t in out)


def test_entitled_empty_roles_gets_only_free():
    themes = [{"id": "free", "role": None}, {"id": "gold", "role": "111"}]
    assert [t["id"] for t in entitled_themes(themes, [])] == ["free"]
