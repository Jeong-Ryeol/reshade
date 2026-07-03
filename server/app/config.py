from functools import lru_cache

from pydantic import field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    discord_client_id: str
    discord_client_secret: str
    discord_bot_token: str
    discord_guild_id: str
    discord_redirect_uri: str
    session_secret: str
    role_buyer_id: str
    token_ttl_seconds: int = 86400

    @field_validator("session_secret")
    @classmethod
    def _session_secret_min_length(cls, v: str) -> str:
        if len(v) < 16:
            raise ValueError("session_secret must be at least 16 characters")
        return v


@lru_cache
def get_settings() -> Settings:
    return Settings()
