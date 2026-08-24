from __future__ import annotations

import os

import numpy as np
import pytest

from gwent_rl import (
    GLOBAL_FEATURE_NAMES,
    OBJECT_FEATURE_NAMES,
    OPTION_FEATURE_NAMES,
    RlCollector,
    explain_batch_row,
    random_legal_actions,
)


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_collector_batch_shapes_and_random_step():
    rng = np.random.default_rng(7)
    with RlCollector(num_envs=8, max_batch_size=8, base_seed=7, enable_invariants=True) as collector:
        batch = collector.collect()
        assert batch.count == 8
        assert batch.global_features.shape == (8, 34)
        assert batch.object_features.shape == (8, 128, 27)
        assert batch.option_features.shape == (8, 256, 16)
        assert batch.option_mask.shape == (8, 256)
        assert batch.prefix_mask.shape == (8, 16)
        assert batch.prefix_card_ids.shape == (8, 16)
        assert len(GLOBAL_FEATURE_NAMES) == 34
        assert "actor_deck_count" not in GLOBAL_FEATURE_NAMES
        assert "opponent_deck_count" not in GLOBAL_FEATURE_NAMES
        assert GLOBAL_FEATURE_NAMES[-4:] == [
            "actor_melee_frost_duration",
            "actor_ranged_frost_duration",
            "opponent_melee_frost_duration",
            "opponent_ranged_frost_duration",
        ]
        assert len(OBJECT_FEATURE_NAMES) == 27
        assert OBJECT_FEATURE_NAMES[-2:] == ["saved_frost_melee", "saved_frost_ranged"]
        assert len(OPTION_FEATURE_NAMES) == 16
        assert "target_power" in OPTION_FEATURE_NAMES
        assert np.all(batch.object_counts > 0)
        assert batch.option_card_ids.shape == (8, 256)
        assert batch.option_insert_positions.shape == (8, 256)
        assert batch.prefix_insert_positions.shape == (8, 16)
        assert isinstance(explain_batch_row(batch, 0, max_objects=1, max_options=1), str)
        assert np.all(batch.option_counts > 0)
        actions = random_legal_actions(batch, rng)
        results = collector.apply_actions(batch, actions)
        assert results.count == 8
        assert np.all(results.result_codes == 0)
        assert collector.total_steps == 8


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_collector_selects_deck_b_per_player():
    with RlCollector(
        num_envs=1,
        max_batch_size=1,
        base_seed=17,
        shuffle_decks=False,
        player0_deck="b",
        player1_deck="a",
    ) as collector:
        batch = collector.collect()
        visible_ids = set(batch.object_card_ids[0, : batch.object_counts[0]].tolist())
        assert 200055 in visible_ids  # White Frost, deck B leader.
        assert 202185 in visible_ids  # Blood Scent, deck A leader.


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_collector_accepts_weighted_matchup_mapping():
    matchups = {
        "ab": {"player0": "deck_a", "player1": "deck_b", "weight": 1},
        "ba": {"player0": "deck_b", "player1": "deck_a", "weight": 1},
        "aa": {"player0": "deck_a", "player1": "deck_a", "weight": 1},
        "bb": {"player0": "deck_b", "player1": "deck_b", "weight": 1},
    }
    with RlCollector(num_envs=4, max_batch_size=4, matchups=matchups) as collector:
        batch = collector.collect()
        leaders = []
        for row in range(4):
            visible = {
                (int(batch.object_owner_ids[row, col]), int(batch.object_card_ids[row, col]))
                for col in range(int(batch.object_counts[row]))
            }
            leaders.append(visible)
        assert (0, 202185) in leaders[0] and (1, 200055) in leaders[0]
        assert (0, 200055) in leaders[1] and (1, 202185) in leaders[1]
        assert (0, 202185) in leaders[2] and (1, 202185) in leaders[2]
        assert (0, 200055) in leaders[3] and (1, 200055) in leaders[3]


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_dense_reward_mode_can_return_nonzero_rewards():
    rng = np.random.default_rng(48)
    with RlCollector(
        num_envs=8,
        max_batch_size=8,
        base_seed=48,
        reward_mode="dense",
        reward_overrides={"score_delta_weight": 0.25, "damage_weight": 0.05, "boost_weight": 0.02},
    ) as collector:
        saw_nonzero = False
        for _ in range(64):
            batch = collector.collect()
            actions = random_legal_actions(batch, rng)
            results = collector.apply_actions(batch, actions)
            if np.any(results.rewards != 0.0):
                saw_nonzero = True
                break
        assert saw_nonzero

@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_strategic_reward_mode_can_return_round_or_resource_rewards():
    rng = np.random.default_rng(1042)
    with RlCollector(
        num_envs=8,
        max_batch_size=8,
        base_seed=1042,
        reward_mode="strategic",
        reward_overrides={
            "secured_pass_reward": 0.04,
            "secured_overplay_cost": 0.08,
        },
    ) as collector:
        saw_nonzero = False
        for _ in range(256):
            batch = collector.collect()
            actions = random_legal_actions(batch, rng)
            results = collector.apply_actions(batch, actions)
            if np.any(results.rewards != 0.0):
                saw_nonzero = True
                break
        assert saw_nonzero
