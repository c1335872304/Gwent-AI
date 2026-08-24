from __future__ import annotations

from types import SimpleNamespace

import pytest

from tools.server.human_vs_ai import HumanVsAiGame


def bare_game(mode: str) -> HumanVsAiGame:
    game = HumanVsAiGame.__new__(HumanVsAiGame)
    game.mode = mode
    game.player0_deck_id = 0
    game.player1_deck_id = 0
    game.last_ai_actions = []
    game.last_human_action = None
    game.seed = 123
    return game


def test_manual_new_game_does_not_auto_step_ai() -> None:
    game = bare_game(HumanVsAiGame.MODE_HUMAN_VS_AI)
    calls: list[str] = []
    game._create_env = lambda *args: calls.append("create")
    game._run_ai_until_human = lambda: calls.append("ai")
    game.public_state = lambda: {"mode": game.mode}

    state = game.new_game(
        seed=99,
        starting_player_id=1,
        player0_deck_id=1,
        player1_deck_id=0,
        mode=HumanVsAiGame.MODE_MANUAL_TEST,
    )

    assert state == {"mode": "manual_test"}
    assert calls == ["create"]


def test_manual_step_accepts_player1_without_ai_takeover() -> None:
    game = bare_game(HumanVsAiGame.MODE_MANUAL_TEST)
    obs = SimpleNamespace(done=0, actor_id=1, option_count=1, option_mask=[1])
    result = SimpleNamespace(action_status=0, reward=[0.25, 0.75])

    game.observation = lambda: obs
    game.describe_option = lambda _obs, i: {"index": i}
    game._step = lambda i: result
    game.action_status_name = lambda _status: "ok"
    game.public_state = lambda: {"mode": game.mode}
    game._run_ai_until_human = lambda: pytest.fail("AI must not run in manual_test mode")
    game.last_ai_actions = [{"old": True}]

    state = game.player_step(0)

    assert state == {"mode": "manual_test"}
    assert game.last_human_action["reward_p1"] == pytest.approx(0.75)
    assert game.last_ai_actions == []


def test_human_vs_ai_still_rejects_manual_player1_step() -> None:
    game = bare_game(HumanVsAiGame.MODE_HUMAN_VS_AI)
    game.observation = lambda: SimpleNamespace(done=0, actor_id=1, option_count=1, option_mask=[1])

    with pytest.raises(ValueError, match="not P0 turn"):
        game.player_step(0)


def test_manual_test_exposes_both_hands_but_ai_mode_hides_p1_hand() -> None:
    from tools.server.human_vs_ai import GwentRlObservation

    game = bare_game(HumanVsAiGame.MODE_MANUAL_TEST)
    game.card_name = lambda card_id: f"Card#{card_id}"
    game.card_types = {}

    obs = GwentRlObservation()
    obs.object_count = 2
    for i, owner in enumerate((0, 1)):
        obs.object_mask[i] = 1
        obs.object_entity_ids[i] = 100 + i
        obs.object_card_ids[i] = 200 + i
        obs.object_owner_ids[i] = owner
        obs.object_controller_ids[i] = owner
        obs.object_zone_ids[i] = 1  # Hand
        obs.object_row_ids[i] = -1
        obs.object_slot_indices[i] = i

    assert [item["owner"] for item in game._public_objects(obs)] == [0, 1]

    game.mode = HumanVsAiGame.MODE_HUMAN_VS_AI
    assert [item["owner"] for item in game._public_objects(obs)] == [0]



def test_local_server_defaults_to_manual_mode_without_checkpoint() -> None:
    from tools.server import human_vs_ai

    assert human_vs_ai.SERVER_MODE == HumanVsAiGame.MODE_MANUAL_TEST
    assert human_vs_ai.SERVER_CHECKPOINT is None


def test_public_row_effects_exposes_weather_per_side_and_row() -> None:
    game = bare_game(HumanVsAiGame.MODE_MANUAL_TEST)
    game.handle = 123

    durations = {
        (1, 0, "frost"): 2,
        (0, 1, "blood_moon"): 3,
    }

    class FakeLib:
        @staticmethod
        def gwent_rl_env_row_effect_duration(_handle, side, row, effect_id):
            return durations.get((side, row, effect_id.decode("utf-8")), 0)

    game.lib = FakeLib()

    assert game._public_row_effects() == [
        {"side": 0, "row": 1, "id": "blood_moon", "name": "血月", "duration": 3},
        {"side": 1, "row": 0, "id": "frost", "name": "霜", "duration": 2},
    ]
