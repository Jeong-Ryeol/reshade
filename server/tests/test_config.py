import pytest
from pydantic import ValidationError

from app.config import Settings


def _kwargs(**over):
    base = dict(
        discord_client_id="cid",
        discord_client_secret="csecret",
        discord_bot_token="btoken",
        discord_guild_id="123",
        discord_redirect_uri="https://x/callback",
        session_secret="sixteen-chars-min",
        role_buyer_id="456",
    )
    base.update(over)
    return base


def test_settings_reads_fields_and_defaults():
    s = Settings(**_kwargs())
    assert s.discord_client_id == "cid"
    assert s.role_buyer_id == "456"
    assert s.token_ttl_seconds == 86400  # default


def test_short_session_secret_rejected():
    with pytest.raises(ValidationError):
        Settings(**_kwargs(session_secret="short"))
