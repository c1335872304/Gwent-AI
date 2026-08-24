from functools import lru_cache
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    app_name: str = "Gwent App API"
    core_api_url: str = "http://127.0.0.1:8008"
    request_timeout_s: float = 15.0
    teacher_api_url: str = "http://127.0.0.1:8020"
    teacher_timeout_s: float = 8.0
    cors_origins: str = "http://127.0.0.1:5173,http://localhost:5173"

    model_config = SettingsConfigDict(
        env_file=".env",
        env_prefix="GWENT_APP_",
        extra="ignore",
    )

    @property
    def cors_origin_list(self) -> list[str]:
        return [item.strip() for item in self.cors_origins.split(",") if item.strip()]


@lru_cache
def get_settings() -> Settings:
    return Settings()
