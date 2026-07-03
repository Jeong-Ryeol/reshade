from functools import lru_cache

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


@lru_cache
def get_settings() -> Settings:
    return Settings()
