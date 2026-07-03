import os
import secrets

import httpx
from fastapi import Depends, FastAPI, Header, HTTPException
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel

from app.config import Settings, get_settings
from app.content import entitled_themes, load_themes
from app.discord_roles import get_member_role_ids, roles_snapshot
from app.oauth import build_authorize_url, exchange_code, get_user_id
from app.store import PendingStore
from app.tokens import issue_token, verify_token

app = FastAPI(title="Sherbet Auth")
store = PendingStore()

# 원격 테마 정의 파일 경로 + mtime 캐시(요청마다 파일 stat, 안 바뀌면 캐시)
THEMES_PATH = os.path.join(os.path.dirname(os.path.dirname(__file__)), "content", "themes.json")
THEMES_CACHE: dict = {}


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
        user_id = await get_user_id(access_token)
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
    token = issue_token(settings, user_id, hwid, roles)
    store.set_result(state, token)
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
    themes = load_themes(THEMES_PATH, THEMES_CACHE)
    return {"themes": entitled_themes(themes, role_ids or [])}


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
    return {"valid": True, "sub": payload["sub"], "roles": roles_snapshot(settings, role_ids)}
