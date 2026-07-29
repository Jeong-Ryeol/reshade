import os
import secrets

import httpx
from fastapi import Depends, FastAPI, Header, HTTPException
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse
from pydantic import BaseModel

from app.config import Settings, get_settings
from app.content import entitled_items, find_item, is_entitled, items_with_lock, load_manifest
from app.discord_roles import get_member_role_ids, roles_snapshot
from app.oauth import build_authorize_url, exchange_code, get_user_identity
from app.store import PendingStore
from app.tokens import issue_token, verify_token

app = FastAPI(title="Sherbet Auth")
from app.update import router as update_router  # noqa: E402  (app 생성 뒤 등록)
app.include_router(update_router)
store = PendingStore()

# 원격 콘텐츠 정의 파일 경로 + mtime 캐시(요청마다 파일 stat, 안 바뀌면 캐시)
_CONTENT_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "content")
THEMES_PATH = os.path.join(_CONTENT_DIR, "themes.json")
PRESETS_PATH = os.path.join(_CONTENT_DIR, "presets.json")
EFFECTS_PATH = os.path.join(_CONTENT_DIR, "effects.json")
FEATURES_PATH = os.path.join(_CONTENT_DIR, "features.json")  # 역할로 잠그는 '기능' 목록(예: custompicture)
FILES_DIR = os.path.join(_CONTENT_DIR, "files")
THEMES_CACHE: dict = {}
PRESETS_CACHE: dict = {}
EFFECTS_CACHE: dict = {}
FEATURES_CACHE: dict = {}


class StartBody(BaseModel):
    hwid: str


class VerifyBody(BaseModel):
    token: str
    hwid: str


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


@app.post("/auth/start")
def auth_start(body: StartBody, settings: Settings = Depends(get_settings)) -> dict:
    # state는 서버가 추측 불가능하게 생성 (호출자 입력 신뢰 안 함)
    state = secrets.token_urlsafe(32)
    store.put_pending(state, body.hwid)
    return {"state": state, "authorize_url": build_authorize_url(settings, state)}


@app.get("/auth/callback", response_class=HTMLResponse)
async def auth_callback(
    state: str,
    code: str | None = None,
    error: str | None = None,
    settings: Settings = Depends(get_settings),
):
    hwid = store.get_hwid(state)
    if hwid is None:
        raise HTTPException(status_code=400, detail="unknown_state")
    if code is None:
        # 사용자가 거부했거나 OAuth 오류 (?error=...) — 폴링이 denied로 풀리게
        store.set_denied(state, "oauth_denied")
        return HTMLResponse(
            "<h2>인증 취소</h2><p>로그인이 취소되었습니다. Sherbet에서 다시 시도해주세요.</p>",
            status_code=200,
        )
    try:
        access_token = await exchange_code(settings, code)
        user_id, user_name = await get_user_identity(access_token)
        role_ids = await get_member_role_ids(settings, user_id)
    except (httpx.HTTPError, KeyError, ValueError):
        # 디스코드 업스트림 실패(연결 오류 또는 200인데 응답 본문이 깨진 경우 KeyError/JSONDecodeError 포함)
        # — 상태가 pending으로 갇히지 않게 denied 처리
        store.set_denied(state, "discord_error")
        return HTMLResponse(
            "<h2>인증 오류</h2><p>디스코드 연결에 실패했습니다. 잠시 후 다시 시도해주세요.</p>",
            status_code=200,
        )
    if role_ids is None or settings.role_buyer_id not in role_ids:
        store.set_denied(state, "no_buyer_role")
        return HTMLResponse("<h2>인증 실패</h2><p>구매자 역할이 없습니다. 창을 닫아주세요.</p>", status_code=200)
    roles = roles_snapshot(settings, role_ids)
    token = issue_token(settings, user_id, hwid, roles, name=user_name)
    store.set_result(state, token, user_name)
    return HTMLResponse("<h2>인증 완료</h2><p>Sherbet으로 돌아가세요. 이 창은 닫아도 됩니다.</p>")


@app.get("/auth/poll")
def auth_poll(state: str) -> dict:
    result = store.pop_result(state)
    if result is None:
        raise HTTPException(status_code=404, detail="unknown_state")
    return result


