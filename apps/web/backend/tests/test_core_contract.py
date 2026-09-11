from __future__ import annotations

import pytest
from pydantic import ValidationError

from app.clients.gwent_core import GwentCoreClient, GwentCoreProtocolError
from app.models.core_contract import CORE_API_VERSION, CounterfactualActionChainTrace, GameState


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
        "match_id": "a" * 32,
        "revision": 7,
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
        "objects": [
            {
                "object_index": 0,
                "entity_id": 123,
                "card_id": 203099,
                "name": "雷吉斯：重生",
                "type": "unit",
                "ability_text": "部署：汲食 1 个敌军单位 3 点。",
                "owner": 0,
                "controller": 0,
                "zone": 3,
                "zone_name": "Melee",
                "row": 0,
                "row_name": "Melee",
                "slot": 0,
                "power": 1,
                "armor": 0,
                "status": [],
            }
        ],
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
    assert state.objects[0].ability_text == "部署：汲食 1 个敌军单位 3 点。"


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


def test_counterfactual_action_chain_requires_one_root_and_structured_children() -> None:
    payload = state_payload()
    action = payload["actions"][0]
    action.update(
        {
            "confidence": 0.6,
            "value": 0.1,
            "ms": 2.0,
            "status": "APPLIED",
            "reward_p0": 0.0,
            "decision_serial": 1,
            "parent_decision_serial": None,
            "role": "root_action",
            "actor_id": 0,
            "source_card_id": -1,
            "target_card_id": -1,
            "source_zone": -1,
            "summary_before": payload["summary"],
            "summary_after": payload["summary"],
        }
    )
    trace = CounterfactualActionChainTrace.model_validate(
        {
            "api_version": CORE_API_VERSION,
            "schema_version": "counterfactual-action-chain-v2",
            "base_match_id": "a" * 32,
            "base_revision": 7,
            "base_state_signature": "a" * 64,
            "controlled_player": 0,
            "boundary": "one_root_action_with_required_choices",
            "root": {
                "decision_serial": 1,
                "kind": action["kind"],
                "card_id": action["card_id"],
                "source_object_index": action["source_object_index"],
            },
            "status": "complete",
            "stopped_reason": "action_chain_resolved",
            "steps": [action],
            "start_summary": payload["summary"],
            "end_summary": payload["summary"],
        }
    )

    assert trace.steps[0].actor_id == 0
    assert trace.steps[0].insert_position == 2
    assert trace.steps[0].role == "root_action"
