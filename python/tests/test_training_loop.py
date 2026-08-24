from __future__ import annotations

import os
import json

import pytest
import torch

from gwent_rl import ACTION_GRAMMAR_VERSION, SCHEMA_VERSION, CandidatePolicyValueNet, PPOConfig, RlCollector, RolloutRunner, ppo_update
from gwent_rl.benchmark_collector import run_benchmark
from gwent_rl.card_vocab import load_deck_a_card_records
from gwent_rl.text_embeddings import make_hash_embeddings


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_rollout_runner_and_ppo_update_smoke(tmp_path):
    torch.manual_seed(123)
    policy = CandidatePolicyValueNet(hidden_dim=32)
    optimizer = torch.optim.Adam(policy.parameters(), lr=1e-3)
    with RlCollector(num_envs=4, max_batch_size=4, base_seed=123, reward_mode="dense") as collector:
        runner = RolloutRunner(collector, policy)
        rollout, stats = runner.collect_steps(32)
        assert stats.decisions >= 32
        # Mulligan 必须真正进入 PPO rollout，而不是只存在于 C ABI 枚举或单元测试里。
        rollout_obs = rollout._concat_obs()
        mulligan_rows = rollout_obs["decision_kinds"] == 2
        assert mulligan_rows.any()
        mulligan_option_kinds = rollout_obs["option_kind_ids"][mulligan_rows]
        mulligan_option_mask = rollout_obs["option_mask"][mulligan_rows].astype(bool)
        assert any(10 in row[mask] for row, mask in zip(mulligan_option_kinds, mulligan_option_mask))
        assert any(4 in row[mask] for row, mask in zip(mulligan_option_kinds, mulligan_option_mask))
        advantages, returns = rollout.compute_signed_gae()
        assert advantages.shape[0] == rollout.size
        tensor_rollout = rollout.as_torch()
        update = ppo_update(
            policy,
            optimizer,
            tensor_rollout,
            PPOConfig(epochs=1, minibatch_size=16, learning_rate=1e-3),
        )
        assert torch.isfinite(torch.tensor(update.loss))
        checkpoint = tmp_path / "policy.pt"
        torch.save({"model": policy.state_dict()}, checkpoint)
        assert checkpoint.exists()


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_complete_game_generation_rollout_has_only_terminal_trajectories():
    torch.manual_seed(321)
    policy = CandidatePolicyValueNet(hidden_dim=32)
    with RlCollector(num_envs=2, max_batch_size=2, base_seed=321, reward_mode="strategic") as collector:
        runner = RolloutRunner(collector, policy)
        rollout, stats = runner.collect_games(2)
        assert stats.completed_episodes == 2
        assert stats.decisions == rollout.size
        advantages, returns = rollout.compute_signed_gae(require_complete_episodes=True)
        assert advantages.shape[0] == rollout.size
        assert returns.shape[0] == rollout.size
        tensors = rollout.as_torch(normalize_advantages=False)
        assert int(tensors.dones.sum().item()) == 2


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_benchmark_and_hash_embeddings_smoke(tmp_path):
    stats = run_benchmark(num_envs=4, rounds=4, seed=5)
    assert stats["decisions"] > 0
    records = load_deck_a_card_records()
    assert records
    out = make_hash_embeddings(tmp_path / "deck_a_hash.npz", dim=16)
    assert out.exists()


def test_prefix_encoder_preserves_order():
    torch.manual_seed(123)
    policy = CandidatePolicyValueNet(hidden_dim=16, num_attention_heads=4, max_prefix=4)
    policy.eval()
    device = torch.device("cpu")
    object_tokens = torch.zeros((1, 1, 16), dtype=torch.float32)

    common = {
        "prefix_mask": torch.tensor([[1, 1, 0, 0]], dtype=torch.bool),
        "prefix_row_ids": torch.tensor([[0, 0, -1, -1]], dtype=torch.long),
        "prefix_source_object_indices": torch.full((1, 4), -1, dtype=torch.long),
        "prefix_target_object_indices": torch.full((1, 4), -1, dtype=torch.long),
    }
    forward = {**common, "prefix_kind_ids": torch.tensor([[3, 8, 0, 0]], dtype=torch.long)}
    reversed_order = {**common, "prefix_kind_ids": torch.tensor([[8, 3, 0, 0]], dtype=torch.long)}

    a = policy._prefix_context(forward, object_tokens, device)
    b = policy._prefix_context(reversed_order, object_tokens, device)
    assert not torch.allclose(a, b)


