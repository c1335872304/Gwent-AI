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
            "match_id": "a" * 32,
            "revision": 0,
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


@pytest.mark.asyncio
async def test_core_client_requests_read_only_current_turn_preview(monkeypatch) -> None:
    from app.clients.gwent_core import GwentCoreClient

    client = GwentCoreClient("http://core.invalid")
    captured = {}
    summary = {
        "round": 1,
        "turn": 3,
        "actor": 0,
        "decision_kind": 6,
        "decision": "insert_position",
        "done": False,
        "winner_id": -1,
        "p0": {"score": 10, "hand": 7, "wins": 0, "passed": False},
        "p1": {"score": 8, "hand": 7, "wins": 0, "passed": False},
    }
    step = {
        "index": 0,
        "kind_id": 11,
        "kind": "choose_insert_position",
        "label": "选择位置 · 2",
        "card_id": -1,
        "source": "-",
        "target": "P0 Melee/Melee @ position 2",
        "hand_slot": -1,
        "stable_hash": "9",
        "source_object_index": -1,
        "target_object_index": -1,
        "target_side": 0,
        "target_zone": 3,
        "target_row": 0,
        "insert_position": 2,
        "confidence": 0.6,
        "value": 0.1,
        "ms": 1.0,
        "status": "APPLIED",
        "reward_p0": 0.0,
        "decision_serial": 1,
        "parent_decision_serial": None,
        "role": "root_action",
        "actor_id": 0,
        "source_card_id": -1,
        "target_card_id": -1,
        "source_zone": -1,
        "summary_before": summary,
        "summary_after": summary,
    }

    async def fake_request(method, path, **kwargs):
        captured.update({"method": method, "path": path, "json": kwargs.get("json")})
        return {
            "api_version": 1,
            "schema_version": "counterfactual-action-chain-v2",
            "base_match_id": "a" * 32,
            "base_revision": 3,
            "base_state_signature": "a" * 64,
            "controlled_player": 0,
            "boundary": "one_root_action_with_required_choices",
            "root": {
                "decision_serial": 1,
                "kind": step["kind"],
                "card_id": step["card_id"],
                "source_object_index": step["source_object_index"],
            },
            "status": "complete",
            "stopped_reason": "action_chain_resolved",
            "steps": [step],
            "start_summary": summary,
            "end_summary": summary,
        }

    monkeypatch.setattr(client, "_request_json", fake_request)
    trace = await client.preview_current_human_turn("a" * 32, 3)

    assert trace.steps[0].decision_serial == 1
    assert captured == {
        "method": "POST",
        "path": "/preview/current-human-turn",
        "json": {"match_id": "a" * 32, "expected_revision": 3},
    }
