#!/usr/bin/env python3
"""Validate training configs/tasks or inspect a checkpoint without launching training."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PYTHON_SRC = ROOT / "python" / "src"
sys.path.insert(0, str(PYTHON_SRC))

from gwent_rl.config import load_experiment_config, load_mapping_file  # noqa: E402
from gwent_rl.schema import ACTION_GRAMMAR_VERSION, SCHEMA_VERSION  # noqa: E402
from gwent_rl.training.initialization import checkpoint_sha256, inspect_checkpoint  # noqa: E402
from gwent_rl.training.task import load_training_task  # noqa: E402

CHECKPOINT_REGISTRY = ROOT / "artifacts" / "checkpoints" / "registry.yaml"


def resolve(path: str | Path) -> Path:
    p = Path(path)
    return p.resolve() if p.is_absolute() else (ROOT / p).resolve()


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)



def load_checkpoint_registry() -> dict[str, dict]:
    if not CHECKPOINT_REGISTRY.exists():
        return {}
    raw = load_mapping_file(CHECKPOINT_REGISTRY)
    entries = raw.get("checkpoints", {})
    if not isinstance(entries, dict):
        raise ValueError("artifacts/checkpoints/registry.yaml checkpoints must be a mapping")
    return {str(name): dict(value) for name, value in entries.items() if isinstance(value, dict)}


def registered_checkpoint(path: Path) -> tuple[str, dict] | None:
    target = rel(path)
    for name, entry in load_checkpoint_registry().items():
        if str(entry.get("path", "")) == target:
            return name, entry
    return None


def validate_registered_source(path: Path, task) -> None:
    found = registered_checkpoint(path)
    if found is None:
        if task.status == "ready":
            raise ValueError(
                f"ready warm-start task references an unpackaged, unregistered artifact: {rel(path)}"
            )
        return
    name, entry = found
    expected_sha = task.initialization.checkpoint_sha256.strip().lower()
    registered_sha = str(entry.get("sha256", "")).strip().lower()
    if expected_sha and registered_sha != expected_sha:
        raise ValueError(
            f"checkpoint registry SHA mismatch for {name}: registry={registered_sha}, task={expected_sha}"
        )
    if int(entry.get("observation_schema", -1)) != task.initialization.source_observation_schema:
        raise ValueError(
            f"checkpoint registry schema mismatch for {name}: "
            f"registry={entry.get('observation_schema')}, task={task.initialization.source_observation_schema}"
        )
    if int(entry.get("action_grammar", -1)) != task.initialization.source_action_grammar:
        raise ValueError(
            f"checkpoint registry grammar mismatch for {name}: "
            f"registry={entry.get('action_grammar')}, task={task.initialization.source_action_grammar}"
        )

def validate_config(path: Path) -> list[str]:
    cfg = load_experiment_config(path)
    print(f"PASS config: {rel(path)}")
    print(
        f"  device={cfg.device} reward={cfg.collector.reward_mode} "
        f"games={cfg.ppo.total_games} envs={cfg.collector.num_envs} run_dir={cfg.run_dir}"
    )
    return []


def validate_task(path: Path, *, require_current: bool = True) -> list[str]:
    raw = load_mapping_file(path)
    task = load_training_task(path)
    warnings: list[str] = []
    compatibility_pinned = bool(raw.get("compatibility"))
    algorithm = resolve(task.algorithm.config)
    if not algorithm.exists():
        raise ValueError(f"algorithm config missing: {task.algorithm.config}")
    load_experiment_config(algorithm)

    version_mismatches: list[str] = []
    if task.compatibility.observation_schema != SCHEMA_VERSION:
        version_mismatches.append(
            f"observation schema task={task.compatibility.observation_schema}, current={SCHEMA_VERSION}"
        )
    if task.compatibility.action_grammar != ACTION_GRAMMAR_VERSION:
        version_mismatches.append(
            f"action grammar task={task.compatibility.action_grammar}, current={ACTION_GRAMMAR_VERSION}"
        )
    if version_mismatches:
        detail = "; ".join(version_mismatches)
        if require_current or not compatibility_pinned:
            raise ValueError(f"target contract mismatch: {detail}")
        warnings.append(
            f"version-pinned task preserved without retargeting: {rel(path)} ({detail}); "
            "planning/running this task against the current runtime will still fail"
        )

    if task.training_control.mode == "learner_vs_frozen":
        frozen = resolve(task.training_control.frozen_checkpoint)
        if frozen.exists():
            actual_digest = checkpoint_sha256(frozen)
            expected_digest = task.training_control.frozen_checkpoint_sha256.strip().lower()
            if expected_digest and actual_digest != expected_digest:
                raise ValueError(
                    f"frozen-opponent checkpoint SHA-256 mismatch: actual={actual_digest}, expected={expected_digest}"
                )
            meta = inspect_checkpoint(frozen)
            if int(meta.get("schema_version", -1)) != SCHEMA_VERSION:
                raise ValueError("frozen-opponent checkpoint observation schema is not current")
            if int(meta.get("action_grammar_version", -1)) != ACTION_GRAMMAR_VERSION:
                raise ValueError("frozen-opponent checkpoint action grammar is not current")
        elif task.status == "ready":
            warnings.append(
                f"focused frozen checkpoint is not packaged in this source tree: {rel(frozen)}; "
                "stage the pinned checkpoint before server run"
            )

    if task.initialization.mode == "warm_start":
        checkpoint = resolve(task.initialization.checkpoint)
        if checkpoint.exists():
            actual_digest = checkpoint_sha256(checkpoint)
            expected_digest = task.initialization.checkpoint_sha256.strip().lower()
            if expected_digest and actual_digest != expected_digest:
                raise ValueError(
                    f"warm-start checkpoint SHA-256 mismatch: actual={actual_digest}, expected={expected_digest}"
                )
            meta = inspect_checkpoint(checkpoint)
            source_schema = int(meta.get("schema_version", -1))
            source_grammar = int(meta.get("action_grammar_version", -1))
            if source_schema != task.initialization.source_observation_schema:
                raise ValueError(
                    f"warm-start source schema mismatch: checkpoint={source_schema}, "
                    f"task={task.initialization.source_observation_schema}"
                )
            if source_grammar != task.initialization.source_action_grammar:
                raise ValueError(
                    f"warm-start source grammar mismatch: checkpoint={source_grammar}, "
                    f"task={task.initialization.source_action_grammar}"
                )
        else:
            validate_registered_source(checkpoint, task)

    print(f"PASS task: {rel(path)}")
    print(
        f"  target=schema-v{task.compatibility.observation_schema}/grammar-v{task.compatibility.action_grammar} "
        f"games={task.budget.total_games} run_dir={task.execution.run_dir} "
        f"mode={task.training_control.mode} resume={task.execution.resume}"
    )
    return warnings


def inspect(path: Path) -> None:
    if not path.exists():
        raise ValueError(f"checkpoint missing: {path}")
    metadata = inspect_checkpoint(path)
    view = {
        "path": str(path),
        "sha256": checkpoint_sha256(path),
        "schema_version": metadata.get("schema_version"),
        "action_grammar_version": metadata.get("action_grammar_version"),
        "prefix_semantics": metadata.get("prefix_semantics"),
        "update": metadata.get("update"),
        "total_games": metadata.get("total_games"),
        "total_decisions": metadata.get("total_decisions"),
        "model_manifest": metadata.get("model_manifest", {}),
    }
    print(json.dumps(view, ensure_ascii=False, indent=2))


def validate_one(path: Path, *, require_current: bool = True) -> list[str]:
    if not path.exists():
        raise ValueError(f"missing file: {path}")
    if path.parent.name == "tasks" and path.parent.parent.name == "training":
        return validate_task(path, require_current=require_current)
    return validate_config(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", help="configs/training/*.yaml or training/tasks/*.yaml")
    parser.add_argument("--all", action="store_true", help="validate all repository configs and tasks")
    parser.add_argument("--checkpoint", help="inspect checkpoint metadata instead of validating config/task")
    args = parser.parse_args()

    modes = int(bool(args.path)) + int(args.all) + int(bool(args.checkpoint))
    if modes != 1:
        parser.error("choose exactly one of PATH, --all, or --checkpoint")

    try:
        if args.checkpoint:
            inspect(resolve(args.checkpoint))
            return 0

        warnings: list[str] = []
        if args.all:
            paths = sorted((ROOT / "configs" / "training").glob("*.yaml")) + sorted((ROOT / "training" / "tasks").glob("*.yaml"))
            if not paths:
                raise ValueError("no configs/tasks found")
            for path in paths:
                warnings.extend(validate_one(path, require_current=False))
            for warning in warnings:
                print(f"WARN {warning}")
            print(f"PASS all training definitions: {len(paths)} files, {len(warnings)} warnings")
            return 0

        warnings.extend(validate_one(resolve(args.path)))
        for warning in warnings:
            print(f"WARN {warning}")
        return 0
    except Exception as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
