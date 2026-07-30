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


def test_items_with_lock_includes_all_with_flag():
    items = [
        {"id": "free", "role": None, "display_name": "F"},
        {"id": "gold", "role": "111", "display_name": "G"},
        {"id": "plat", "role": "222", "display_name": "P"},
    ]
    out = content.items_with_lock(items, ["111"])
    # 잠긴 것도 포함(진열), role 은 제거, unlocked 로 구분
    assert [i["id"] for i in out] == ["free", "gold", "plat"]
    assert {i["id"]: i["unlocked"] for i in out} == {"free": True, "gold": True, "plat": False}
    assert all("role" not in i for i in out)


# ── 조준점 마켓 ────────────────────────────────────────────────────────────
# 항목 하나가 공유 코드 한 줄이다. 파일 배포가 없으므로 게이트는 이 함수가 전부다.

def test_crosshair_items_locked_entry_carries_no_code():
    """잠긴 항목은 이름만 진열되고 code 는 응답에 실리지 않는다.

    코드가 곧 상품이라, 잠긴 채로 내려보내면 캐시 파일만 열어도 가져갈 수 있다.
    """
    items = [
        {"id": "free", "role": None, "display_name": "F", "code": "0"},
        {"id": "pro", "role": "r-pro", "display_name": "P", "code": "0;P;c;5"},
    ]
    out = content.crosshair_items(items, [])
    assert [i["id"] for i in out] == ["free", "pro"]      # 잠긴 것도 진열은 한다
    assert out[0]["unlocked"] is True and out[0]["code"] == "0"
    assert out[1]["unlocked"] is False
    assert "code" not in out[1]                            # ← 코드 없음
    assert all("role" not in i for i in out)


def test_crosshair_items_unlocks_with_role():
    items = [{"id": "pro", "role": "r-pro", "display_name": "P", "code": "0;P;c;5"}]
    out = content.crosshair_items(items, ["r-pro"])
    assert out[0]["unlocked"] is True
    assert out[0]["code"] == "0;P;c;5"


def test_crosshair_items_values_are_strings():
    """클라 파서는 따옴표 없는 값을 못 읽는다(설계 §3.3). 숫자를 적어도 문자열로 나간다."""
    items = [{"id": 7, "role": None, "display_name": 42, "author": 1, "tag": 2, "code": 0}]
    out = content.crosshair_items(items, [])
    assert out == [{"id": "7", "display_name": "42", "author": "1", "tag": "2",
                    "code": "0", "unlocked": True}]
    for k, v in out[0].items():
        assert isinstance(v, str) or k == "unlocked"
    assert isinstance(out[0]["unlocked"], bool)  # unlocked 만 진짜 불리언


def test_crosshair_items_drops_unusable():
    items = [
        {"role": None, "code": "0"},                       # id 없음
        {"id": "", "role": None, "code": "0"},             # 빈 id
        {"id": "  ", "role": None, "code": "0"},           # 공백뿐인 id
        {"id": "nocode", "role": None},                    # 열려 있는데 코드 없음
        {"id": "blank", "role": None, "code": "   "},      # 공백뿐인 코드
        {"id": "ok", "role": None, "code": "0"},
    ]
    assert [i["id"] for i in content.crosshair_items(items, [])] == ["ok"]


def test_crosshair_items_locked_without_code_still_listed():
    # 운영자가 코드를 아직 안 넣은 잠긴 항목 — '잠김' 카드로는 뜬다
    items = [{"id": "coming", "role": "r-x", "display_name": "곧 공개"}]
    out = content.crosshair_items(items, [])
    assert out == [{"id": "coming", "display_name": "곧 공개", "unlocked": False}]


def test_crosshair_items_strips_unknown_keys():
    # 매니페스트에 메모를 적어도 클라로 나가지 않는다(페이로드는 평면 + 화이트리스트)
    items = [{"id": "a", "role": None, "code": "0", "memo": {"nested": "obj"}, "price": 5000}]
    out = content.crosshair_items(items, [])
    assert set(out[0]) == {"id", "code", "unlocked"}


def test_crosshair_items_empty_manifest():
    assert content.crosshair_items([], []) == []
    assert content.crosshair_items([], ["r-pro"]) == []
