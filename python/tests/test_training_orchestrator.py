from __future__ import annotations

import json
from pathlib import Path

import pytest

from gwent_rl import ACTION_GRAMMAR_VERSION, SCHEMA_VERSION
from gwent_rl.training.planner import build_training_plan
from gwent_rl.training.state import StateStore, TrainingTaskState
from gwent_rl.training.task import TASK_SCHEMA_VERSION, load_training_task


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_mulligan_task_has_bounded_250k_plan():
    task_path = REPO_ROOT / "training" / "tasks" / "mulligan_v3_250k.yaml"
    task = load_training_task(task_path)
    plan = build_training_plan(task, task_path, project_root=REPO_ROOT, resume_policy="never")

    assert task.schema_version == TASK_SCHEMA_VERSION
    assert task.name == "mulligan_v3_250k"
    assert plan.total_games == 250_000
    assert plan.games_per_update == 2_500
    assert plan.expected_updates == 100
    assert plan.checkpoint_every_games == 10_000
    assert plan.eval_every_games == 10_000
    assert plan.expected_eval_count == 25
    assert "duplicate_wild_hunt_rider" in plan.probes
    assert plan.probes["duplicate_wild_hunt_rider"]["status"] == "reserved"
    assert "gwent_rl.train_ppo" in plan.command
    assert "--total-games" in plan.command
    assert plan.initialization_mode == "warm_start"
    assert task.initialization.source_observation_schema == 6
    assert task.initialization.source_action_grammar == 2
    assert task.initialization.checkpoint_sha256 == "921dacf91fdbb11f924b6f2bda931b5ad46712d04706eb95c404cfc8175b35e1"
    # Source-package zips may intentionally omit large model files. Planner metadata
    # is populated only when the pinned checkpoint is locally available.
    checkpoint = REPO_ROOT / task.initialization.checkpoint
    if checkpoint.exists():
        assert plan.initialization_source_schema == 6
        assert plan.initialization_source_action_grammar == 2
        assert plan.initialization_checkpoint_sha256 == task.initialization.checkpoint_sha256
    else:
        assert plan.initialization_source_schema is None
        assert plan.initialization_source_action_grammar is None
        assert plan.initialization_checkpoint_sha256 is None
    assert "--initialize-from" in plan.command
    assert "--initialize-checkpoint-sha256" in plan.command
    assert "--reset-decision-embedding" in plan.command
    assert "--reset-option-embedding" in plan.command



def test_unpinned_task_uses_current_environment_contract():
    task_path = REPO_ROOT / "training" / "tasks" / "smoke.yaml"
    task = load_training_task(task_path)
    assert task.compatibility.observation_schema == SCHEMA_VERSION
    assert task.compatibility.action_grammar == ACTION_GRAMMAR_VERSION


def test_task_parser_rejects_unknown_fields(tmp_path: Path):
    task_path = tmp_path / "bad.yaml"
    task_path.write_text(
        """
schema_version: 3
name: bad
status: ready
unexpected_key: 123
budget:
  total_games: 10
  games_per_update: 5
""".strip(),
        encoding="utf-8",
    )
    with pytest.raises(ValueError, match="unknown task keys"):
        load_training_task(task_path)


def test_state_store_roundtrip_is_structured(tmp_path: Path):
    path = tmp_path / "trainer" / "task_state.json"
    store = StateStore(path)
    state = TrainingTaskState(task_name="x", run_dir="runs/x", status="RUNNING", pid=123)
    store.save(state)
    loaded = store.load()

    assert loaded is not None
    assert loaded.task_name == "x"
    assert loaded.status == "RUNNING"
    assert loaded.pid == 123
    raw = json.loads(path.read_text(encoding="utf-8"))
    assert raw["schema_version"] == 1


def test_auto_resume_discovers_latest_checkpoint(tmp_path: Path):
    from gwent_rl.training.task import TrainingTask, ExecutionSpec, BudgetSpec

    run_dir = tmp_path / "run"
    latest = run_dir / "checkpoints" / "latest.pt"
    latest.parent.mkdir(parents=True)
    latest.write_bytes(b"checkpoint")

    task = TrainingTask(
        name="resume_case",
        status="ready",
        budget=BudgetSpec(total_games=100, games_per_update=10),
        execution=ExecutionSpec(run_dir=str(run_dir), library="", resume="auto"),
    )
    task_file = REPO_ROOT / "training" / "tasks" / "smoke.yaml"
    plan = build_training_plan(task, task_file, project_root=REPO_ROOT)

    assert plan.resume_policy == "auto"
    assert plan.resume_checkpoint == str(latest)
    assert "--resume" in plan.command


def test_ready_warm_start_task_requires_pinned_sha256(tmp_path: Path):
    task_path = tmp_path / "warm.yaml"
    task_path.write_text(
        """
schema_version: 3
name: unsafe_warm
status: ready
algorithm:
  config: configs/training/ppo_debug.yaml
compatibility:
  observation_schema: 6
  action_grammar: 3
initialization:
  mode: warm_start
  checkpoint: old.pt
  source_observation_schema: 6
  source_action_grammar: 2
budget:
  total_games: 10
  games_per_update: 5
runtime:
  num_envs: 2
  max_batch_size: 2
  minibatch_size: 4
execution:
  run_dir: runs/unsafe
  library: ""
  resume: never
""".strip(),
        encoding="utf-8",
    )
    with pytest.raises(ValueError, match="checkpoint_sha256"):
        load_training_task(task_path)


