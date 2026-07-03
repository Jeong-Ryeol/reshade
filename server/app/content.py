import json
import os


def load_themes(path: str, mtime_cache: dict) -> list[dict]:
    """themes.json(배열)을 읽는다. mtime_cache 로 파일이 안 바뀌면 캐시 반환.
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


def entitled_themes(themes: list[dict], role_ids: list[str]) -> list[dict]:
    """role 이 falsy(무료)이거나 사용자 역할 ID 목록에 포함된 테마만, role 키를 제거해 반환."""
    role_set = set(role_ids or [])
    out = []
    for t in themes:
        role = t.get("role")
        if not role or role in role_set:
            out.append({k: v for k, v in t.items() if k != "role"})
    return out
