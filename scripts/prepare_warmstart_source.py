#!/usr/bin/env python3
"""Validate and stage a pinned warm-start checkpoint for a Training Task.

This is intentionally separate from ``install_model.py``: a historical
warm-start source may be incompatible with the current inference contract and
must never be promoted directly into the product model slot.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[1]
PYTHON_SRC = ROOT / "python" / "src"
sys.path.insert(0, str(PYTHON_SRC))

from gwent_rl.config import load_experiment_config  # noqa: E402
from gwent_rl.device import resolve_torch_device  # noqa: E402
from gwent_rl.train_ppo import build_policy_from_config  # noqa: E402
from gwent_rl.training.initialization import (  # noqa: E402
    checkpoint_sha256,
    inspect_checkpoint,
    warm_start_policy,
)
from gwent_rl.training.task import load_training_task  # noqa: E402

DEFAULT_TASK = ROOT / "training" / "tasks" / "insert_position_v1_warmstart_50k.yaml"


def _resolve(value: str | Path) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (ROOT / path).resolve()


def _validate_source(source: Path, task_path: Path):
    task = load_training_task(task_path)
    if task.initialization.mode != "warm_start":
        raise ValueError(f"task {task.name} is not a warm_start task")

    expected_sha = task.initialization.checkpoint_sha256.strip().lower()
    actual_sha = checkpoint_sha256(source)
    if not expected_sha:
        raise ValueError("task does not pin initialization.checkpoint_sha256")
    if actual_sha != expected_sha:
        raise ValueError(
            f"checkpoint SHA-256 mismatch: actual={actual_sha}, expected={expected_sha}"
        )

    metadata = inspect_checkpoint(source)
    source_schema = int(metadata.get("schema_version", -1))
    source_grammar = int(metadata.get("action_grammar_version", -1))
    if source_schema != task.initialization.source_observation_schema:
        raise ValueError(
            f"source schema mismatch: checkpoint={source_schema}, "
            f"task={task.initialization.source_observation_schema}"
        )
    if source_grammar != task.initialization.source_action_grammar:
        raise ValueError(
            f"source grammar mismatch: checkpoint={source_grammar}, "
            f"task={task.initialization.source_action_grammar}"
        )

    config = load_experiment_config(_resolve(task.algorithm.config))
    torch.manual_seed(config.seed)
    policy = build_policy_from_config(config, resolve_torch_device("cpu"))
    report = warm_start_policy(
        source,
        policy,
        expected_source_schema=task.initialization.source_observation_schema,
        expected_source_action_grammar=task.initialization.source_action_grammar,
        reset_decision_embeddings=task.initialization.reset_decision_embeddings,
        reset_option_embeddings=task.initialization.reset_option_embeddings,
        expected_sha256=expected_sha,
    )
    return task, metadata, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", help="downloaded historical .pt checkpoint")
    parser.add_argument("--task", default=str(DEFAULT_TASK), help="warm-start Training Task")
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="run all migration checks without copying into the task artifact slot",
    )
    args = parser.parse_args()

    source = _resolve(args.checkpoint)
    task_path = _resolve(args.task)
    if not source.is_file():
        print(f"FAIL checkpoint not found: {source}", file=sys.stderr)
        return 1

    try:
        task, metadata, report = _validate_source(source, task_path)
        destination = _resolve(task.initialization.checkpoint)
        if not args.verify_only and source != destination:
            destination.parent.mkdir(parents=True, exist_ok=True)
            tmp = destination.with_suffix(destination.suffix + ".tmp")
            shutil.copy2(source, tmp)
            os.replace(tmp, destination)
            # Verify the bytes that will actually be consumed by the task.
            if checkpoint_sha256(destination) != report.sha256:
                raise ValueError("staged checkpoint digest changed after copy")

        print("PASS warm-start source")
        print(f"  task={task.name}")
        print(f"  sha256={report.sha256}")
        print(
            f"  contract=schema-{report.source_schema}/grammar-{report.source_action_grammar} "
            f"-> schema-{report.target_schema}/grammar-{report.target_action_grammar}"
        )
        print(f"  migration={report.migration_id}")
        print(f"  source_training=update-{report.source_update}, games={report.source_total_games}")
        print(f"  copied_parameter_keys={report.copied_parameter_keys}")
        print(f"  target_initialized_keys={report.target_initialized_keys or 'none'}")
        print(f"  reset_decision_embeddings={report.reset_decision_embeddings or 'none'}")
        print(f"  reset_option_embeddings={report.reset_option_embeddings or 'none'}")
        print("  optimizer=reset")
        if args.verify_only:
            print("  mode=verify-only (source not copied)")
        else:
            print(f"  staged={destination}")
        return 0
    except Exception as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
