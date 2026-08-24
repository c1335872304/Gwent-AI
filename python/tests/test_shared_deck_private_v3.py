from __future__ import annotations

from pathlib import Path

import torch

from gwent_rl import ACTION_GRAMMAR_VERSION, SCHEMA_VERSION
from gwent_rl.experiment import policy_from_checkpoint
from gwent_rl.policy import CandidatePolicyValueNet, SharedDeckPrivatePolicyValueNet
from gwent_rl.shared_ab_v3 import build_v3_checkpoint


def _batch(deck_ids=(0, 1), *, hidden_objects: int = 3, options: int = 4) -> dict[str, torch.Tensor]:
    b = len(deck_ids)
    n = hidden_objects
    k = options
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


def test_v3_routes_a_and_b_to_independent_private_paths():
    torch.manual_seed(1)
    model = SharedDeckPrivatePolicyValueNet(hidden_dim=16, num_attention_heads=4, text_embedding_dim=0)
    model.eval()
    with torch.no_grad():
        model.logit_heads["a"][-1].weight.zero_()
        model.logit_heads["a"][-1].bias.fill_(2.0)
        model.logit_heads["b"][-1].weight.zero_()
        model.logit_heads["b"][-1].bias.fill_(-3.0)
        model.value_heads["a"][-1].weight.zero_()
        model.value_heads["a"][-1].bias.fill_(0.25)
        model.value_heads["b"][-1].weight.zero_()
        model.value_heads["b"][-1].bias.fill_(-0.75)
    out = model(_batch((0, 1)))
    assert torch.allclose(out.logits[0], torch.full_like(out.logits[0], 2.0))
    assert torch.allclose(out.logits[1], torch.full_like(out.logits[1], -3.0))
    assert torch.allclose(out.values, torch.tensor([0.25, -0.75]))


def test_v3_production_active_path_is_approximately_half_private():
    model = SharedDeckPrivatePolicyValueNet(
        hidden_dim=128,
        num_attention_heads=4,
        text_embedding_dim=1024,
        freeze_text_embeddings=True,
    )
    report = model.parameter_partition_report(trainable_only=True)
    assert 0.49 <= report["a_private_fraction"] <= 0.51
    assert 0.49 <= report["b_private_fraction"] <= 0.51
    assert report["a_private"] == report["b_private"]


def test_v3_private_scope_freezes_shared_and_other_deck():
    model = SharedDeckPrivatePolicyValueNet(hidden_dim=16, num_attention_heads=4, text_embedding_dim=0)
    model.set_trainable_scope("a_private_only")
    assert all(p.requires_grad for p in model.private_parameters("a"))
    assert not any(p.requires_grad for p in model.private_parameters("b"))
    assert not any(p.requires_grad for p in model.shared_parameters())


def _legacy_checkpoint(path: Path, *, bias: float, update: int) -> CandidatePolicyValueNet:
    policy = CandidatePolicyValueNet(hidden_dim=16, num_attention_heads=4, text_embedding_dim=0)
    with torch.no_grad():
        policy.logit_head[-1].bias.fill_(bias)
        policy.value_head[-1].bias.fill_(bias / 10.0)
        policy.global_encoder[0].weight.fill_(bias / 100.0)
        policy.card_embedding.weight.fill_(bias / 1000.0)
    metadata = {
        "schema_version": SCHEMA_VERSION,
        "action_grammar_version": ACTION_GRAMMAR_VERSION,
        "prefix_semantics": "ordered_full_resolution_prefix",
        "update": update,
        "total_decisions": 123,
        "training_config": {
            "model": {
                "architecture": "single_head",
                "hidden_dim": 16,
                "num_attention_heads": 4,
                "attention_layers": 1,
                "text_embedding_dim": 0,
            }
        },
        "model_manifest": policy.model_manifest(),
    }
    torch.save({"model": policy.state_dict(), "metadata": metadata}, path)
    return policy


def test_v3_builder_keeps_private_teachers_and_means_shared_weights(tmp_path):
    a_path = tmp_path / "a.pt"
    b_path = tmp_path / "b.pt"
    a = _legacy_checkpoint(a_path, bias=2.0, update=14)
    b = _legacy_checkpoint(b_path, bias=6.0, update=194)
    out = build_v3_checkpoint(a_path, b_path, tmp_path / "v3.pt", shared_source="mean")
    restored, metadata = policy_from_checkpoint(out)
    assert isinstance(restored, SharedDeckPrivatePolicyValueNet)
    assert torch.equal(restored.logit_heads["a"][-1].bias, a.logit_head[-1].bias)
    assert torch.equal(restored.logit_heads["b"][-1].bias, b.logit_head[-1].bias)
    assert torch.equal(restored.global_encoders["a"][0].weight, a.global_encoder[0].weight)
    assert torch.equal(restored.global_encoders["b"][0].weight, b.global_encoder[0].weight)
    expected_shared = (a.card_embedding.weight + b.card_embedding.weight) * 0.5
    assert torch.allclose(restored.card_embedding.weight, expected_shared)
    # Imported older teachers have identity residual adapters.
    assert torch.count_nonzero(restored.state_adapters["a"][-1].weight) == 0
    assert torch.count_nonzero(restored.candidate_adapters["b"][-1].weight) == 0
    assert metadata["model_manifest"]["architecture"] == "shared_deck_private_v3"
