from urllib.parse import urlencode

import httpx

from app.config import Settings

_API = "https://discord.com/api/v10"
_SCOPE = "identify guilds.members.read"


def build_authorize_url(settings: Settings, state: str) -> str:
    params = {
        "client_id": settings.discord_client_id,
        "response_type": "code",
        "redirect_uri": settings.discord_redirect_uri,
        "scope": _SCOPE,
        "state": state,
        "prompt": "none",
    }
    return f"https://discord.com/oauth2/authorize?{urlencode(params)}"


async def exchange_code(settings: Settings, code: str) -> str:
    data = {
        "client_id": settings.discord_client_id,
        "client_secret": settings.discord_client_secret,
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": settings.discord_redirect_uri,
    }
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.post(f"{_API}/oauth2/token", data=data)
    resp.raise_for_status()
    return resp.json()["access_token"]


async def get_user_id(access_token: str) -> str:
    headers = {"Authorization": f"Bearer {access_token}"}
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get(f"{_API}/users/@me", headers=headers)
    resp.raise_for_status()
    return resp.json()["id"]
