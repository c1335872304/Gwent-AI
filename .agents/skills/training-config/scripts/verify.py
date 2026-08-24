#!/usr/bin/env python3
"""Deterministic verification entry point for the training-config Skill."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def run(*args: str) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", help="validate one training config/task before the repository-wide checks")
    parser.add_argument("--smoke", action="store_true", help="also build the core and run the shortest collector/PPO smoke")
    args = parser.parse_args()
    try:
        if args.task:
            run(sys.executable, ".agents/skills/training-config/scripts/validate_training.py", args.task)
        run(sys.executable, ".agents/skills/training-config/scripts/validate_training.py", "--all")
        if args.smoke:
            run(sys.executable, "scripts/check.py", "train")
        print("PASS training-config verification")
        return 0
    except subprocess.CalledProcessError as exc:
        print(f"FAIL training-config verification: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