@app.get("/content/me")
async def content_me(
    authorization: str | None = Header(default=None),
    settings: Settings = Depends(get_settings),
):
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="missing_bearer")
    token = authorization[len("Bearer "):]
    payload = verify_token(settings, token, hwid=None)  # 콘텐츠는 신원만 검증
    if payload is None:
        raise HTTPException(status_code=401, detail="invalid_token")
    try:
        role_ids = await get_member_role_ids(settings, payload["sub"])
    except (httpx.HTTPError, KeyError, ValueError):
        return JSONResponse(status_code=503, content={"error": "upstream_unavailable"})
    roles = role_ids or []
    # 테마는 파일 다운로드가 없어 잠긴 것도 '진열'로 내려준다(unlocked=false). 프리셋/fx 는
    # 파일 다운로드를 유발하므로 권한 있는 것만(entitled) 내려 게이트를 유지한다.
    themes = items_with_lock(load_manifest(THEMES_PATH, THEMES_CACHE), roles)
    presets = entitled_items(load_manifest(PRESETS_PATH, PRESETS_CACHE), roles)
    effects = entitled_items(load_manifest(EFFECTS_PATH, EFFECTS_CACHE), roles)
    # 기능 잠금: 권한 있는 기능의 id 만 문자열 배열로 내려준다(예: ["custompicture"]).
    features = [it["id"] for it in load_manifest(FEATURES_PATH, FEATURES_CACHE)
                if is_entitled(it, roles) and it.get("id")]
    return {"themes": themes, "presets": presets, "effects": effects, "features": features}


@app.get("/content/file/{item_id}")
async def content_file(
    item_id: str,
    authorization: str | None = Header(default=None),
    settings: Settings = Depends(get_settings),
):
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="missing_bearer")
    token = authorization[len("Bearer "):]
    payload = verify_token(settings, token, hwid=None)
    if payload is None:
        raise HTTPException(status_code=401, detail="invalid_token")

    # 매니페스트(프리셋+이펙트)에서 id 조회 — 화이트리스트
    items = load_manifest(PRESETS_PATH, PRESETS_CACHE) + load_manifest(EFFECTS_PATH, EFFECTS_CACHE)
    item = find_item(items, item_id)
    if item is None:
        raise HTTPException(status_code=404, detail="unknown_item")

    # per-id 역할 재검증 — 매니페스트 필터만 믿지 않는다(id 추측 방어)
    try:
        role_ids = await get_member_role_ids(settings, payload["sub"])
    except (httpx.HTTPError, KeyError, ValueError):
        return JSONResponse(status_code=503, content={"error": "upstream_unavailable"})
    if not is_entitled(item, role_ids or []):
        raise HTTPException(status_code=403, detail="not_entitled")

    # 경로탈출 방어: files/<id> 고정 구성 + 실경로가 FILES_DIR 안인지 확인
    base = os.path.realpath(FILES_DIR)
    target = os.path.realpath(os.path.join(base, item_id))
    if os.path.commonpath([base, target]) != base or not os.path.isfile(target):
        raise HTTPException(status_code=404, detail="file_missing")
    return FileResponse(target, media_type="application/octet-stream")


@app.post("/auth/verify")
async def auth_verify(body: VerifyBody, settings: Settings = Depends(get_settings)):
    payload = verify_token(settings, body.token, body.hwid)
    if payload is None:
        return {"valid": False}
    try:
        role_ids = await get_member_role_ids(settings, payload["sub"])
    except (httpx.HTTPError, KeyError, ValueError):
        # 서버/디스코드 다운(또는 응답 본문 파싱 실패) — 클라가 "구매자 아님"과 구별해 24h 오프라인
        # 유예를 적용할 수 있게 재시도 가능한 별도 신호(503 + valid:null)를 반환. valid:false를 쓰면 안 됨.
        return JSONResponse(
            status_code=503,
            content={"valid": None, "error": "upstream_unavailable"},
        )
    if role_ids is None or settings.role_buyer_id not in role_ids:
        return {"valid": False}
    return {
        "valid": True,
        "sub": payload["sub"],
        "roles": roles_snapshot(settings, role_ids),
        "name": payload.get("name", ""),  # 공용 DLL 이름 각인
    }
