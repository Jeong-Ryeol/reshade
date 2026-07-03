from fastapi import Depends, FastAPI, HTTPException
from fastapi.responses import HTMLResponse, RedirectResponse
from pydantic import BaseModel

from app.config import Settings, get_settings
from app.discord_roles import get_member_role_ids, has_buyer_role, roles_snapshot
from app.oauth import build_authorize_url, exchange_code, get_user_id
from app.store import PendingStore
from app.tokens import issue_token, verify_token

app = FastAPI(title="Sherbet Auth")
store = PendingStore()


class VerifyBody(BaseModel):
    token: str
    hwid: str


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


@app.get("/auth/discord")
def auth_discord(state: str, hwid: str, settings: Settings = Depends(get_settings)):
    store.put_pending(state, hwid)
    return RedirectResponse(build_authorize_url(settings, state))


@app.get("/auth/callback", response_class=HTMLResponse)
async def auth_callback(code: str, state: str, settings: Settings = Depends(get_settings)):
    hwid = store.get_hwid(state)
    if hwid is None:
        raise HTTPException(status_code=400, detail="unknown_state")
    access_token = await exchange_code(settings, code)
    user_id = await get_user_id(access_token)
    role_ids = await get_member_role_ids(settings, user_id)
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


@app.post("/auth/verify")
async def auth_verify(body: VerifyBody, settings: Settings = Depends(get_settings)) -> dict:
    payload = verify_token(settings, body.token, body.hwid)
    if payload is None:
        return {"valid": False}
    role_ids = await get_member_role_ids(settings, payload["sub"])
    if role_ids is None or settings.role_buyer_id not in role_ids:
        return {"valid": False}
    return {"valid": True, "sub": payload["sub"], "roles": roles_snapshot(settings, role_ids)}
