from __future__ import annotations

from app.api.teacher import _safe_packet
from app.models.core_contract import AiAction, GameState


def _state() -> GameState:
    action = AiAction(
        index=4,
        kind_id=2,
        kind="play_card",
        label="play",
        card_id=202889,
        source="Unseen Elder",
        target="-",
        hand_slot=2,
        stable_hash="99",
        source_object_index=3,
        target_object_index=-1,
        target_side=-1,
        target_zone=-1,
        target_row=1,
        insert_position=0,
        confidence=0.68,
        value=0.31,
        ms=2.0,
        status="ok",
    )
    return GameState.model_validate(
        {
            "api_version": 1,
            "match_id": "a" * 32,
            "revision": 7,
            "summary": {
                "round": 1,
                "turn": 7,
                "actor": 0,
                "decision_kind": 1,
                "decision": "turn",
                "done": False,
                "winner_id": -1,
                "p0": {"score": 20, "hand": 7, "wins": 0, "passed": False},
                "p1": {"score": 24, "hand": 6, "wins": 0, "passed": False},
            },
            "objects": [],
            "row_effects": [],
            "actions": [],
            "last_human_action": None,
            "last_ai_actions": [action.model_dump()],
            "checkpoint": "policy.pt",
            "checkpoint_update": 20,
            "device": "cpu",
            "decks": {"p0": 0, "p1": 1},
            "mode": "human_vs_ai",
        }
    )


def test_safe_packet_contains_only_executed_action() -> None:
    game = _state()
    action = game.last_ai_actions[0]
    packet = _safe_packet(action, game, 0)

    assert packet["recommended_option_index"] == 4
    assert len(packet["candidates"]) == 1
    assert packet["candidates"][0]["card_id"] == 202889
    assert packet["candidates"][0]["probability"] == 0.68
    assert packet["evidence"]["hidden_candidates_redacted"] is True
