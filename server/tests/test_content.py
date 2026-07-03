import app.content as content


def test_load_manifest_missing_returns_empty(tmp_path):
    cache = {}
    assert content.load_manifest(str(tmp_path / "nope.json"), cache) == []


def test_load_manifest_reads_and_caches(tmp_path):
    p = tmp_path / "m.json"
    p.write_text('[{"id":"a"}]', encoding="utf-8")
    cache = {}
    first = content.load_manifest(str(p), cache)
    assert first == [{"id": "a"}]
    # 파일 안 바뀌면 같은 (캐시된) 객체 반환
    assert content.load_manifest(str(p), cache) is first
    assert cache["data"] == [{"id": "a"}]


def test_load_manifest_non_list_becomes_empty(tmp_path):
    p = tmp_path / "m.json"
    p.write_text('{"not":"list"}', encoding="utf-8")
    assert content.load_manifest(str(p), {}) == []


def test_load_manifest_corrupt_returns_empty(tmp_path):
    p = tmp_path / "m.json"
    p.write_text("{bad json", encoding="utf-8")
    assert content.load_manifest(str(p), {}) == []


def test_entitled_items_filters_and_strips_role():
    items = [
        {"id": "free", "role": None, "filename": "f.ini"},
        {"id": "gold", "role": "r-gold", "filename": "g.ini"},
        {"id": "plat", "role": "r-plat", "filename": "p.ini"},
    ]
    out = content.entitled_items(items, ["r-gold"])
    assert [i["id"] for i in out] == ["free", "gold"]
    assert all("role" not in i for i in out)


def test_entitled_items_empty_roles_gets_only_free():
    items = [{"id": "free", "role": None}, {"id": "gold", "role": "111"}]
    assert [i["id"] for i in content.entitled_items(items, [])] == ["free"]


def test_find_item():
    items = [{"id": "a"}, {"id": "b"}]
    assert content.find_item(items, "b") == {"id": "b"}
    assert content.find_item(items, "zzz") is None


def test_is_entitled():
    assert content.is_entitled({"id": "x", "role": None}, []) is True   # 무료
    assert content.is_entitled({"id": "x"}, []) is True                 # role 키 없음=무료
    assert content.is_entitled({"id": "x", "role": "r1"}, ["r1"]) is True
    assert content.is_entitled({"id": "x", "role": "r1"}, ["r2"]) is False
    assert content.is_entitled({"id": "x", "role": "r1"}, None) is False
