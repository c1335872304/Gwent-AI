#!/usr/bin/env python3
from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

from gwent_rl.replay_debug import main as replay_main
from gwent_rl.train_ppo_demo import main as train_main


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True)
    args = parser.parse_args()
    train_main([
        "--library", args.library,
        "--num-envs", "4",
        "--steps-per-update", "32",
        "--updates", "1",
        "--hidden-dim", "32",
        "--minibatch-size", "16",
        "--epochs", "1",
        "--reward-mode", "dense",
    ])
    with tempfile.TemporaryDirectory() as tmp:
        trace = Path(tmp) / "trace.json"
        replay_main([
            "record",
            "--library", args.library,
            "--num-envs", "4",
            "--rounds", "2",
            "--seed", "17",
            "--output", str(trace),
        ])
        replay_main([
            "replay",
            "--library", args.library,
            "--input", str(trace),
        ])


if __name__ == "__main__":
    main()
