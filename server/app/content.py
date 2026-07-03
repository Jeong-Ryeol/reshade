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


def is_entitled(item: dict, role_ids: list[str]) -> bool:
    """item 의 role 이 falsy(무료)이거나 사용자 역할 ID 목록에 포함되면 True."""
    role = item.get("role")
    return (not role) or role in set(role_ids or [])


def entitled_items(items: list[dict], role_ids: list[str]) -> list[dict]:
    """권한 있는 아이템만, role 키를 제거해 반환."""
    return [{k: v for k, v in it.items() if k != "role"}
            for it in items if is_entitled(it, role_ids)]


def find_item(items: list[dict], item_id: str) -> dict | None:
    """id 가 item_id 인 첫 아이템, 없으면 None."""
    for it in items:
        if it.get("id") == item_id:
            return it
    return None
