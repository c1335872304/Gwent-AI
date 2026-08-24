#!/usr/bin/env python3
"""Deterministic verification entry point for the core-environment Skill."""

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
    parser.add_argument("--tests", action="store_true", help="also run focused project regression tests")
    args = parser.parse_args()
    try:
        run(sys.executable, ".agents/skills/core-environment/scripts/check_schema.py")
        run(sys.executable, "tools/codegen/generate_card_data.py", "--check")
        run(sys.executable, "tools/codegen/validate_card_data.py")
        if args.tests:
            run(sys.executable, "scripts/check.py", "test")
        print("PASS core-environment verification")
        return 0
    except subprocess.CalledProcessError as exc:
        print(f"FAIL core-environment verification: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
