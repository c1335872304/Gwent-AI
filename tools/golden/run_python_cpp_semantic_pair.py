#!/usr/bin/env python3
"""Run shared semantic .trace scripts through both Python and C++ cores.

This is the first action-level pair runner.  It intentionally compares only the
fields that are already semantically aligned between the two engines; known
implementation-internal fields such as listener memory are ignored until the
ports are fully converged.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


DEFAULT_IGNORES = "mulligans_available,cards_drawn_this_round,order_charges,memory,listeners"


CASES = [
    "deck_a_semantic_pass_pair",
    "deck_a_semantic_unit_play",
    "deck_a_semantic_unit_then_pass",
    "deck_a_semantic_named_hand_play",
]


def run_to_file(cmd: list[str], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(cmd, check=True, text=True, capture_output=True)
    path.write_text(result.stdout, encoding="utf-8")


def compare(here: Path, python_trace: Path, cpp_trace: Path, ignore_key: str) -> None:
    cmd = [
        sys.executable,
        str(here / "compare_trace.py"),
        str(python_trace),
        str(cpp_trace),
        "--ignore-engine-name",
        "--ignore-checksums",
        "--ignore-actions-results",
        "--ignore-key", ignore_key,
    ]
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp-runner", type=Path, required=True, help="Path to gwent_trace_runner")
    parser.add_argument("--python-core", type=Path, required=True, help="Path containing the extracted Python gwent package")
    parser.add_argument("--out-dir", type=Path, default=Path("build/golden/python_cpp_semantic"))
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--starting-player", type=int, default=0)
    parser.add_argument("--case", action="append", choices=CASES, help="Run only selected case(s); may be repeated")
    parser.add_argument("--ignore-key", default=DEFAULT_IGNORES)
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    case_names = args.case or CASES
    for name in case_names:
        script = here / "cases" / f"{name}.trace"
        cpp_trace = args.out_dir / f"cpp_{name}.json"
        python_trace = args.out_dir / f"python_{name}.json"
        run_to_file(
            [
                str(args.cpp_runner),
                "--scenario", name,
                "--script", str(script),
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
                "--scenario", name,
                "--script", str(script),
                "--seed", str(args.seed),
                "--starting-player", str(args.starting_player),
                "--deck", "deck-a",
                "--effects", "deck-a",
            ],
            python_trace,
        )
        compare(here, python_trace, cpp_trace, args.ignore_key)
        print(f"OK: semantic Python/C++ pair {name}")
    print(f"OK: wrote semantic pair traces -> {args.out_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"semantic pair command failed: {' '.join(map(str, exc.cmd))}", file=sys.stderr)
        raise SystemExit(exc.returncode)
