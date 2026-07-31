import json
import os


def load_manifest(path: str, mtime_cache: dict) -> list[dict]:
    """콘텐츠 매니페스트(JSON 배열)를 읽는다. mtime_cache 로 파일이 안 바뀌면 캐시 반환.
    파일 없음/깨짐이면 빈 목록(서비스가 죽지 않게)."""
    try:
        mtime = os.path.getmtime(path)
    except OSError:
        mtime_cache.pop("mtime", None)
        mtime_cache.pop("data", None)
        return []
    if mtime_cache.get("mtime") == mtime and "data" in mtime_cache:
        return mtime_cache["data"]
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return []
    if not isinstance(data, list):
        data = []
    mtime_cache["mtime"] = mtime
    mtime_cache["data"] = data
    return data


def load_json_object(path: str, mtime_cache: dict) -> dict:
    """JSON 객체(dict) 파일을 읽는다. load_manifest 와 같은 mtime 캐시·fail-soft 규약.
    파일 없음/깨짐/비-dict 이면 빈 dict — 업데이트 안내가 없을 뿐 서비스는 살아 있어야 한다."""
    try:
        mtime = os.path.getmtime(path)
    except OSError:
        mtime_cache.pop("mtime", None)
        mtime_cache.pop("data", None)
        return {}
    if mtime_cache.get("mtime") == mtime and "data" in mtime_cache:
        return mtime_cache["data"]
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    if not isinstance(data, dict):
        data = {}
    mtime_cache["mtime"] = mtime
    mtime_cache["data"] = data
    return data


def is_entitled(item: dict, role_ids: list[str]) -> bool:
    """item 의 role 이 falsy(무료)이거나 사용자 역할 ID 목록에 포함되면 True."""
    role = item.get("role")
    return (not role) or role in set(role_ids or [])


def entitled_items(items: list[dict], role_ids: list[str]) -> list[dict]:
    """권한 있는 아이템만, role 키를 제거해 반환."""
    return [{k: v for k, v in it.items() if k != "role"}
            for it in items if is_entitled(it, role_ids)]


def items_with_lock(items: list[dict], role_ids: list[str]) -> list[dict]:
    """모든 아이템을 반환하되 role 은 제거하고 unlocked(bool)를 붙인다.
    권한 없는 것도 목록에 포함(마켓 '잠김' 진열용). 역할 ID 는 노출 안 함.
    파일 다운로드가 없는 콘텐츠(테마)에만 쓴다 — 파일형은 entitled_items 로 게이트."""
    out = []
    for it in items:
        entry = {k: v for k, v in it.items() if k != "role"}
        entry["unlocked"] = is_entitled(it, role_ids)
        out.append(entry)
    return out


def file_items_with_lock(items: list[dict], role_ids: list[str]) -> list[dict]:
    """파일형 아이템(프리셋)을 **잠긴 것까지 진열**한다. 파일 자체는 계속 막는다.

    왜 필요한가: entitled_items 로 내리면 안 산 사람 화면에는 그 상품이 **존재하지 않는다**.
    팔고 있는 물건을 아무도 모르는 상태였다. 테마·조준점은 이미 진열형인데 프리셋만 아니었다.

    ⚠️ **잠긴 항목에는 id 를 싣지 않는다.** 두 가지 이유가 모두 중요하다.
      1. id 는 GET /content/file/<id> 의 다운로드 키다. (파일 라우트가 역할을 라이브 재확인하므로
         id 를 알아도 파일은 못 받지만, 굳이 줄 이유가 없다.)
      2. **하위호환 장치다.** 구버전 클라의 parse_content_items 는 id 가 없는 항목을 건너뛴다
         → 아직 업데이트 안 한 구매자에게는 지금과 **완전히 똑같이** 동작하고, 잠긴 항목을
         받으려고 시도(=전부 403)하지도 않는다. id 를 실으면 구버전 클라가 잠긴 프리셋 수만큼
         쓸데없는 요청을 보내고 그때마다 서버가 디스코드에 역할을 물어본다.

    ⚠️ effects 에는 쓰지 마라. 이펙트는 상품이 아니라 프리셋이 쓰는 셰이더 파일(의존성)이다.
       (라이브 실측: Pretty 1상품 = 프리셋 1개 + 이펙트 17개.) 낱개로 진열하면 구매자는
       상품명 대신 AdaptiveSharpen.fx 같은 카드를 17장 보게 된다.

    unlocked 는 **진짜 JSON 불리언**이다(클라는 json_bool 로 읽는다). crosshair_items 와 같다.
    """
    out = []
    for it in items:
        unlocked = is_entitled(it, role_ids)
        entry = {k: v for k, v in it.items() if k not in ("role", "id")}
        if unlocked:
            entry["id"] = it.get("id")
        entry["unlocked"] = unlocked
        out.append(entry)
    return out


# 조준점 항목에서 클라에 실어 보내는 키. 이 목록 밖의 키(role, 메모 등)는 나가지 않는다.
CROSSHAIR_TEXT_KEYS = ("id", "display_name", "author", "tag", "code")


def crosshair_items(items: list[dict], role_ids: list[str]) -> list[dict]:
    """조준점 매니페스트 → /content/me 에 실을 평면 항목 목록.

    계약(docs/superpowers/specs/2026-07-29-sherbet-auto-update-design.md §3.3 과 동일):

    * **중첩 객체 금지.** 클라 파서(source/sherbet_json.hpp)는 중첩을 모르는 평면
      substring 파서다.
    * **unlocked 를 뺀 모든 값은 따옴표로 감싼 문자열.** 따옴표 없는 숫자를 내면
      json_str 이 false 를 반환하고 클라는 오류 없이 그 키를 '없음' 으로 넘긴다 —
      code 가 그렇게 되면 전 항목이 조용히 '사용 불가' 카드가 된다. 그래서 str() 로
      직렬화한다(운영자가 매니페스트에 숫자를 적어도 문자열로 나간다).
    * **잠긴 항목에는 code 를 싣지 않는다.** 조준점 상품의 실체가 코드 한 줄이라
      잠긴 채로 내려보내면 캐시 파일(sherbet.themes)만 열어도 가져갈 수 있다.
      이름·제작자·태그만 남겨 '잠김' 카드로 진열한다.
    """
    out: list[dict] = []
    for it in items:
        entry: dict = {}
        for k in CROSSHAIR_TEXT_KEYS:
            v = it.get(k)
            if v is None:
                continue
            s = str(v).strip()
            if s:
                entry[k] = s
        if not entry.get("id"):
            continue  # 카드 키가 없으면 클라가 진열할 수 없다
        unlocked = is_entitled(it, role_ids)
        if unlocked:
            if not entry.get("code"):
                continue  # 열려 있는데 코드가 없으면 진열할 이유가 없다(우리 쪽 실수)
        else:
            entry.pop("code", None)
        entry["unlocked"] = unlocked  # ⚠️ 진짜 JSON 불리언(클라는 json_bool 로 읽는다)
        out.append(entry)
    return out


def find_item(items: list[dict], item_id: str) -> dict | None:
    """id 가 item_id 인 첫 아이템, 없으면 None."""
    for it in items:
        if it.get("id") == item_id:
            return it
    return None