def test_config_inline_empty_matchups_is_mapping(tmp_path):
    from gwent_rl.config import load_experiment_config

    cfg_path = tmp_path / "empty_matchups.yaml"
    cfg_path.write_text(
        "collector:\n"
        "  matchups: {}\n",
        encoding="utf-8",
    )
    cfg = load_experiment_config(cfg_path)
    assert cfg.collector.matchups == {}
    assert isinstance(cfg.collector.matchups, dict)


def test_config_round_trip_and_checkpoint_metadata(tmp_path):
    from gwent_rl import CandidatePolicyValueNet, ExperimentConfig, create_run_dir, load_experiment_config, save_checkpoint

    cfg_path = tmp_path / "cfg.yaml"
    cfg_path.write_text(
        "seed: 7  # inline comments are allowed\n"
        "run_dir: runs/test\n"
        "collector:\n"
        "  num_envs: 4\n"
        "  reward_mode: dense\n"
        "  matchups:\n"
        "    ab:\n"
        "      player0: deck_a\n"
        "      player1: deck_b\n"
        "      weight: 7\n"
        "model:\n"
        "  hidden_dim: 32\n"
        "ppo:\n"
        "  updates: 1\n",
        encoding="utf-8",
    )
    cfg = load_experiment_config(cfg_path)
    assert cfg.seed == 7
    assert cfg.collector.num_envs == 4
    assert cfg.collector.reward_mode == "dense"
    assert cfg.collector.matchups["ab"] == {"player0": "deck_a", "player1": "deck_b", "weight": 7}
    assert cfg.ppo.total_games == 0
    policy = CandidatePolicyValueNet(hidden_dim=32, num_attention_heads=4, attention_layers=1)
    opt = torch.optim.Adam(policy.parameters(), lr=1e-3)
    ckpt = save_checkpoint(tmp_path / "latest.pt", policy, opt, config=cfg, update=3, total_decisions=99)
    payload = torch.load(ckpt, map_location="cpu")
    assert payload["metadata"]["schema_version"] == SCHEMA_VERSION
    assert payload["metadata"]["action_grammar_version"] == ACTION_GRAMMAR_VERSION
    assert payload["metadata"]["prefix_semantics"] == "ordered_full_resolution_prefix"
    assert payload["metadata"]["reward_mode"] == "dense"
    assert payload["metadata"]["update"] == 3
    from gwent_rl import policy_from_checkpoint
    reloaded, metadata = policy_from_checkpoint(ckpt)
    assert reloaded.hidden_dim == 32
    assert reloaded.num_attention_heads == 4
    assert metadata["total_decisions"] == 99


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_eval_and_replay_debug_smoke(tmp_path):
    from gwent_rl.eval_policy import evaluate
    from gwent_rl.replay_debug import record_random_trace, replay_trace

    policy = CandidatePolicyValueNet(hidden_dim=32)
    stats = evaluate(policy, num_envs=4, rounds=4, seed=9, opponent="random", controlled_player=0, reward_mode="terminal")
    assert stats["decisions"] > 0
    assert stats["illegal_result_count"] == 0
    assert "secured_round_pass_rate" in stats
    library_path = os.environ["GWENT_CORE_LIBRARY"]
    trace = record_random_trace(
        num_envs=4,
        rounds=3,
        seed=9,
        reward_mode="terminal",
        library_path=library_path,
    )
    path = tmp_path / "trace.json"
    path.write_text(json.dumps(trace.to_json()), encoding="utf-8")
    replay_stats = replay_trace(path, library_path=library_path)
    assert replay_stats["mismatches"] == 0
    assert replay_stats["applied"] == len(trace.actions)


def test_checkpoint_schema_mismatch_is_rejected(tmp_path):
    from gwent_rl import CandidatePolicyValueNet, policy_from_checkpoint

    policy = CandidatePolicyValueNet(hidden_dim=32, num_attention_heads=4)
    ckpt = tmp_path / "schema3.pt"
    torch.save({"model": policy.state_dict(), "metadata": {"schema_version": 3}}, ckpt)
    with pytest.raises(ValueError, match="RL schema v3"):
        policy_from_checkpoint(ckpt)


