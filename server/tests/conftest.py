import pytest

from app.config import Settings


@pytest.fixture
def settings() -> Settings:
    return Settings(
        discord_client_id="test-cid",
        discord_client_secret="test-secret",
        discord_bot_token="test-bot-token",
        discord_guild_id="guild-1",
        discord_redirect_uri="https://test/auth/callback",
        session_secret="unit-test-session-secret",
        role_buyer_id="role-buyer",
        token_ttl_seconds=86400,
    )
