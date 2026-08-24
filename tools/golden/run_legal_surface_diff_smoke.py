#!/usr/bin/env python3
"""Smoke-test the legal_surface_diff tool against two identical generated traces."""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    left = args.out_dir / "legal_surface.left.json"
    right = args.out_dir / "legal_surface.right.json"
    runner_args = [
        str(args.runner),
        "--scenario", "trace_smoke_legal_surface",
        "--script", "tools/golden/cases/trace_smoke.trace",
        "--seed", "0",
        "--starting-player", "0",
        "--deck", "trace",
        "--effects", "none",
        "--include-legal-surface",
    ]
    for path in (left, right):
        result = subprocess.run(runner_args, check=True, text=True, capture_output=True)
        path.write_text(result.stdout, encoding="utf-8")

    tool = Path(__file__).with_name("legal_surface_diff.py")
    subprocess.run([sys.executable, str(tool), str(left), str(right), "--summary"], check=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - command-line diagnostics
        print(f"legal surface diff smoke failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