def test_checkpoint_action_grammar_mismatch_is_rejected(tmp_path):
    from gwent_rl import CandidatePolicyValueNet, policy_from_checkpoint

    policy = CandidatePolicyValueNet(hidden_dim=32, num_attention_heads=4)
    ckpt = tmp_path / "grammar2.pt"
    torch.save(
        {
            "model": policy.state_dict(),
            "metadata": {
                "schema_version": SCHEMA_VERSION,
                "action_grammar_version": ACTION_GRAMMAR_VERSION - 1,
                "prefix_semantics": "ordered_full_resolution_prefix",
            },
        },
        ckpt,
    )
    with pytest.raises(ValueError, match=rf"action_grammar_version={ACTION_GRAMMAR_VERSION - 1}"):
        policy_from_checkpoint(ckpt)


def test_policy_text_embeddings_and_checkpoint_round_trip(tmp_path):
    from gwent_rl import CandidatePolicyValueNet, make_hash_embedding_table, policy_from_checkpoint
    from gwent_rl.config import ExperimentConfig
    from gwent_rl.experiment import save_checkpoint

    table = make_hash_embedding_table(dim=8)
    policy = CandidatePolicyValueNet(hidden_dim=32, num_attention_heads=4, text_embedding_dim=8)
    policy.set_text_embedding_table(table, freeze=True)
    assert policy.text_embedding_dim == 8
    assert policy.text_embedding is not None
    assert policy.text_embedding.weight.shape[1] == 8

    opt = torch.optim.Adam(policy.parameters(), lr=1e-3)
    cfg = ExperimentConfig()
    cfg.model.hidden_dim = 32
    cfg.model.text_embedding_mode = "hash"
    cfg.model.text_embedding_dim = 8
    cfg.model.freeze_text_embeddings = True
    ckpt = save_checkpoint(tmp_path / "text_policy.pt", policy, opt, config=cfg, update=1, total_decisions=10)
    restored, metadata = policy_from_checkpoint(ckpt)
    assert restored.text_embedding is not None
    assert restored.text_embedding.weight.shape == policy.text_embedding.weight.shape
    assert metadata["model_manifest"]["text_embedding_dim"] == 8


@pytest.mark.skipif("GWENT_CORE_LIBRARY" not in os.environ, reason="GWENT_CORE_LIBRARY is not set")
def test_m60_benchmark_suite_smoke(tmp_path):
    from gwent_rl.benchmark_suite import build_cases, run_suite

    stats = run_benchmark(
        num_envs=4,
        rounds=2,
        warmup_rounds=1,
        seed=60,
        torch_forward=True,
        hidden_dim=32,
        text_embedding_mode="none",
    )
    assert stats["decisions"] > 0
    assert stats["forward_ms"] >= 0.0
    assert stats["model_param_count"] > 0

    cases = build_cases(
        envs=[4],
        hidden_dims=[32],
        text_modes=["none", "hash"],
        object_attention=[True, False],
        self_attention=[True],
        attention_layers=[1],
        num_attention_heads=[4],
    )[:2]
    summary = run_suite(
        cases,
        rounds=2,
        warmup_rounds=1,
        seed=61,
        reward_mode="terminal",
        text_embedding_dim=16,
        device="cpu",
        torch_threads=None,
        library_path=os.environ["GWENT_CORE_LIBRARY"],
        output_dir=tmp_path / "bench",
    )
    assert summary["case_count"] == 2
    assert (tmp_path / "bench" / "benchmark_matrix.csv").exists()
    assert (tmp_path / "bench" / "benchmark_summary.json").exists()

