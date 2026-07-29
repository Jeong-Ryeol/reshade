import os

from fastapi import APIRouter, Query
from fastapi.responses import JSONResponse

from app.content import load_json_object

router = APIRouter()

UPDATE_PATH = os.path.join(
    os.path.dirname(os.path.dirname(__file__)), "content", "update.json")
UPDATE_CACHE: dict = {}

_ARCHS = ("x64", "x86")


@router.get("/update/manifest")
def update_manifest(arch: str = Query(...), cur: str | None = None):
    """클라가 시작할 때 무인증으로 묻는다.

    인증을 붙이지 않는 이유(스펙 §3.1): 캐시 토큰이 없으면 시작 verify 자체가 안 돌고,
    무엇보다 '로그인을 망가뜨린 빌드' 는 인증을 요구하는 순간 영원히 고칠 수 없다.
    리포도 릴리스도 공개라 숨길 것도 없다.

    ⚠️ 모든 스칼라를 문자열로 직렬화한다. 클라의 json_string 은 따옴표 있는 문자열만
    읽으므로, 숫자를 그대로 내면 클라가 조용히 '업데이트 없음' 으로 넘어간다.
    allow_downgrade 만 예외 — json_bool_or_null 이 읽는 진짜 불리언이다.
    """
    if arch not in _ARCHS:
        return JSONResponse(status_code=400, content={"error": "bad_arch"})

    doc = load_json_object(UPDATE_PATH, UPDATE_CACHE)
    if not doc:
        return JSONResponse(status_code=503, content={"error": "no_manifest"})

    builds = doc.get("builds")
    if not isinstance(builds, dict):
        return JSONResponse(status_code=503, content={"error": "no_builds"})
    slot = builds.get(arch)
    if not isinstance(slot, dict):
        return JSONResponse(status_code=503, content={"error": "no_build_for_arch"})

    # cur 은 집계용으로만 받는다 — 저장하지 않는다(새 PII 저장소를 만들지 않는다).
    return {
        "schema": str(doc.get("schema", 1)),
        "arch": arch,
        "version": str(doc.get("version", "")),
        "min_version": str(doc.get("min_version", "")),
        "allow_downgrade": bool(doc.get("allow_downgrade", False)),
        "size": str(slot.get("size", "")),
        "sha256": str(slot.get("sha256", "")),
        "url": str(slot.get("url", "")),
        "notice": str(doc.get("notice", "")),
        "notes": str(doc.get("notes", "")),
    }
