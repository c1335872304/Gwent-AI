from __future__ import annotations

import argparse
import numpy as np

from gwent_rl import (
    GLOBAL_FEATURE_NAMES,
    OBJECT_FEATURE_NAMES,
    OPTION_FEATURE_NAMES,
    RlCollector,
    explain_batch_row,
    random_legal_actions,
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--num-envs", type=int, default=16)
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--seed", type=int, default=46)
    parser.add_argument("--library", default=None)
    args = parser.parse_args()

    rng = np.random.default_rng(args.seed)
    with RlCollector(
        num_envs=args.num_envs,
        max_batch_size=args.num_envs,
        base_seed=args.seed,
        enable_invariants=True,
        library_path=args.library,
    ) as collector:
        for _ in range(args.rounds):
            batch = collector.collect()
            assert batch.count == args.num_envs
            assert batch.global_features.shape == (args.num_envs, 34)
            assert batch.object_features.shape == (args.num_envs, 128, 27)
            assert batch.option_features.shape == (args.num_envs, 256, 16)
            assert batch.option_mask.shape == (args.num_envs, 256)
            assert batch.prefix_mask.shape == (args.num_envs, 16)
            assert batch.prefix_card_ids.shape == (args.num_envs, 16)
            assert len(GLOBAL_FEATURE_NAMES) == 34
            assert len(OBJECT_FEATURE_NAMES) == 27
            assert len(OPTION_FEATURE_NAMES) == 16
            assert "actor_score" in GLOBAL_FEATURE_NAMES
            assert "opponent_ranged_frost_duration" in GLOBAL_FEATURE_NAMES
            assert batch.global_features[0, 0] == 1.0
            assert batch.object_counts.shape == (args.num_envs,)
            assert batch.option_card_ids.shape == (args.num_envs, 256)
            assert batch.option_insert_positions.shape == (args.num_envs, 256)
            assert batch.prefix_insert_positions.shape == (args.num_envs, 16)
            _ = explain_batch_row(batch, 0, max_objects=2, max_options=2)
            actions = random_legal_actions(batch, rng)
            results = collector.apply_actions(batch, actions)
            assert results.count == args.num_envs
            assert np.all(results.result_codes == 0)
        print(
            f"python ctypes collector smoke ok: envs={collector.env_count} "
            f"steps={collector.total_steps} completed={collector.completed_episodes}"
        )


if __name__ == "__main__":
    main()