def test_m61_benchmark_analysis_autotuning_smoke(tmp_path):
    from gwent_rl.benchmark_analysis import load_benchmark_rows, run_analysis
    from gwent_rl.config import load_experiment_config

    bench_dir = tmp_path / "bench"
    bench_dir.mkdir()
    csv_path = bench_dir / "benchmark_matrix.csv"
    csv_path.write_text(
        "case_id,num_envs,hidden_dim,text_embedding_mode,text_embedding_dim,use_candidate_object_attention,use_candidate_self_attention,attention_layers,num_attention_heads,reward_mode,torch_threads,decisions_per_second,collect_ms,to_torch_ms,forward_ms,action_select_ms,apply_ms,loop_overhead_ms,rss_delta_mb,model_param_count,device\n"
        "case_0001,4,32,none,0,true,true,1,4,terminal,4,1000,0.1,0.05,0.2,0.01,0.12,0.01,3,50000,cpu\n"
        "case_0002,8,64,hash,16,true,true,1,4,terminal,4,1800,0.2,0.08,0.35,0.02,0.18,0.02,8,150000,cpu\n"
        "case_0003,8,32,none,0,false,true,1,4,terminal,4,1500,0.18,0.06,0.16,0.01,0.16,0.01,4,48000,cpu\n",
        encoding="utf-8",
    )
    rows = load_benchmark_rows(bench_dir)
    assert len(rows) == 3
    out_dir = tmp_path / "analysis"
    analysis = run_analysis(bench_dir, output_dir=out_dir, top_k=2, write_configs=True)
    assert analysis["case_count"] == 3
    assert analysis["throughput_max"] == 1800
    assert (out_dir / "benchmark_analysis.json").exists()
    assert (out_dir / "benchmark_report.md").exists()
    local_cfg = out_dir / "configs" / "ppo_best_balanced.yaml"
    assert local_cfg.exists()
    cfg = load_experiment_config(local_cfg)
    assert cfg.collector.num_envs in {4, 8}
    assert cfg.model.hidden_dim in {32, 64}


def test_default_config_uses_bundled_qwen_and_auto_device():
    from gwent_rl.config import load_experiment_config
    from gwent_rl.device import resolve_torch_device
    from gwent_rl.text_embeddings import resolve_text_embedding_table

    cfg = load_experiment_config(None)
    assert cfg.device == "auto"
    assert cfg.model.text_embedding_mode == "npz"
    assert cfg.model.text_embedding_dim == 1024
    assert "qwen1024" in str(cfg.model.text_embedding_path)
    device = resolve_torch_device(cfg.device)
    assert device.type in {"cpu", "cuda", "mps"}
    table = resolve_text_embedding_table(cfg.model.text_embedding_mode, cfg.model.text_embedding_path, cfg.model.text_embedding_dim)
    assert table is not None
    assert table.dim == 1024
    from gwent_rl.card_vocab import load_supported_card_records
    assert table.row_count == len(load_supported_card_records()) + 1


def test_rollout_buffer_retain_actor_filters_after_gae_without_recomputing():
    import numpy as np
    from gwent_rl.rollout_buffer import RolloutBuffer

    rollout = RolloutBuffer()
    rollout._obs = [{
        "global_features": np.arange(8, dtype=np.float32).reshape(4, 2),
        "actor_deck_ids": np.array([0, 1, 0, 1], dtype=np.int32),
    }]
    rollout._actions = [np.array([0, 1, 2, 3], dtype=np.int64)]
    rollout._log_probs = [np.zeros(4, dtype=np.float32)]
    rollout._values = [np.zeros(4, dtype=np.float32)]
    rollout._rewards = [np.array([0.0, 0.0, 1.0, -1.0], dtype=np.float32)]
    rollout._dones = [np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float32)]
    rollout._env_indices = [np.zeros(4, dtype=np.uint64)]
    rollout._actor_ids = [np.array([0, 1, 0, 1], dtype=np.int32)]
    rollout._decision_serials = [np.arange(4, dtype=np.uint64)]

    rollout.compute_signed_gae(gamma=0.99, gae_lambda=0.95, require_complete_episodes=True)
    full_advantages = rollout._advantages.copy()
    kept = rollout.retain_actor(0)

    assert kept == 2
    assert rollout.size == 2
    assert rollout._actor_ids[0].tolist() == [0, 0]
    assert np.allclose(rollout._advantages, full_advantages[[0, 2]])
    tensors = rollout.as_torch(normalize_advantages=False)
    assert tensors.actor_ids.tolist() == [0, 0]
    assert tensors.observations["actor_deck_ids"].tolist() == [0, 0]
