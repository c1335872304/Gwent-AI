from __future__ import annotations

import json
import math
import os
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from gwent_rl.config import load_experiment_config
from gwent_rl.schema import ACTION_GRAMMAR_VERSION, SCHEMA_VERSION

from .initialization import checkpoint_sha256, inspect_checkpoint
from .migrations import get_migration_spec
from .task import TrainingTask




@dataclass
class PlanCheck:
    level: str  # ok | warning | error
    name: str
    detail: str


@dataclass
class TrainingPlan:
    task_name: str
    task_file: str
    project_root: str
    run_dir: str
    total_games: int
    games_per_update: int
    expected_updates: int
    checkpoint_every_games: int
    eval_every_games: int
    expected_eval_count: int
    initialization_mode: str
    initialization_checkpoint: str | None
    initialization_checkpoint_sha256: str | None
    initialization_source_schema: int | None
    initialization_source_action_grammar: int | None
    training_mode: str
    frozen_opponent_checkpoint: str | None
    frozen_opponent_checkpoint_sha256: str | None
    resume_policy: str
    resume_checkpoint: str | None
    command: list[str]
    environment: dict[str, str] = field(default_factory=dict)
    checks: list[PlanCheck] = field(default_factory=list)
    probes: dict[str, dict[str, Any]] = field(default_factory=dict)

    @property
    def has_errors(self) -> bool:
        return any(c.level == "error" for c in self.checks)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    def write_json(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_dict(), ensure_ascii=False, indent=2), encoding="utf-8")


def find_project_root(start: Path) -> Path:
    current = start.resolve()
    if current.is_file():
        current = current.parent
    for candidate in (current, *current.parents):
        if (candidate / "python" / "pyproject.toml").exists() and (candidate / "CMakeLists.txt").exists():
            return candidate
    raise ValueError(f"cannot locate project root from {start}")


def _resolve(root: Path, value: str) -> Path:
    p = Path(value)
    return p if p.is_absolute() else root / p


def _choose_resume(task: TrainingTask, run_dir: Path, override: str | None) -> str | None:
    policy = override or task.execution.resume
    if policy not in {"auto", "never", "required"}:
        raise ValueError(f"invalid resume policy: {policy}")
    latest = run_dir / "checkpoints" / "latest.pt"
    if policy == "never":
        return None
    if latest.exists():
        return str(latest)
    if policy == "required":
        raise ValueError(f"resume required but checkpoint not found: {latest}")
    return None


