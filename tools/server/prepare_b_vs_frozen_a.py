#!/usr/bin/env python3
"""Prepare (and optionally run) Deck B learner vs frozen Deck A training.

This is a thin server-side launcher around the repository's existing generic
learner_vs_frozen training mode. It does not change rewards or card rules.

Default experiment:
- learner: deck_b
- frozen opponent: deck_a
- learner initialization: same pinned checkpoint as frozen opponent
- 20,000 complete games
- 2,500 games/update (8 updates)
- learner seat alternates P0/P1 by update
- only learner-owned decisions are retained for PPO after full-trajectory GAE
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CHECKPOINT = Path(
    "runs/tasks/fair_observation_v1_warmstart_50k/checkpoints/best.pt"
)
DEFAULT_TASK = Path("training/tasks/deck_b_vs_frozen_a_20k.yaml")


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", default=str(DEFAULT_CHECKPOINT))
    parser.add_argument("--task", default=str(DEFAULT_TASK))
    parser.add_argument("--name", default="deck_b_vs_frozen_a_20k")
    parser.add_argument("--run-dir", default="runs/tasks/deck_b_vs_frozen_a_20k")
    parser.add_argument("--total-games", type=int, default=20_000)
    parser.add_argument("--games-per-update", type=int, default=2_500)
    parser.add_argument("--num-envs", type=int, default=512)
    parser.add_argument("--max-batch-size", type=int, default=512)
    parser.add_argument("--minibatch-size", type=int, default=2048)
    parser.add_argument("--collector-threads", type=int, default=12)
    parser.add_argument("--eval-interval", type=int, default=2)
    parser.add_argument("--eval-games", type=int, default=2000)
    parser.add_argument("--eval-num-envs", type=int, default=128)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--run", action="store_true", help="Start training after prepare/validate/plan")
    args = parser.parse_args()

    if args.total_games <= 0 or args.games_per_update <= 0:
        parser.error("game budgets must be positive")
    if args.games_per_update < args.num_envs:
        parser.error("games-per-update must be >= num-envs")

    python = sys.executable
    task = Path(args.task)
    checkpoint = Path(args.checkpoint)

    prepare_cmd = [
        python,
        "scripts/prepare_learner_vs_frozen_task.py",
        str(checkpoint),
        "--output", str(task),
        "--name", args.name,
        "--learner-deck", "deck_b",
        "--opponent-deck", "deck_a",
        "--total-games", str(args.total_games),
        "--games-per-update", str(args.games_per_update),
        "--num-envs", str(args.num_envs),
        "--max-batch-size", str(args.max_batch_size),
        "--minibatch-size", str(args.minibatch_size),
        "--collector-threads", str(args.collector_threads),
        "--device", args.device,
        "--checkpoint-interval", "2",
        "--eval-interval", str(args.eval_interval),
        "--eval-games", str(args.eval_games),
        "--eval-num-envs", str(args.eval_num_envs),
        "--algorithm-config", "configs/training/ppo_128env_ab_strategic.yaml",
        "--run-dir", args.run_dir,
    ]
    run(prepare_cmd)

    run([python, ".agents/skills/training-config/scripts/validate_training.py", "--all"])
    run([
        python,
        "-m", "gwent_rl.training.cli", "plan",
        "--task", str(task),
        "--library", "build-release/libgwent_core.so",
    ])

    print("\nREADY")
    print(f"  learner        : deck_b")
    print(f"  frozen opponent: deck_a")
    print(f"  checkpoint     : {checkpoint}")
    print(f"  task           : {task}")
    print(f"  run dir        : {args.run_dir}")
    print(f"  games          : {args.total_games}")
    print("  reward         : unchanged")
    print("  seat schedule  : alternate by update")
    print("  PPO rows       : learner-owned decisions only")

    if not args.run:
        print("\nTo start inside tmux:")
        print(
            f"{python} -m gwent_rl.training.cli run --task {task} "
            "--library build-release/libgwent_core.so"
        )
        return 0

    env = os.environ.copy()
    env["GWENT_COLLECTOR_THREADS"] = str(args.collector_threads)
    run([
        python,
        "-m", "gwent_rl.training.cli", "run",
        "--task", str(task),
        "--library", "build-release/libgwent_core.so",
    ], env=env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
