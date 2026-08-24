from __future__ import annotations

from pathlib import Path

import pytest
import torch

from gwent_rl import SCHEMA_VERSION
from gwent_rl.policy import CandidatePolicyValueNet
from gwent_rl.training.initialization import inspect_checkpoint, warm_start_policy


REPO_ROOT = Path(__file__).resolve().parents[2]


def _policy() -> CandidatePolicyValueNet:
    return CandidatePolicyValueNet(
        hidden_dim=32,
        num_attention_heads=4,
        attention_layers=1,
        dropout=0.0,
        card_vocab_size=128,
        text_embedding_dim=0,
        freeze_text_embeddings=True,
        use_candidate_object_attention=True,
        use_candidate_self_attention=True,
    )


def test_warm_start_preserves_strategy_weights_and_resets_new_action_rows(tmp_path: Path):
    torch.manual_seed(1)
    source = _policy()
    with torch.no_grad():
        source.decision_embedding.weight.fill_(1.25)
        source.option_kind_embedding.weight.fill_(2.5)
        source.global_encoder[0].weight.fill_(3.75)

    checkpoint = tmp_path / "v2.pt"
    torch.save(
        {
            "model": {
                key: value for key, value in source.state_dict().items()
                if key != "insert_position_embedding.weight"
            },
            "optimizer": {"legacy": True},
            "metadata": {
                "schema_version": 6,
                "action_grammar_version": 2,
                "prefix_semantics": "ordered_full_resolution_prefix",
                "update": 88,
                "total_games": 220000,
            },
        },
        checkpoint,
    )

    torch.manual_seed(2)
    target = _policy()
    fresh_decision = target.decision_embedding.weight[2].detach().clone()
    fresh_mulligan = target.option_kind_embedding.weight[4].detach().clone()
    fresh_keep = target.option_kind_embedding.weight[10].detach().clone()

    report = warm_start_policy(
        checkpoint,
        target,
        expected_source_schema=6,
        expected_source_action_grammar=2,
        reset_decision_embeddings=["mulligan"],
        reset_option_embeddings=["mulligan", "keep_hand"],
    )

    # 旧 Strategy Core 的主体知识应完整迁移。
    assert torch.allclose(target.global_encoder[0].weight, source.global_encoder[0].weight)
    assert torch.allclose(target.decision_embedding.weight[1], source.decision_embedding.weight[1])
    assert torch.allclose(target.option_kind_embedding.weight[3], source.option_kind_embedding.weight[3])

    # 新动作语义对应入口必须保留当前网络的随机初始化，而不是继承旧版本的空白槽位。
    assert torch.allclose(target.decision_embedding.weight[2], fresh_decision)
    assert torch.allclose(target.option_kind_embedding.weight[4], fresh_mulligan)
    assert torch.allclose(target.option_kind_embedding.weight[10], fresh_keep)
    assert not torch.allclose(target.option_kind_embedding.weight[10], source.option_kind_embedding.weight[10])

    assert report.source_action_grammar == 2
    assert report.source_update == 88
    assert report.source_total_games == 220000
    assert report.optimizer_reset is True


def test_repository_pre_mulligan_checkpoint_metadata_is_pinned():
    checkpoint = REPO_ROOT / "artifacts" / "checkpoints" / "pre_mulligan_v2" / "update_000088.pt"
    if not checkpoint.exists():
        pytest.skip("large pinned checkpoint is intentionally not included in source packages")
    metadata = inspect_checkpoint(checkpoint)

    assert int(metadata["schema_version"]) == 6
    assert int(metadata["action_grammar_version"]) == 2
    assert int(metadata["update"]) == 88
    assert int(metadata["total_games"]) == 220000


def test_v7_to_current_migration_preserves_old_columns_and_target_text_table(tmp_path: Path):
    torch.manual_seed(11)
    target = CandidatePolicyValueNet(
        hidden_dim=32,
        num_attention_heads=4,
        attention_layers=1,
        card_vocab_size=48,
        text_embedding_dim=8,
    )
    fresh_global_tail = target.global_encoder[0].weight[:, 30:].detach().clone()
    fresh_object_tail = target.object_feature_encoder[0].weight[:, 24:].detach().clone()
    fresh_text = target.text_embedding.weight.detach().clone()

    source_state = {key: value.detach().clone() for key, value in target.state_dict().items()}
    source_state.pop("insert_position_embedding.weight")
    source_state["global_encoder.0.weight"] = torch.full_like(
        source_state["global_encoder.0.weight"][:, :30], 1.25
    )
    source_state["object_feature_encoder.0.weight"] = torch.full_like(
        source_state["object_feature_encoder.0.weight"][:, :24], 2.5
    )
    source_state["text_embedding.weight"] = torch.full((23, 8), 9.0)
    source_state["global_encoder.0.bias"].fill_(3.75)
    checkpoint = tmp_path / "v7.pt"
    torch.save(
        {
            "model": source_state,
            "metadata": {
                "schema_version": 7,
                "action_grammar_version": 3,
                "prefix_semantics": "ordered_full_resolution_prefix",
                "update": 80,
                "total_games": 200000,
            },
        },
        checkpoint,
    )

    report = warm_start_policy(
        checkpoint,
        target,
        expected_source_schema=7,
        expected_source_action_grammar=3,
    )

    assert torch.allclose(target.global_encoder[0].weight[:, :30], source_state["global_encoder.0.weight"])
    assert torch.allclose(target.global_encoder[0].weight[:, 30:], fresh_global_tail)
    assert torch.allclose(target.object_feature_encoder[0].weight[:, :24], source_state["object_feature_encoder.0.weight"])
    assert torch.allclose(target.object_feature_encoder[0].weight[:, 24:], fresh_object_tail)
    assert torch.allclose(target.text_embedding.weight, fresh_text)
    assert torch.allclose(target.global_encoder[0].bias, source_state["global_encoder.0.bias"])
    assert report.source_schema == 7
    assert report.source_total_games == 200000


