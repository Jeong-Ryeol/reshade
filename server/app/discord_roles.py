import httpx

from app.config import Settings

_API = "https://discord.com/api/v10"


async def get_member_role_ids(settings: Settings, user_id: str) -> list[str] | None:
    url = f"{_API}/guilds/{settings.discord_guild_id}/members/{user_id}"
    headers = {"Authorization": f"Bot {settings.discord_bot_token}"}
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get(url, headers=headers)
    if resp.status_code == 404:
        return None
    resp.raise_for_status()
    return list(resp.json().get("roles", []))


async def has_buyer_role(settings: Settings, user_id: str) -> bool:
    role_ids = await get_member_role_ids(settings, user_id)
    if role_ids is None:
        return False
    return settings.role_buyer_id in role_ids


def roles_snapshot(settings: Settings, role_ids: list[str]) -> list[str]:
    mapping = {settings.role_buyer_id: "sherbet-buyer"}
    return [mapping.get(rid, rid) for rid in role_ids]
