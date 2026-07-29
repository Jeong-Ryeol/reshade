import os
import re

from fastapi import APIRouter, Query
from fastapi.responses import JSONResponse

from app.content import load_json_object

router = APIRouter()

UPDATE_PATH = os.path.join(
    os.path.dirname(os.path.dirname(__file__)), "content", "update.json")
UPDATE_CACHE: dict = {}

_ARCHS = ("x64", "x86")

# 클라의 parse_version 과 같은 규약: 정확히 3필드, 각 필드는 숫자만, 앞뒤 잔여물 금지,
# 필드값 999999 이하(sherbet_update_core.hpp parse_version 의 오버플로 가드와 같은 상한).
_VERSION_RE = re.compile(r"^[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}$")


def _optional_version(doc: dict, key: str) -> str | None:
    """선택적 버전 필드를 클라가 읽을 수 있는 형태로 정규화한다.

    ⚠️ **없는 값을 빈 문자열로 내보내면 안 된다.** 클라의 parse_manifest 는
    "키가 있는데 파싱 불가" 를 **매니페스트 전체 거부**로 처리한다(§3.3 대칭 강제).
    그래서 min_version 을 안 쓰는 평범한 릴리스에 `"min_version": ""` 을 실으면
    **전 고객이 업데이트를 한 번도 못 받고, 아무 데도 에러가 남지 않는다.**
    - 없음 / 빈 문자열 / 공백뿐  → None(키를 통째로 빼서 '선택 필드 없음' 으로 만든다)
    - x.y.z 형식               → 그 값
    - 그 외(숫자 타입, "1.0", "v1.4.0" …) → ValueError 로 올려 503 으로 **시끄럽게** 실패한다.
      조용히 빼면 강제 업데이트가 소리 없이 무력화되고, 그대로 내보내면 위와 똑같이
      전 고객의 업데이트가 죽는다. 어느 쪽이든 채널은 멈추므로, 운영자가 알아챌 수 있는
      쪽(503 + 로그)을 고른다.
    """
    if key not in doc:
        return None
    raw = doc[key]
    if raw is None:
        return None
    if isinstance(raw, bool) or not isinstance(raw, str):
        raise ValueError(f"{key} 는 문자열이어야 합니다 (실제 {type(raw).__name__})")
    v = raw.strip()
    if not v:
        return None
    if not _VERSION_RE.match(v):
        raise ValueError(f"{key} 가 x.y.z 형식이 아닙니다: {v!r}")
    return v


@router.get("/update/manifest")
def update_manifest(arch: str = Query(...), cur: str | None = None):
    """클라가 시작할 때 무인증으로 묻는다.

    인증을 붙이지 않는 이유(스펙 §3.1): 캐시 토큰이 없으면 시작 verify 자체가 안 돌고,
    무엇보다 '로그인을 망가뜨린 빌드' 는 인증을 요구하는 순간 영원히 고칠 수 없다.
    리포도 릴리스도 공개라 숨길 것도 없다.

    ⚠️ 모든 스칼라를 문자열로 직렬화한다. 클라의 json_string 은 따옴표 있는 문자열만
    읽으므로, 숫자를 그대로 내면 클라가 조용히 '업데이트 없음' 으로 넘어간다.
    allow_downgrade 만 예외 — json_bool_or_null 이 읽는 진짜 불리언이다.

    ⚠️ **값이 없는 선택 키는 빈 문자열이 아니라 '키 자체를 뺀' 형태로 낸다.**
    클라는 '키 없음' 과 '키가 있는데 파싱 불가' 를 전혀 다르게 다룬다 — 전자는 선택
    필드 미사용, 후자는 **매니페스트 전체 거부**다. 지금 그런 키는 min_version 하나다.
    이 규약은 test_update.py 가 진짜 클라 파서(tools/sherbet_manifest_check.cpp)로
    왕복시켜 못 박는다.
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

    try:
        min_version = _optional_version(doc, "min_version")
    except ValueError as e:
        # 조용히 빠지지 않는다 — 이 매니페스트로는 아무도 업데이트를 못 받으므로
        # 운영자가 배포 직후 curl 한 번으로 알아챌 수 있어야 한다.
        return JSONResponse(status_code=503,
                            content={"error": "bad_min_version", "detail": str(e)})

    # cur 은 집계용으로만 받는다 — 저장하지 않는다(새 PII 저장소를 만들지 않는다).
    #
    # 빈 문자열을 내보내도 되는 키는 **표시 전용인 notice/notes 둘뿐이다.**
    #   schema/arch     — 항상 값이 있다(기본값 1 / 검증된 쿼리 파라미터)
    #   version/sha256  — 비면 클라가 매니페스트를 거부한다. 그리고 그게 **옳다**:
    #   url/size          이 넷 중 하나라도 없으면 설치할 대상 자체가 없다.
    #   min_version     — 유일한 '없어도 정상' 인 키라 값이 없으면 **키를 뺀다**(위 함수 참고).
    #   allow_downgrade — 문자열이 아니라 진짜 불리언(클라 json_bool_or_null 이 읽는다).
    body = {
        "schema": str(doc.get("schema", 1)),
        "arch": arch,
        "version": str(doc.get("version", "")),
        "allow_downgrade": bool(doc.get("allow_downgrade", False)),
        "size": str(slot.get("size", "")),
        "sha256": str(slot.get("sha256", "")),
        "url": str(slot.get("url", "")),
        "notice": str(doc.get("notice", "")),
        "notes": str(doc.get("notes", "")),
    }
    if min_version is not None:
        body["min_version"] = min_version
    return body
