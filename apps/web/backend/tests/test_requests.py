from __future__ import annotations

import pytest
from pydantic import ValidationError

from app.api.game import NewGameRequest, StepRequest


def test_new_game_does_not_duplicate_core_deck_catalog_limit() -> None:
    req = NewGameRequest(player0_deck_id=99, player1_deck_id=42)
    assert (req.player0_deck_id, req.player1_deck_id) == (99, 42)


def test_step_rejects_negative_option_index() -> None:
    with pytest.raises(ValidationError):
        StepRequest(option_index=-1)


def test_new_game_accepts_manual_test_mode() -> None:
    req = NewGameRequest(mode="manual_test")
    assert req.mode == "manual_test"


def test_new_game_rejects_unknown_mode() -> None:
    with pytest.raises(ValidationError):
        NewGameRequest(mode="anything_else")


@pytest.mark.asyncio
async def test_core_client_forwards_manual_test_mode(monkeypatch) -> None:
    from app.clients.gwent_core import GwentCoreClient
    from app.models.core_contract import CORE_API_VERSION

    client = GwentCoreClient("http://core.invalid")
    captured = {}

    async def fake_request(method, path, **kwargs):
        captured.update({"method": method, "path": path, "json": kwargs.get("json")})
        return {
            "api_version": CORE_API_VERSION,
            "summary": {
                "round": 1,
                "turn": 0,
                "actor": 1,
                "decision_kind": 1,
                "decision": "mulligan",
                "done": False,
                "winner_id": -1,
                "p0": {"score": 0, "hand": 10, "wins": 0, "passed": False},
                "p1": {"score": 0, "hand": 10, "wins": 0, "passed": False},
            },
            "objects": [],
            "row_effects": [],
            "actions": [],
            "last_human_action": None,
            "last_ai_actions": [],
            "checkpoint": "checkpoint.pt",
            "checkpoint_update": 1,
            "device": "cpu",
            "decks": {"p0": 0, "p1": 1},
            "mode": "manual_test",
        }

    monkeypatch.setattr(client, "_request_json", fake_request)
    state = await client.new_game(123, -1, 0, 1, mode="manual_test")

    assert state.mode == "manual_test"
    assert captured == {
        "method": "POST",
        "path": "/new",
        "json": {
            "seed": 123,
            "starting_player_id": -1,
            "player0_deck_id": 0,
            "player1_deck_id": 1,
            "mode": "manual_test",
        },
    }


@pytest.mark.asyncio
async def test_core_client_disables_environment_proxy(monkeypatch) -> None:
    import app.clients.gwent_core as core_module

    captured = {}

    class DummyAsyncClient:
        def __init__(self, **kwargs):
            captured.update(kwargs)

        async def aclose(self):
            return None

    monkeypatch.setattr(core_module.httpx, "AsyncClient", DummyAsyncClient)
    client = core_module.GwentCoreClient("http://127.0.0.1:8008")
    await client.start()

    assert captured["base_url"] == "http://127.0.0.1:8008"
    assert captured["trust_env"] is False
