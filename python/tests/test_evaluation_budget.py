from __future__ import annotations

import pytest

from gwent_rl.eval_matchup import allocate_weighted_games


def test_ab_evaluation_budget_is_exact_2000_games():
    allocation = allocate_weighted_games(
        2000,
        [("ab", 35), ("ba", 35), ("aa", 15), ("bb", 15)],
    )
    assert allocation == {"ab": 700, "ba": 700, "aa": 300, "bb": 300}
    assert sum(allocation.values()) == 2000


def test_weighted_game_allocator_preserves_exact_total_with_remainder():
    allocation = allocate_weighted_games(11, [("x", 2), ("y", 1), ("z", 1)])
    assert allocation == {"x": 5, "y": 3, "z": 3}
    assert sum(allocation.values()) == 11


def test_weighted_game_allocator_rejects_budget_smaller_than_matchup_count():
    with pytest.raises(ValueError, match="too small"):
        allocate_weighted_games(2, [("a", 1), ("b", 1), ("c", 1)])
