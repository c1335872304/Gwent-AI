from __future__ import annotations

import ctypes as ct
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
    game.match_id = "a" * 32
    game.revision = 0
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
    assert game.revision == 1


def test_human_vs_ai_still_rejects_manual_player1_step() -> None:
    game = bare_game(HumanVsAiGame.MODE_HUMAN_VS_AI)
    game.observation = lambda: SimpleNamespace(done=0, actor_id=1, option_count=1, option_mask=[1])

    with pytest.raises(ValueError, match="not P0 turn"):
        game.player_step(0)


def test_stale_expected_revision_is_rejected_before_core_observation() -> None:
    from tools.server.human_vs_ai import StaleStateError

    game = bare_game(HumanVsAiGame.MODE_MANUAL_TEST)
    game.observation = lambda: pytest.fail("stale command must not observe or step the environment")

    with pytest.raises(StaleStateError, match="state is stale"):
        game.player_step(0, expected_match_id="b" * 32, expected_revision=0)


def test_manual_test_exposes_both_hands_but_ai_mode_hides_p1_hand() -> None:
    from tools.server.human_vs_ai import GwentRlObservation

    game = bare_game(HumanVsAiGame.MODE_MANUAL_TEST)
    game.card_name = lambda card_id: f"Card#{card_id}"
    game.card_types = {}
    game.card_descriptions = {200: "卡牌能力"}

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

    objects = game._public_objects(obs)
    assert [item["owner"] for item in objects] == [0, 1]
    assert objects[0]["ability_text"] == "卡牌能力"

    game.mode = HumanVsAiGame.MODE_HUMAN_VS_AI
    assert [item["owner"] for item in game._public_objects(obs)] == [0]



def test_local_server_defaults_to_manual_mode_without_checkpoint() -> None:
    from tools.server import human_vs_ai

    assert human_vs_ai.SERVER_MODE == HumanVsAiGame.MODE_MANUAL_TEST
    assert human_vs_ai.SERVER_CHECKPOINT is None


def test_environment_privacy_tracks_the_selected_mode() -> None:
    from tools.server.human_vs_ai import GwentRlConfig

    class FakeLib:
        def __init__(self) -> None:
            self.configs: list[GwentRlConfig] = []

        @staticmethod
        def gwent_rl_default_config() -> GwentRlConfig:
            return GwentRlConfig()

        def gwent_rl_env_create(self, config) -> int:
            source = ct.cast(config, ct.POINTER(GwentRlConfig)).contents
            self.configs.append(GwentRlConfig.from_buffer_copy(source))
            return 123

        @staticmethod
        def gwent_rl_env_destroy(_handle) -> None:
            return None

    game = bare_game(HumanVsAiGame.MODE_HUMAN_VS_AI)
    game.handle = None
    game.lib = FakeLib()

    game._create_env(seed=7, starting_player_id=-1)
    assert game.lib.configs[-1].include_private_info == 0

    game.mode = HumanVsAiGame.MODE_MANUAL_TEST
    game._create_env(seed=8, starting_player_id=-1)
    assert game.lib.configs[-1].include_private_info == 1


def test_new_game_recreates_environment_when_switching_privacy_mode() -> None:
    game = bare_game(HumanVsAiGame.MODE_MANUAL_TEST)
    game.handle = 123
    game.policy = object()
    calls: list[str] = []
    game._create_env = lambda *args: calls.append("create")
    game._run_ai_until_human = lambda: calls.append("ai")
    game.public_state = lambda: {"mode": game.mode}

    state = game.new_game(
        seed=99,
        starting_player_id=-1,
        player0_deck_id=0,
        player1_deck_id=0,
        mode=HumanVsAiGame.MODE_HUMAN_VS_AI,
    )

    assert state == {"mode": "human_vs_ai"}
    assert calls == ["create", "ai"]


def test_torch_batch_routes_by_current_actor_deck() -> None:
    torch = pytest.importorskip("torch")
    from tools.server.human_vs_ai import GwentRlObservation

    game = bare_game(HumanVsAiGame.MODE_HUMAN_VS_AI)
    game.device = torch.device("cpu")
    game.player0_deck_id = 0
    game.player1_deck_id = 1
    observation = GwentRlObservation()
    observation.actor_id = HumanVsAiGame.AI

    tensors = game._obs_to_torch(observation)

    assert tensors["actor_deck_ids"].tolist() == [1]


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


