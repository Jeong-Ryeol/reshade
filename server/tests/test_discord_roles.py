import httpx
import pytest
import respx

from app.discord_roles import get_member_role_ids, has_buyer_role, roles_snapshot

API = "https://discord.com/api/v10"


@pytest.mark.asyncio
@respx.mock
async def test_get_member_role_ids_returns_roles(settings):
    route = respx.get(f"{API}/guilds/guild-1/members/u1").mock(
        return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-x"]})
    )
    ids = await get_member_role_ids(settings, "u1")
    assert ids == ["role-buyer", "role-x"]
    assert route.called
    assert route.calls.last.request.headers["Authorization"] == "Bot test-bot-token"


@pytest.mark.asyncio
@respx.mock
async def test_get_member_role_ids_none_when_not_member(settings):
    respx.get(f"{API}/guilds/guild-1/members/u1").mock(
        return_value=httpx.Response(404, json={"message": "Unknown Member"})
    )
    assert await get_member_role_ids(settings, "u1") is None


@pytest.mark.asyncio
@respx.mock
async def test_has_buyer_role_true(settings):
    respx.get(f"{API}/guilds/guild-1/members/u1").mock(
        return_value=httpx.Response(200, json={"roles": ["role-buyer"]})
    )
    assert await has_buyer_role(settings, "u1") is True


@pytest.mark.asyncio
@respx.mock
async def test_has_buyer_role_false_when_missing(settings):
    respx.get(f"{API}/guilds/guild-1/members/u1").mock(
        return_value=httpx.Response(200, json={"roles": ["role-x"]})
    )
    assert await has_buyer_role(settings, "u1") is False


def test_roles_snapshot_maps_buyer(settings):
    assert roles_snapshot(settings, ["role-buyer", "role-x"]) == ["sherbet-buyer", "role-x"]