def build_training_plan(
    task: TrainingTask,
    task_path: str | Path,
    *,
    project_root: str | Path | None = None,
    resume_policy: str | None = None,
    library_override: str | None = None,
) -> TrainingPlan:
    task_path = Path(task_path).resolve()
    root = Path(project_root).resolve() if project_root else find_project_root(task_path)
    run_dir = _resolve(root, task.execution.run_dir).resolve()
    config_path = _resolve(root, task.algorithm.config).resolve()
    library_value = library_override or os.environ.get("GWENT_CORE_LIBRARY") or task.execution.library
    library_path = _resolve(root, library_value).resolve() if library_value else None

    checks: list[PlanCheck] = []
    base_config = None
    if task.status != "ready":
        checks.append(PlanCheck("warning", "task.status", f"任务状态为 {task.status!r}，正式服务器任务建议使用 ready"))
    checks.append(
        PlanCheck(
            "ok" if task.compatibility.observation_schema == SCHEMA_VERSION else "error",
            "observation_schema",
            f"task={task.compatibility.observation_schema}, python={SCHEMA_VERSION}",
        )
    )
    checks.append(
        PlanCheck(
            "ok" if task.compatibility.action_grammar == ACTION_GRAMMAR_VERSION else "error",
            "action_grammar",
            f"task={task.compatibility.action_grammar}, python={ACTION_GRAMMAR_VERSION}",
        )
    )
    if config_path.exists():
        try:
            base = load_experiment_config(config_path)
            base_config = base
            checks.append(PlanCheck("ok", "algorithm.config", str(config_path)))
            if base.model.text_embedding_mode == "npz" and base.model.text_embedding_path:
                emb = _resolve(root, base.model.text_embedding_path)
                checks.append(
                    PlanCheck(
                        "ok" if emb.exists() else "warning",
                        "text_embedding",
                        str(emb),
                    )
                )
        except Exception as exc:  # 配置错误应显示在 plan 中，而不是吞掉。
            checks.append(PlanCheck("error", "algorithm.config", f"{config_path}: {exc}"))
    else:
        checks.append(PlanCheck("error", "algorithm.config", f"not found: {config_path}"))

    if library_path is not None:
        checks.append(
            PlanCheck("ok" if library_path.exists() else "warning", "core_library", str(library_path))
        )

    initialization_checkpoint: Path | None = None
    initialization_checkpoint_digest: str | None = None
    initialization_source_schema: int | None = None
    initialization_source_grammar: int | None = None
    if task.initialization.mode == "warm_start":
        initialization_checkpoint = _resolve(root, task.initialization.checkpoint).resolve()
        if not initialization_checkpoint.exists():
            checks.append(
                PlanCheck("error", "initialization.checkpoint", f"not found: {initialization_checkpoint}")
            )
        else:
            try:
                initialization_checkpoint_digest = checkpoint_sha256(initialization_checkpoint)
                expected_digest = task.initialization.checkpoint_sha256.strip().lower()
                checks.append(
                    PlanCheck(
                        "ok" if not expected_digest or initialization_checkpoint_digest == expected_digest else "error",
                        "initialization.checkpoint_sha256",
                        f"actual={initialization_checkpoint_digest}, expected={expected_digest or 'not-pinned'}",
                    )
                )
                meta = inspect_checkpoint(initialization_checkpoint)
                initialization_source_schema = int(meta.get("schema_version", -1))
                initialization_source_grammar = int(meta.get("action_grammar_version", -1))
                checks.append(
                    PlanCheck(
                        "ok"
                        if initialization_source_schema == task.initialization.source_observation_schema
                        else "error",
                        "initialization.source_schema",
                        f"checkpoint={initialization_source_schema}, task={task.initialization.source_observation_schema}",
                    )
                )
                checks.append(
                    PlanCheck(
                        "ok"
                        if initialization_source_grammar == task.initialization.source_action_grammar
                        else "error",
                        "initialization.source_action_grammar",
                        f"checkpoint={initialization_source_grammar}, task={task.initialization.source_action_grammar}",
                    )
                )
                exact_contract = (
                    initialization_source_schema == SCHEMA_VERSION
                    and initialization_source_grammar == ACTION_GRAMMAR_VERSION
                )
                migration = None if exact_contract else get_migration_spec(
                    initialization_source_schema,
                    initialization_source_grammar,
                    SCHEMA_VERSION,
                    ACTION_GRAMMAR_VERSION,
                )
                contract_compatible = exact_contract or migration is not None
                checks.append(
                    PlanCheck(
                        "ok" if contract_compatible else "error",
                        "initialization.target_contract_compatibility",
                        f"source=schema-{initialization_source_schema}/grammar-{initialization_source_grammar}, "
                        f"target=schema-{SCHEMA_VERSION}/grammar-{ACTION_GRAMMAR_VERSION}",
                    )
                )
                if migration is not None:
                    checks.append(
                        PlanCheck(
                            "ok",
                            "initialization.migration_policy",
                            f"{migration.migration_id}; target_only={sorted(migration.target_only_keys) or 'none'}; "
                            f"reset_decision={list(migration.required_reset_decision_embeddings) or 'none'}; "
                            f"reset_option={list(migration.required_reset_option_embeddings) or 'none'}",
                        )
                    )

                checks.append(
                    PlanCheck(
                        "ok",
                        "initialization.source_training",
                        f"update={int(meta.get('update', 0))}, total_games={int(meta.get('total_games', 0))}",
                    )
                )
                manifest = meta.get("model_manifest", {})
                if isinstance(manifest, dict) and base_config is not None:
                    expected_manifest = {
                        "hidden_dim": int(base_config.model.hidden_dim),
                        "num_attention_heads": int(base_config.model.num_attention_heads),
                        "attention_layers": int(base_config.model.attention_layers),
                        "card_vocab_size": int(base_config.model.card_vocab_size),
                        "text_embedding_dim": int(base_config.model.text_embedding_dim),
                        "freeze_text_embeddings": bool(base_config.model.freeze_text_embeddings),
                        "use_candidate_object_attention": bool(base_config.model.use_candidate_object_attention),
                        "use_candidate_self_attention": bool(base_config.model.use_candidate_self_attention),
                    }
                    mismatches = [
                        f"{key}: checkpoint={manifest.get(key)!r}, config={value!r}"
                        for key, value in expected_manifest.items()
                        if key in manifest and manifest.get(key) != value
                    ]
                    checks.append(
                        PlanCheck(
                            "error" if mismatches else "ok",
                            "initialization.model_manifest",
                            "; ".join(mismatches) if mismatches else "checkpoint model manifest matches target config",
                        )
                    )
            except Exception as exc:
                checks.append(PlanCheck("error", "initialization.checkpoint", f"{initialization_checkpoint}: {exc}"))

    frozen_opponent_checkpoint: Path | None = None
    frozen_opponent_digest: str | None = None
    if task.training_control.mode == "learner_vs_frozen":
        frozen_opponent_checkpoint = _resolve(root, task.training_control.frozen_checkpoint).resolve()
        if not frozen_opponent_checkpoint.exists():
            checks.append(
                PlanCheck("error", "training_control.frozen_checkpoint", f"not found: {frozen_opponent_checkpoint}")
            )
        else:
            try:
                frozen_opponent_digest = checkpoint_sha256(frozen_opponent_checkpoint)
                expected = task.training_control.frozen_checkpoint_sha256.strip().lower()
                checks.append(
                    PlanCheck(
                        "ok" if not expected or frozen_opponent_digest == expected else "error",
                        "training_control.frozen_checkpoint_sha256",
                        f"actual={frozen_opponent_digest}, expected={expected or 'not-pinned'}",
                    )
                )
                frozen_meta = inspect_checkpoint(frozen_opponent_checkpoint)
                frozen_schema = int(frozen_meta.get("schema_version", -1))
                frozen_grammar = int(frozen_meta.get("action_grammar_version", -1))
                checks.append(
                    PlanCheck(
                        "ok" if frozen_schema == SCHEMA_VERSION and frozen_grammar == ACTION_GRAMMAR_VERSION else "error",
                        "training_control.frozen_contract",
                        f"checkpoint=schema-{frozen_schema}/grammar-{frozen_grammar}, "
                        f"required=schema-{SCHEMA_VERSION}/grammar-{ACTION_GRAMMAR_VERSION}",
                    )
                )
                checks.append(
                    PlanCheck(
                        "ok",
                        "training_control.policy_ownership",
                        f"learner={task.training_control.learner_deck}, "
                        f"frozen={task.training_control.opponent_deck}, "
                        "side_schedule=alternate; reward unchanged",
                    )
                )
            except Exception as exc:
                checks.append(
                    PlanCheck("error", "training_control.frozen_checkpoint", f"{frozen_opponent_checkpoint}: {exc}")
                )

    expected_updates = math.ceil(task.budget.total_games / task.budget.games_per_update)
    checkpoint_every_games = (
        task.checkpoint.interval_updates * task.budget.games_per_update
        if task.checkpoint.interval_updates > 0
        else 0
    )
    eval_every_updates = task.evaluation.every_updates if task.evaluation.enabled else 0
    eval_every_games = eval_every_updates * task.budget.games_per_update if eval_every_updates > 0 else 0
    expected_eval_count = expected_updates // eval_every_updates if eval_every_updates > 0 else 0
    effective_resume_policy = resume_policy or task.execution.resume
    resume_checkpoint = _choose_resume(task, run_dir, effective_resume_policy)
    if effective_resume_policy == "never" and (run_dir / "checkpoints" / "latest.pt").exists():
        checks.append(
            PlanCheck(
                "warning",
                "run_dir_collision",
                "resume=never 但 run_dir 已存在 latest.pt；真正执行会拒绝覆盖，请换 run_dir 或显式改 resume policy",
            )
        )
    if resume_checkpoint and task.initialization.mode == "warm_start":
        checks.append(
            PlanCheck(
                "ok",
                "initialization.precedence",
                "检测到 latest.pt：本次将 resume 当前 v3 任务；warm_start 只用于该 run_dir 的首次启动。",
            )
        )

    cmd = [
        sys.executable,
        "-m",
        "gwent_rl.train_ppo",
        "--config",
        str(config_path),
        "--total-games",
        str(task.budget.total_games),
        "--games-per-update",
        str(task.budget.games_per_update),
        "--num-envs",
        str(task.runtime.num_envs),
        "--max-batch-size",
        str(task.runtime.max_batch_size),
        "--minibatch-size",
        str(task.runtime.minibatch_size),
        "--device",
        task.runtime.device,
        "--checkpoint-interval",
        str(task.checkpoint.interval_updates),
        "--eval-interval",
        str(eval_every_updates),
        "--eval-games",
        str(task.evaluation.games),
        "--eval-num-envs",
        str(task.evaluation.num_envs),
        "--best-promote-win-rate",
        str(task.evaluation.internal_promote_win_rate),
        "--run-dir",
        str(run_dir),
    ]
    if task.training_control.mode == "learner_vs_frozen":
        assert frozen_opponent_checkpoint is not None
        cmd += [
            "--training-mode",
            "learner_vs_frozen",
            "--learner-deck",
            task.training_control.learner_deck,
            "--opponent-deck",
            task.training_control.opponent_deck,
            "--frozen-opponent-checkpoint",
            str(frozen_opponent_checkpoint),
            "--frozen-opponent-sha256",
            task.training_control.frozen_checkpoint_sha256,
        ]
    if library_path is not None:
        cmd += ["--library", str(library_path)]
    if resume_checkpoint:
        cmd += ["--resume", resume_checkpoint]
    elif initialization_checkpoint is not None:
        cmd += [
            "--initialize-from",
            str(initialization_checkpoint),
            "--initialize-source-schema",
            str(task.initialization.source_observation_schema),
            "--initialize-source-action-grammar",
            str(task.initialization.source_action_grammar),
        ]
        if task.initialization.checkpoint_sha256:
            cmd += ["--initialize-checkpoint-sha256", task.initialization.checkpoint_sha256]
        for name in task.initialization.reset_decision_embeddings:
            cmd += ["--reset-decision-embedding", str(name)]
        for name in task.initialization.reset_option_embeddings:
            cmd += ["--reset-option-embedding", str(name)]

    env: dict[str, str] = {}
    if task.runtime.collector_threads > 0:
        env["GWENT_COLLECTOR_THREADS"] = str(task.runtime.collector_threads)
    # 子进程从源码目录运行时，也能稳定导入当前 checkout，而不依赖用户先 pip install -e。
    env["PYTHONPATH"] = str(root / "python" / "src")

    return TrainingPlan(
        task_name=task.name,
        task_file=str(task_path),
        project_root=str(root),
        run_dir=str(run_dir),
        total_games=task.budget.total_games,
        games_per_update=task.budget.games_per_update,
        expected_updates=expected_updates,
        checkpoint_every_games=checkpoint_every_games,
        eval_every_games=eval_every_games,
        expected_eval_count=expected_eval_count,
        initialization_mode=task.initialization.mode,
        initialization_checkpoint=str(initialization_checkpoint) if initialization_checkpoint else None,
        initialization_checkpoint_sha256=initialization_checkpoint_digest,
        initialization_source_schema=initialization_source_schema,
        initialization_source_action_grammar=initialization_source_grammar,
        training_mode=task.training_control.mode,
        frozen_opponent_checkpoint=str(frozen_opponent_checkpoint) if frozen_opponent_checkpoint else None,
        frozen_opponent_checkpoint_sha256=frozen_opponent_digest,
        resume_policy=effective_resume_policy,
        resume_checkpoint=resume_checkpoint,
        command=cmd,
        environment=env,
        checks=checks,
        probes={name: asdict(probe) for name, probe in task.probes.items()},
    )