def test_turn_preview_steps_only_a_cloned_human_turn() -> None:
    game = bare_game(HumanVsAiGame.MODE_HUMAN_VS_AI)
    game.handle = 100
    game.policy = object()
    game.metadata = {"update": 7}
    game.match_id = "a" * 32
    game.revision = 4
    game.last_ai_actions = [{"real": "ai-action"}]
    game.last_human_action = {"real": "human-action"}
    live = SimpleNamespace(done=0, actor_id=0, decision_kind=1)
    preview_before = SimpleNamespace(done=0, actor_id=0, decision_kind=1)
    preview_after = SimpleNamespace(done=0, actor_id=1, decision_kind=1)
    preview_state = {"after": False}
    destroyed: list[int] = []

    class FakeLib:
        @staticmethod
        def gwent_rl_env_clone(handle):
            assert handle == 100
            return 200

        @staticmethod
        def gwent_rl_env_destroy(handle):
            destroyed.append(handle)

    game.lib = FakeLib()
    game.observation = lambda: live
    game._observation_for = lambda handle: preview_after if preview_state["after"] else preview_before
    game._preview_state_signature = lambda _obs: "a" * 64
    game._summary = lambda obs: {
        "round": 1,
        "turn": 3,
        "actor": obs.actor_id,
        "decision_kind": 1,
        "decision": "turn",
        "done": bool(obs.done),
        "winner_id": -1,
        "p0": {"score": 0, "hand": 8, "wins": 0, "passed": False},
        "p1": {"score": 0, "hand": 8, "wins": 0, "passed": False},
    }
    game._choose_ai_action = lambda _obs: (0, 0.75, 0.2)
    game.object_card_id = lambda _obs, _index: -1
    game.object_zone_id = lambda _obs, _index: -1
    game.describe_option = lambda _obs, index: {
        "index": index,
        "kind_id": 1,
        "kind": "end_turn",
        "label": "end_turn",
        "card_id": -1,
        "source": "-",
        "target": "-",
        "hand_slot": -1,
        "stable_hash": "9",
        "source_object_index": -1,
        "target_object_index": -1,
        "target_side": -1,
        "target_zone": -1,
        "target_row": -1,
        "insert_position": -1,
    }
    game._step_on_handle = lambda handle, index: (
        preview_state.update(after=True)
        or SimpleNamespace(action_status=0, reward=[0.5, -0.5])
    )
    game.action_status_name = lambda _status: "APPLIED"

    trace = game.preview_current_human_turn(max_steps=4)

    assert trace["status"] == "complete"
    assert trace["schema_version"] == "counterfactual-action-chain-v2"
    assert trace["boundary"] == "one_root_action_with_required_choices"
    assert trace["stopped_reason"] == "action_chain_resolved"
    assert trace["controlled_player"] == 0
    assert trace["base_match_id"] == "a" * 32
    assert trace["base_revision"] == 4
    assert len(trace["steps"]) == 1
    assert trace["steps"][0]["actor_id"] == 0
    assert trace["steps"][0]["role"] == "root_action"
    assert trace["steps"][0]["parent_decision_serial"] is None
    assert trace["root"] == {
        "decision_serial": 1,
        "kind": "end_turn",
        "card_id": -1,
        "source_object_index": -1,
    }
    assert destroyed == [200]
    assert game.handle == 100
    assert game.last_ai_actions == [{"real": "ai-action"}]
    assert game.last_human_action == {"real": "human-action"}


def test_turn_preview_snapshots_choice_source_before_core_step() -> None:
    game = bare_game(HumanVsAiGame.MODE_HUMAN_VS_AI)
    game.handle = 100
    game.policy = object()
    game.metadata = {"update": 20}
    game.match_id = "c" * 32
    game.revision = 8

    states = [
        SimpleNamespace(done=0, actor_id=0, decision_kind=1, phase="before"),
        SimpleNamespace(done=0, actor_id=0, decision_kind=3, phase="before"),
        SimpleNamespace(done=0, actor_id=0, decision_kind=1, phase="after"),
    ]
    progress = {"index": 0}
    destroyed: list[int] = []

    class FakeLib:
        @staticmethod
        def gwent_rl_env_clone(handle):
            assert handle == 100
            return 200

        @staticmethod
        def gwent_rl_env_destroy(handle):
            destroyed.append(handle)

    game.lib = FakeLib()
    game.observation = lambda: states[0]
    game._observation_for = lambda _handle: states[progress["index"]]
    game._preview_state_signature = lambda _obs: "c" * 64
    game._summary = lambda obs: {
        "round": 1,
        "turn": 1,
        "actor": obs.actor_id,
        "decision_kind": obs.decision_kind,
        "decision": "turn" if obs.decision_kind == 1 else "card_target",
        "done": bool(obs.done),
        "winner_id": -1,
        "p0": {"score": 10, "hand": 9, "wins": 0, "passed": False},
        "p1": {"score": 0, "hand": 10, "wins": 0, "passed": False},
    }
    game._choose_ai_action = lambda _obs: (0, 0.68, -0.02)

    def describe(obs, index):
        if obs.decision_kind == 1:
            return {
                "index": index,
                "kind_id": 6,
                "kind": "use_order",
                "label": "use_order · 注魔盔甲",
                "card_id": 202497,
                "source": "注魔盔甲 [E52]",
                "target": "-",
                "source_object_index": 59,
                "target_object_index": -1,
                "target_side": -1,
                "target_zone": -1,
                "target_row": -1,
                "insert_position": -1,
            }
        return {
            "index": index,
            "kind_id": 7,
            "kind": "choose_card",
            "label": "选择卡牌 · 卡塔卡恩",
            "card_id": 132220,
            "source": "注魔盔甲 [E52]",
            "target": "卡塔卡恩 [E5]",
            "source_object_index": 59,
            "target_object_index": 50,
            "target_side": 0,
            "target_zone": 1,
            "target_row": -1,
            "insert_position": -1,
        }

    game.describe_option = describe

    def object_card_id(obs, index):
        if index == 59:
            return 202497 if obs.phase == "before" else 131102
        if index == 50:
            return 132220
        return -1

    game.object_card_id = object_card_id
    game.object_zone_id = lambda _obs, index: 3 if index == 59 else -1

    def step(_handle, _index):
        if progress["index"] == 1:
            # Core may rebuild/reorder its object table after resolving the
            # choice; the old source index now points at Gal.
            states[1].phase = "after"
        progress["index"] += 1
        return SimpleNamespace(action_status=0, reward=[0.0, 0.0])

    game._step_on_handle = step
    game.action_status_name = lambda _status: "APPLIED"

    trace = game.preview_current_human_turn(max_steps=4)

    assert trace["steps"][1]["source_card_id"] == 202497
    assert trace["steps"][1]["target_card_id"] == 132220
    assert trace["steps"][1]["source_zone"] == 3
    assert destroyed == [200]


