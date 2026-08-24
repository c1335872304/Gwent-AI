from app.config import Settings


def test_default_core_url() -> None:
    settings = Settings(_env_file=None)
    assert settings.core_api_url == "http://127.0.0.1:8008"