def test_insert_position_warmstart_task_targets_current_contract_and_pinned_source():
    task_path = REPO_ROOT / "training" / "tasks" / "insert_position_v1_warmstart_50k.yaml"
    task = load_training_task(task_path)
    plan = build_training_plan(task, task_path, project_root=REPO_ROOT, resume_policy="never")

    assert task.compatibility.observation_schema == SCHEMA_VERSION
    assert task.compatibility.action_grammar == ACTION_GRAMMAR_VERSION == 5
    assert task.initialization.source_observation_schema == 11
    assert task.initialization.source_action_grammar == 4
    assert task.initialization.checkpoint_sha256 == "86f2a5915be988b24c50c58faa7bdc7d6b12262b78f50dc3c608386a4bab9717"
    assert plan.total_games == 50_000
    assert task.evaluation.games == 2_000
    assert "--eval-games" in plan.command
    eval_games_index = plan.command.index("--eval-games")
    assert plan.command[eval_games_index + 1] == "2000"
    assert "--eval-rounds" not in plan.command
    assert "--initialize-from" in plan.command
    migration_checks = [c for c in plan.checks if c.name == "initialization.migration_policy"]
    # Source packages may omit the large checkpoint; when staged, planner must
    # surface the exact reviewed migration policy.
    checkpoint = REPO_ROOT / task.initialization.checkpoint
    if checkpoint.exists():
        assert len(migration_checks) == 1
        assert "schema-11-grammar-4__to__schema-12-grammar-5" in migration_checks[0].detail
        assert "insert_position" in migration_checks[0].detail


def test_task_rejects_unknown_embedding_reset_name(tmp_path: Path):
    task_path = tmp_path / "bad-reset.yaml"
    task_path.write_text(
        """
schema_version: 3
name: bad_reset
status: draft
algorithm:
  config: configs/training/ppo_debug.yaml
initialization:
  mode: warm_start
  checkpoint: old.pt
  source_observation_schema: 11
  source_action_grammar: 4
  reset_optimizer: true
  reset_decision_embeddings: typo_position
budget:
  total_games: 10
  games_per_update: 5
runtime:
  num_envs: 2
  max_batch_size: 2
  minibatch_size: 4
execution:
  run_dir: runs/bad-reset
  library: ""
  resume: never
""".strip(),
        encoding="utf-8",
    )
    with pytest.raises(ValueError, match="reset_decision_embeddings"):
        load_training_task(task_path)


def test_learner_vs_frozen_task_emits_pinned_generic_training_args(tmp_path: Path):
    import hashlib
    import torch

    checkpoint = tmp_path / "u20.pt"
    torch.save(
        {
            "model": {},
            "metadata": {
                "schema_version": SCHEMA_VERSION,
                "action_grammar_version": ACTION_GRAMMAR_VERSION,
                "prefix_semantics": "ordered_full_resolution_prefix",
                "update": 20,
                "total_games": 50000,
            },
        },
        checkpoint,
    )
    digest = hashlib.sha256(checkpoint.read_bytes()).hexdigest()
    task_path = tmp_path / "focused.yaml"
    task_path.write_text(
        f"""
schema_version: 3
name: focused_generic
status: ready
algorithm:
  config: configs/training/ppo_128env_ab_strategic.yaml
initialization:
  mode: warm_start
  checkpoint: {checkpoint}
  checkpoint_sha256: {digest}
  source_observation_schema: {SCHEMA_VERSION}
  source_action_grammar: {ACTION_GRAMMAR_VERSION}
  reset_optimizer: true
training_control:
  mode: learner_vs_frozen
  learner_deck: deck_b
  opponent_deck: deck_a
  frozen_checkpoint: {checkpoint}
  frozen_checkpoint_sha256: {digest}
  side_schedule: alternate
budget:
  total_games: 10000
  games_per_update: 1000
runtime:
  num_envs: 128
  max_batch_size: 128
  minibatch_size: 256
  collector_threads: 12
  device: cpu
checkpoint:
  interval_updates: 2
evaluation:
  enabled: true
  every_updates: 2
  games: 200
  num_envs: 32
  internal_promote_win_rate: 0.55
execution:
  run_dir: {tmp_path / 'focused-run'}
  library: ""
  resume: never
""".strip(),
        encoding="utf-8",
    )

    task = load_training_task(task_path)
    plan = build_training_plan(task, task_path, project_root=REPO_ROOT, resume_policy="never")

    assert task.training_control.mode == "learner_vs_frozen"
    assert plan.training_mode == "learner_vs_frozen"
    assert plan.frozen_opponent_checkpoint == str(checkpoint)
    assert plan.frozen_opponent_checkpoint_sha256 == digest
    assert "--training-mode" in plan.command
    assert plan.command[plan.command.index("--training-mode") + 1] == "learner_vs_frozen"
    assert plan.command[plan.command.index("--learner-deck") + 1] == "deck_b"
    assert plan.command[plan.command.index("--opponent-deck") + 1] == "deck_a"
    assert plan.command[plan.command.index("--frozen-opponent-sha256") + 1] == digest
    frozen_checks = [c for c in plan.checks if c.name == "training_control.frozen_contract"]
    assert len(frozen_checks) == 1
    assert frozen_checks[0].level == "ok"