def test_warm_start_rejects_checkpoint_sha256_mismatch(tmp_path: Path):
    source = _policy()
    checkpoint = tmp_path / "v2.pt"
    torch.save(
        {
            "model": source.state_dict(),
            "optimizer": {},
            "metadata": {
                "schema_version": 6,
                "action_grammar_version": 2,
                "prefix_semantics": "ordered_full_resolution_prefix",
            },
        },
        checkpoint,
    )
    target = _policy()
    try:
        warm_start_policy(
            checkpoint,
            target,
            expected_source_schema=6,
            expected_source_action_grammar=2,
            expected_sha256="0" * 64,
        )
    except ValueError as exc:
        assert "SHA-256 mismatch" in str(exc)
    else:
        raise AssertionError("expected SHA-256 mismatch to fail warm start")


def test_v11_grammar4_to_v12_grammar5_migration_preserves_strategy_and_initializes_position(tmp_path: Path):
    torch.manual_seed(101)
    source = _policy()
    source_state = {key: value.detach().clone() for key, value in source.state_dict().items()}
    source_state.pop("insert_position_embedding.weight")
    with torch.no_grad():
        source_state["global_encoder.0.bias"].fill_(4.25)
        source_state["decision_embedding.weight"][6].fill_(9.0)
        source_state["option_kind_embedding.weight"][11].fill_(8.0)
        source_state["decision_embedding.weight"][1].fill_(7.0)
        source_state["option_kind_embedding.weight"][3].fill_(6.0)

    checkpoint = tmp_path / "schema11_grammar4.pt"
    torch.save(
        {
            "model": source_state,
            "optimizer": {"old_adam_state": True},
            "metadata": {
                "schema_version": 11,
                "action_grammar_version": 4,
                "prefix_semantics": "ordered_full_resolution_prefix",
                "update": 40,
                "total_games": 100000,
            },
        },
        checkpoint,
    )

    torch.manual_seed(202)
    target = _policy()
    fresh_position = target.insert_position_embedding.weight.detach().clone()
    fresh_decision = target.decision_embedding.weight[6].detach().clone()
    fresh_option = target.option_kind_embedding.weight[11].detach().clone()

    report = warm_start_policy(
        checkpoint,
        target,
        expected_source_schema=11,
        expected_source_action_grammar=4,
    )

    assert torch.allclose(target.global_encoder[0].bias, source_state["global_encoder.0.bias"])
    assert torch.allclose(target.decision_embedding.weight[1], source_state["decision_embedding.weight"][1])
    assert torch.allclose(target.option_kind_embedding.weight[3], source_state["option_kind_embedding.weight"][3])
    assert torch.allclose(target.insert_position_embedding.weight, fresh_position)
    assert torch.allclose(target.decision_embedding.weight[6], fresh_decision)
    assert torch.allclose(target.option_kind_embedding.weight[11], fresh_option)
    assert not torch.allclose(target.decision_embedding.weight[6], source_state["decision_embedding.weight"][6])
    assert not torch.allclose(target.option_kind_embedding.weight[11], source_state["option_kind_embedding.weight"][11])

    assert report.source_schema == 11
    assert report.source_action_grammar == 4
    assert report.target_schema == SCHEMA_VERSION
    assert report.target_action_grammar == 5
    assert report.migration_id == f"schema-11-grammar-4__to__schema-{SCHEMA_VERSION}-grammar-5"
    assert report.target_initialized_keys == ["insert_position_embedding.weight"]
    assert report.reset_decision_embeddings == ["insert_position"]
    assert report.reset_option_embeddings == ["choose_insert_position"]
    assert report.optimizer_reset is True


def test_v11_to_v12_migration_rejects_any_unreviewed_missing_parameter(tmp_path: Path):
    source = _policy()
    source_state = {key: value.detach().clone() for key, value in source.state_dict().items()}
    source_state.pop("insert_position_embedding.weight")
    source_state.pop("global_encoder.0.bias")
    checkpoint = tmp_path / "broken_v11.pt"
    torch.save(
        {
            "model": source_state,
            "metadata": {
                "schema_version": 11,
                "action_grammar_version": 4,
                "prefix_semantics": "ordered_full_resolution_prefix",
            },
        },
        checkpoint,
    )

    with pytest.raises(ValueError, match="reviewed policy"):
        warm_start_policy(
            checkpoint,
            _policy(),
            expected_source_schema=11,
            expected_source_action_grammar=4,
        )
