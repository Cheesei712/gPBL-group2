from functools import lru_cache
from typing import Literal

from pydantic import SecretStr
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    gemini_api_key: SecretStr = SecretStr("")
    device_api_key: SecretStr
    gemini_model: str = "gemini-3.7-flash"
    # Safe default: local rules do not consume Gemini tokens.
    analysis_mode: Literal["gemini", "rules"] = "rules"
    log_level: str = "INFO"


@lru_cache
def get_settings() -> Settings:
    return Settings()  # type: ignore[call-arg]
