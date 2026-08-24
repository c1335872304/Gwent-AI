from __future__ import annotations

from pathlib import Path

import torch

from gwent_rl import ACTION_GRAMMAR_VERSION, SCHEMA_VERSION
from gwent_rl.collector import RlCollector
from gwent_rl.experiment import policy_from_checkpoint
from gwent_rl.policy import CandidatePolicyValueNet, SharedDeckHeadsPolicyValueNet
from gwent_rl.shared_ab import build_shared_ab_checkpoint


def _batch(deck_ids=(0, 1)) -> dict[str, torch.Tensor]:
    b = len(deck_ids)
    n = 3
    k = 4
    p = 2
    return {
        "global_features": torch.zeros((b, 34), dtype=torch.float32),
        "decision_kinds": torch.ones((b,), dtype=torch.long),
        "source_object_indices": torch.full((b,), -1, dtype=torch.long),
        "object_features": torch.zeros((b, n, 27), dtype=torch.float32),
        "object_mask": torch.ones((b, n), dtype=torch.bool),
        "object_card_ids": torch.tensor([[1, 2, 3]] * b, dtype=torch.long),
        "object_zone_ids": torch.zeros((b, n), dtype=torch.long),
        "object_row_ids": torch.zeros((b, n), dtype=torch.long),
        "object_owner_ids": torch.zeros((b, n), dtype=torch.long),
        "object_controller_ids": torch.zeros((b, n), dtype=torch.long),
        "object_slot_indices": torch.arange(n).view(1, n).repeat(b, 1),
        "option_features": torch.zeros((b, k, 16), dtype=torch.float32),
        "option_mask": torch.ones((b, k), dtype=torch.bool),
        "option_kind_ids": torch.ones((b, k), dtype=torch.long),
        "option_card_ids": torch.tensor([[10, 11, 12, 13]] * b, dtype=torch.long),
        "option_target_row_ids": torch.zeros((b, k), dtype=torch.long),
        "option_insert_positions": torch.zeros((b, k), dtype=torch.long),
        "option_hand_slot_indices": torch.zeros((b, k), dtype=torch.long),
        "option_source_object_indices": torch.full((b, k), -1, dtype=torch.long),
        "option_target_object_indices": torch.full((b, k), -1, dtype=torch.long),
        "prefix_mask": torch.zeros((b, p), dtype=torch.bool),
        "prefix_kind_ids": torch.zeros((b, p), dtype=torch.long),
        "prefix_row_ids": torch.full((b, p), -1, dtype=torch.long),
        "prefix_insert_positions": torch.full((b, p), -1, dtype=torch.long),
        "prefix_source_object_indices": torch.full((b, p), -1, dtype=torch.long),
        "prefix_target_object_indices": torch.full((b, p), -1, dtype=torch.long),
        "prefix_card_ids": torch.full((b, p), -1, dtype=torch.long),
        "actor_deck_ids": torch.tensor(deck_ids, dtype=torch.long),
    }


def test_shared_model_routes_rows_to_deck_heads():
    torch.manual_seed(1)
    policy = SharedDeckHeadsPolicyValueNet(hidden_dim=16, num_attention_heads=4, text_embedding_dim=0)
    policy.eval()
    # Force final policy/value biases to make head routing observable.
    with torch.no_grad():
        policy.logit_heads["a"][-1].weight.zero_()
        policy.logit_heads["a"][-1].bias.fill_(2.0)
        policy.logit_heads["b"][-1].weight.zero_()
        policy.logit_heads["b"][-1].bias.fill_(-3.0)
        policy.value_heads["a"][-1].weight.zero_()
        policy.value_heads["a"][-1].bias.fill_(0.25)
        policy.value_heads["b"][-1].weight.zero_()
        policy.value_heads["b"][-1].bias.fill_(-0.75)
    out = policy(_batch((0, 1)))
    assert torch.allclose(out.logits[0], torch.full_like(out.logits[0], 2.0))
    assert torch.allclose(out.logits[1], torch.full_like(out.logits[1], -3.0))
    assert torch.allclose(out.values, torch.tensor([0.25, -0.75]))


