from gwent_rl.train_ppo import (
    _joint_promotion_allowed,
    _normalize_deck_id,
    _shared_best_kind,
)


def test_shared_best_scope_routes_to_separate_files():
    assert _shared_best_kind("a_head_only") == "a"
    assert _shared_best_kind("b_head_only") == "b"
    assert _shared_best_kind("joint") == "joint"


def test_deck_id_normalization_accepts_project_aliases():
    assert _normalize_deck_id("a") == "a"
    assert _normalize_deck_id("deck_a") == "a"
    assert _normalize_deck_id("B") == "b"
    assert _normalize_deck_id("deck_b") == "b"


def test_joint_best_requires_joint_gain_and_both_decks_to_hold():
    assert _joint_promotion_allowed(
        joint_score=0.56,
        a_score=0.52,
        b_score=0.50,
        illegal=0,
        promote_threshold=0.55,
        min_deck_score=0.40,
    )
    assert not _joint_promotion_allowed(
        joint_score=0.56,
        a_score=0.60,
        b_score=0.39,
        illegal=0,
        promote_threshold=0.55,
        min_deck_score=0.40,
    )
    assert not _joint_promotion_allowed(
        joint_score=0.54,
        a_score=0.52,
        b_score=0.52,
        illegal=0,
        promote_threshold=0.55,
        min_deck_score=0.40,
    )
    assert not _joint_promotion_allowed(
        joint_score=0.60,
        a_score=0.55,
        b_score=0.55,
        illegal=1,
        promote_threshold=0.55,
        min_deck_score=0.40,
    )
