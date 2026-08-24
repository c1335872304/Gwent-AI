from __future__ import annotations

import pytest
from pydantic import ValidationError

from app.clients.gwent_core import GwentCoreClient, GwentCoreProtocolError
from app.models.core_contract import CORE_API_VERSION, GameState


def state_payload() -> dict:
    action = {
        "index": 0,
        "kind_id": 11,
        "kind": "choose_insert_position",
        "label": "选择位置 · 2",
        "card_id": -1,
        "source": "-",
        "target": "P0 Melee/Melee @ position 2",
        "hand_slot": -1,
        "stable_hash": "18446744073709551615",
        "source_object_index": -1,
        "target_object_index": -1,
        "target_side": 0,
        "target_zone": 3,
        "target_row": 0,
        "insert_position": 2,
    }
    return {
        "api_version": CORE_API_VERSION,
        "summary": {
            "round": 1,
            "turn": 3,
            "actor": 0,
            "decision_kind": 6,
            "decision": "insert_position",
            "done": False,
            "winner_id": -1,
            "p0": {"score": 10, "hand": 7, "wins": 0, "passed": False},
            "p1": {"score": 8, "hand": 7, "wins": 0, "passed": False},
        },
        "objects": [],
        "row_effects": [
            {"side": 1, "row": 0, "id": "frost", "name": "霜", "duration": 2},
            {"side": 0, "row": 1, "id": "blood_moon", "name": "血月", "duration": 3},
        ],
        "actions": [action],
        "last_human_action": None,
        "last_ai_actions": [],
        "checkpoint": "checkpoint.pt",
        "checkpoint_update": 12,
        "device": "cpu",
        "decks": {"p0": 0, "p1": 1},
        "mode": "manual_test",
    }


def test_game_state_accepts_lossless_uint64_hash_string() -> None:
    state = GameState.model_validate(state_payload())
    assert state.actions[0].stable_hash == "18446744073709551615"
    assert state.actions[0].insert_position == 2


def test_game_state_rejects_missing_structured_action_field() -> None:
    payload = state_payload()
    del payload["actions"][0]["insert_position"]
    with pytest.raises(ValidationError):
        GameState.model_validate(payload)


def test_game_state_rejects_extra_contract_fields() -> None:
    payload = state_payload()
    payload["actions"][0]["legacy_guess"] = True
    with pytest.raises(ValidationError):
        GameState.model_validate(payload)


def test_client_rejects_wrong_core_api_version() -> None:
    payload = state_payload()
    payload["api_version"] = CORE_API_VERSION + 1
    with pytest.raises(GwentCoreProtocolError, match="Unsupported Core API version"):
        GwentCoreClient._validate(GameState, payload)


def test_game_state_rejects_uint64_overflow_hash() -> None:
    payload = state_payload()
    payload["actions"][0]["stable_hash"] = str(2**64)
    with pytest.raises(ValidationError):
        GameState.model_validate(payload)


def test_insert_choice_requires_structured_position_metadata() -> None:
    payload = state_payload()
    payload["actions"][0]["insert_position"] = -1
    with pytest.raises(ValidationError):
        GameState.model_validate(payload)