def test_shared_model_head_only_scope_freezes_backbone_and_other_head():
    policy = SharedDeckHeadsPolicyValueNet(hidden_dim=16, num_attention_heads=4)
    policy.set_trainable_scope("b_head_only")
    assert all(p.requires_grad for p in policy.logit_heads["b"].parameters())
    assert all(p.requires_grad for p in policy.value_heads["b"].parameters())
    assert not any(p.requires_grad for p in policy.logit_heads["a"].parameters())
    assert not any(p.requires_grad for p in policy.value_heads["a"].parameters())
    assert not any(p.requires_grad for p in policy.backbone_parameters())


def _legacy_checkpoint(path: Path, *, bias: float, update: int) -> CandidatePolicyValueNet:
    policy = CandidatePolicyValueNet(hidden_dim=16, num_attention_heads=4, text_embedding_dim=0)
    with torch.no_grad():
        policy.logit_head[-1].bias.fill_(bias)
        policy.value_head[-1].bias.fill_(bias / 10.0)
        policy.card_embedding.weight.fill_(bias / 100.0)
    metadata = {
        "schema_version": SCHEMA_VERSION,
        "action_grammar_version": ACTION_GRAMMAR_VERSION,
        "prefix_semantics": "ordered_full_resolution_prefix",
        "update": update,
        "total_decisions": 123,
        "training_config": {"model": {"hidden_dim": 16, "num_attention_heads": 4, "text_embedding_dim": 0}},
        "model_manifest": policy.model_manifest(),
    }
    torch.save({"model": policy.state_dict(), "metadata": metadata}, path)
    return policy


def test_two_legacy_checkpoints_migrate_to_shared_ab(tmp_path):
    a_path = tmp_path / "a.pt"
    b_path = tmp_path / "b.pt"
    a = _legacy_checkpoint(a_path, bias=2.0, update=14)
    b = _legacy_checkpoint(b_path, bias=7.0, update=194)
    out_path = build_shared_ab_checkpoint(a_path, b_path, tmp_path / "ab.pt", backbone="b")
    restored, meta = policy_from_checkpoint(out_path)
    assert isinstance(restored, SharedDeckHeadsPolicyValueNet)
    # Shared backbone came from B.
    assert torch.equal(restored.card_embedding.weight, b.card_embedding.weight)
    # Each final head came from its own teacher.
    assert torch.equal(restored.logit_heads["a"][-1].bias, a.logit_head[-1].bias)
    assert torch.equal(restored.logit_heads["b"][-1].bias, b.logit_head[-1].bias)
    assert meta["shared_ab_initialization"]["deck_a_source_update"] == 14
    assert meta["shared_ab_initialization"]["deck_b_source_update"] == 194
    assert meta["model_manifest"]["architecture"] == "shared_deck_heads_v1"


def test_python_matchup_router_matches_weighted_schedule():
    collector = RlCollector.__new__(RlCollector)
    collector._default_decks = (0, 0)
    collector._matchup_cycle = [(0, 1), (0, 1), (1, 0)]
    collector._env_decks = {0: collector._select_matchup_py(0), 1: collector._select_matchup_py(1), 2: collector._select_matchup_py(2)}
    ids = collector.deck_ids_for_rows(
        torch.tensor([0, 1, 2]).numpy(),
        torch.tensor([0, 1, 0]).numpy(),
    )
    assert ids.tolist() == [0, 1, 1]


def test_rollout_training_keys_preserve_shared_head_routing_metadata():
    from gwent_rl.rollout_buffer import _TRAINING_OBS_KEYS

    assert "actor_deck_ids" in _TRAINING_OBS_KEYS
    assert "opponent_deck_ids" in _TRAINING_OBS_KEYS