def test_action_chain_stops_before_a_second_voluntary_turn_action() -> None:
    game = bare_game(HumanVsAiGame.MODE_HUMAN_VS_AI)
    game.handle = 100
    game.policy = object()
    game.metadata = {"update": 7}
    game.match_id = "b" * 32
    game.revision = 2
    states = [
        SimpleNamespace(done=0, actor_id=0, decision_kind=1),  # root play_card
        SimpleNamespace(done=0, actor_id=0, decision_kind=4),  # required choose_row
        SimpleNamespace(done=0, actor_id=0, decision_kind=1),  # next voluntary action
    ]
    progress = {"index": 0}
    destroyed: list[int] = []
    chosen_kinds: list[int] = []

    class FakeLib:
        @staticmethod
        def gwent_rl_env_clone(handle):
            assert handle == 100
            return 200

        @staticmethod
        def gwent_rl_env_destroy(handle):
            destroyed.append(handle)

    game.lib = FakeLib()
    game.observation = lambda: states[0]
    game._observation_for = lambda _handle: states[progress["index"]]
    game._preview_state_signature = lambda _obs: "b" * 64
    game._summary = lambda obs: {
        "round": 1,
        "turn": 3,
        "actor": obs.actor_id,
        "decision_kind": obs.decision_kind,
        "decision": {1: "turn", 4: "row_target"}[obs.decision_kind],
        "done": bool(obs.done),
        "winner_id": -1,
        "p0": {"score": 0, "hand": 8, "wins": 0, "passed": False},
        "p1": {"score": 0, "hand": 8, "wins": 0, "passed": False},
    }

    def choose(obs):
        chosen_kinds.append(obs.decision_kind)
        return 0, 0.75, 0.2

    def describe(obs, index):
        kind = "play_card" if obs.decision_kind == 1 else "choose_row"
        return {
            "index": index,
            "kind_id": 3 if kind == "play_card" else 8,
            "kind": kind,
            "label": kind,
            "card_id": 202185,
            "source": "-",
            "target": "-",
            "hand_slot": -1,
            "stable_hash": "9",
            "source_object_index": -1,
            "target_object_index": -1,
            "target_side": 0 if kind == "choose_row" else -1,
            "target_zone": 3 if kind == "choose_row" else -1,
            "target_row": 0 if kind == "choose_row" else -1,
            "insert_position": -1,
        }

    game._choose_ai_action = choose
    game.describe_option = describe
    game.object_card_id = lambda _obs, _index: -1
    game.object_zone_id = lambda _obs, _index: -1
    game._step_on_handle = lambda _handle, _index: (
        progress.update(index=progress["index"] + 1)
        or SimpleNamespace(action_status=0, reward=[0.5, -0.5])
    )
    game.action_status_name = lambda _status: "APPLIED"

    trace = game.preview_current_human_turn(max_steps=4)

    assert chosen_kinds == [1, 4]
    assert trace["stopped_reason"] == "action_chain_resolved"
    assert [step["role"] for step in trace["steps"]] == ["root_action", "required_choice"]
    assert trace["steps"][1]["parent_decision_serial"] == 1
    assert trace["root"]["kind"] == "play_card"
    assert destroyed == [200]
