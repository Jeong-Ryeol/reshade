from urllib.parse import parse_qs, urlparse

import httpx
import pytest
import respx

from app.oauth import build_authorize_url, exchange_code, get_user_id

API = "https://discord.com/api/v10"


def test_build_authorize_url(settings):
    url = build_authorize_url(settings, state="nonce123")
    parsed = urlparse(url)
    q = parse_qs(parsed.query)
    assert parsed.netloc == "discord.com"
    assert q["client_id"] == ["test-cid"]
    assert q["state"] == ["nonce123"]
    assert q["response_type"] == ["code"]
    assert q["redirect_uri"] == ["https://test/auth/callback"]
    assert "identify" in q["scope"][0]
    assert "guilds.members.read" in q["scope"][0]


@pytest.mark.asyncio
@respx.mock
async def test_exchange_code(settings):
    route = respx.post(f"{API}/oauth2/token").mock(
        return_value=httpx.Response(200, json={"access_token": "at-xyz", "token_type": "Bearer"})
    )
    token = await exchange_code(settings, code="the-code")
    assert token == "at-xyz"
    body = parse_qs(route.calls.last.request.content.decode())
    assert body["grant_type"] == ["authorization_code"]
    assert body["code"] == ["the-code"]


@pytest.mark.asyncio
@respx.mock
async def test_get_user_id(settings):
    respx.get(f"{API}/users/@me").mock(
        return_value=httpx.Response(200, json={"id": "user-42", "username": "x"})
    )
    assert await get_user_id("at-xyz") == "user-42"
