#!/usr/bin/env python3
"""Run the first real Python ↔ C++ golden setup pair for Deck A."""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run_to_file(cmd: list[str], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(cmd, check=True, text=True, capture_output=True)
    path.write_text(result.stdout, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp-runner", type=Path, required=True, help="Path to gwent_trace_runner")
    parser.add_argument("--python-core", type=Path, required=True, help="Path containing the extracted Python gwent package")
    parser.add_argument("--out-dir", type=Path, default=Path("build/golden/python_cpp"))
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--starting-player", type=int, default=0)
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    cpp_trace = args.out_dir / "cpp_deck_a_setup.json"
    python_trace = args.out_dir / "python_deck_a_setup.json"

    run_to_file(
        [
            str(args.cpp_runner),
            "--scenario", "deck_a_option_replay",
            "--seed", str(args.seed),
            "--starting-player", str(args.starting_player),
            "--shuffle",
            "--deck", "deck-a",
            "--effects", "deck-a",
        ],
        cpp_trace,
    )
    run_to_file(
        [
            sys.executable,
            str(here / "python_trace_exporter.py"),
            "--python-core", str(args.python_core),
            "--scenario", "deck_a_option_replay",
            "--seed", str(args.seed),
            "--starting-player", str(args.starting_player),
        ],
        python_trace,
    )

    compare = subprocess.run(
        [
            sys.executable,
            str(here / "compare_trace.py"),
            str(python_trace),
            str(cpp_trace),
            "--ignore-engine-name",
            "--ignore-checksums",
            "--ignore-key", "mulligans_available,cards_drawn_this_round,order_charges",
        ],
        text=True,
        capture_output=True,
    )
    print(compare.stdout, end="")
    if compare.returncode != 0:
        print(compare.stderr, file=sys.stderr, end="")
        return compare.returncode
    print(f"OK: Python/C++ Deck A setup pair -> {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
