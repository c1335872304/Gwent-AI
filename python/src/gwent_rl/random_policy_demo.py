from __future__ import annotations

import argparse
import time

import numpy as np

from .collector import RlCollector, random_legal_actions


def run(args: argparse.Namespace) -> None:
    rng = np.random.default_rng(args.seed)
    started = time.perf_counter()
    with RlCollector(
        num_envs=args.num_envs,
        max_batch_size=args.max_batch_size or args.num_envs,
        base_seed=args.seed,
        enable_invariants=args.enable_invariants,
        library_path=args.library,
    ) as collector:
        for _ in range(args.rounds):
            batch = collector.collect()
            actions = random_legal_actions(batch, rng)
            collector.apply_actions(batch, actions)
        elapsed = time.perf_counter() - started
        print(
            "gwent-rl random demo: "
            f"envs={collector.env_count} rounds={args.rounds} "
            f"steps={collector.total_steps} completed={collector.completed_episodes} "
            f"resets={collector.total_resets} elapsed_s={elapsed:.4f}"
        )


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Run random-policy rollout through C RL collector")
    parser.add_argument("--num-envs", type=int, default=128)
    parser.add_argument("--max-batch-size", type=int, default=0)
    parser.add_argument("--rounds", type=int, default=32)
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--enable-invariants", action="store_true")
    parser.add_argument("--library", default=None, help="Path to libgwent_core shared library")
    args = parser.parse_args(argv)
    run(args)


if __name__ == "__main__":
    main()
