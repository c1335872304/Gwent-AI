#!/usr/bin/env python3
"""Generate a pinned learner-vs-frozen Training Task from a current checkpoint.

The generated task is deck-agnostic.  It preserves the repository reward
configuration and changes only policy ownership: the learner controls one deck,
the opponent uses a frozen checkpoint, and the learner seat alternates by PPO
update.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON_SRC = ROOT / "python" / "src"
sys.path.insert(0, str(PYTHON_SRC))

from gwent_rl.experiment import policy_from_checkpoint  # noqa: E402
from gwent_rl.schema import ACTION_GRAMMAR_VERSION, SCHEMA_VERSION  # noqa: E402
from gwent_rl.training.initialization import checkpoint_sha256, inspect_checkpoint  # noqa: E402


def _task_path_value(path: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(ROOT))
    except ValueError:
        return str(resolved)


def _validate_current_checkpoint(path: Path) -> tuple[str, dict]:
    if not path.exists():
        raise ValueError(f"checkpoint not found: {path}")
    digest = checkpoint_sha256(path)
    meta = inspect_checkpoint(path)
    schema = int(meta.get("schema_version", -1))
    grammar = int(meta.get("action_grammar_version", -1))
    if schema != SCHEMA_VERSION or grammar != ACTION_GRAMMAR_VERSION:
        raise ValueError(
            "learner-vs-frozen requires a current-contract checkpoint: "
            f"checkpoint=schema-{schema}/grammar-{grammar}, "
            f"current=schema-{SCHEMA_VERSION}/grammar-{ACTION_GRAMMAR_VERSION}"
        )
    # Production loader performs strict model-state reconstruction/validation.
    policy, _ = policy_from_checkpoint(path, device="cpu")
    del policy
    return digest, meta


def render_task(args: argparse.Namespace, checkpoint: Path, digest: str) -> str:
    checkpoint_value = _task_path_value(checkpoint)
    run_dir = args.run_dir or f"runs/tasks/{args.name}"
    return f"""schema_version: 3
name: {args.name}
status: ready
description: 通用 learner-vs-frozen 训练；只优化 learner 所有决策，冻结对手策略，奖励函数保持算法 config 原样不变。

algorithm:
  config: {args.algorithm_config}

initialization:
  mode: warm_start
  checkpoint: {checkpoint_value}
  checkpoint_sha256: {digest}
  source_observation_schema: {SCHEMA_VERSION}
  source_action_grammar: {ACTION_GRAMMAR_VERSION}
  reset_optimizer: true

training_control:
  mode: learner_vs_frozen
  learner_deck: {args.learner_deck}
  opponent_deck: {args.opponent_deck}
  frozen_checkpoint: {checkpoint_value}
  frozen_checkpoint_sha256: {digest}
  side_schedule: alternate

budget:
  total_games: {args.total_games}
  games_per_update: {args.games_per_update}

runtime:
  num_envs: {args.num_envs}
  max_batch_size: {args.max_batch_size or args.num_envs}
  minibatch_size: {args.minibatch_size}
  collector_threads: {args.collector_threads}
  device: {args.device}

checkpoint:
  interval_updates: {args.checkpoint_interval}

evaluation:
  enabled: true
  every_updates: {args.eval_interval}
  games: {args.eval_games}
  num_envs: {args.eval_num_envs}
  internal_promote_win_rate: {args.promote_win_rate}

execution:
  run_dir: {run_dir}
  library: build-release/libgwent_core.so
  resume: auto
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", help="Current-contract checkpoint used by both learner initialization and frozen opponent")
    parser.add_argument("--output", required=True, help="Output training/tasks/*.yaml path")
    parser.add_argument("--name", default="learner_vs_frozen_10k")
    parser.add_argument("--learner-deck", required=True)
    parser.add_argument("--opponent-deck", required=True)
    parser.add_argument("--total-games", type=int, default=10_000)
    parser.add_argument("--games-per-update", type=int, default=1_000)
    parser.add_argument("--num-envs", type=int, default=512)
    parser.add_argument("--max-batch-size", type=int, default=None)
    parser.add_argument("--minibatch-size", type=int, default=2048)
    parser.add_argument("--collector-threads", type=int, default=12)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--checkpoint-interval", type=int, default=2)
    parser.add_argument("--eval-interval", type=int, default=2)
    parser.add_argument("--eval-games", type=int, default=2000)
    parser.add_argument("--eval-num-envs", type=int, default=128)
    parser.add_argument("--promote-win-rate", type=float, default=0.55)
    parser.add_argument("--algorithm-config", default="configs/training/ppo_128env_ab_strategic.yaml")
    parser.add_argument("--run-dir", default=None)
    args = parser.parse_args()

    if args.total_games <= 0 or args.games_per_update <= 0:
        parser.error("game budgets must be positive")
    if args.games_per_update < args.num_envs:
        parser.error("games-per-update must be >= num-envs for exact complete-game collection")
    if args.eval_interval <= 0 or args.eval_games <= 0 or args.eval_num_envs <= 0:
        parser.error("evaluation parameters must be positive")
    if not 0.0 <= args.promote_win_rate <= 1.0:
        parser.error("promote-win-rate must be in [0, 1]")

    checkpoint = Path(args.checkpoint)
    if not checkpoint.is_absolute():
        checkpoint = ROOT / checkpoint
    try:
        digest, meta = _validate_current_checkpoint(checkpoint)
    except Exception as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1

    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render_task(args, checkpoint, digest), encoding="utf-8")

    print(f"PASS checkpoint: {checkpoint}")
    print(f"  sha256={digest}")
    print(
        f"  schema={meta.get('schema_version')} grammar={meta.get('action_grammar_version')} "
        f"update={meta.get('update', 0)} games={meta.get('total_games', 0)}"
    )
    print(f"PASS task: {output}")
    print(
        f"  learner={args.learner_deck} frozen={args.opponent_deck} "
        f"games={args.total_games} games/update={args.games_per_update} side_schedule=alternate"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
