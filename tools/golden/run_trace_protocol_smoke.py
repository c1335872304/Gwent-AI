#!/usr/bin/env python3
"""Smoke-test m39 trace protocol tools against the C++ runner."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from trace_schema import validate_trace_document


def run(cmd: list[str], cwd: Path) -> None:
    subprocess.run(cmd, cwd=cwd, check=True)


def capture(cmd: list[str], cwd: Path, out_path: Path) -> dict:
    result = subprocess.run(cmd, cwd=cwd, check=True, text=True, capture_output=True)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(result.stdout, encoding="utf-8")
    return json.loads(result.stdout)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    root = Path.cwd()
    script = root / "tools/golden/cases/trace_smoke.trace"
    out_dir = args.out_dir
    trace_a = out_dir / "trace_a.json"
    trace_b = out_dir / "trace_b.json"

    base_cmd = [
        str(args.runner),
        "--scenario", "m39_protocol_smoke",
        "--script", str(script),
        "--seed", "0",
        "--starting-player", "0",
        "--deck", "trace",
        "--effects", "none",
        "--include-legal-surface",
    ]
    doc_a = capture(base_cmd, root, trace_a)
    doc_b = capture(base_cmd, root, trace_b)

    validate_trace_document(doc_a, require_legal_surface=True)
    validate_trace_document(doc_b, require_legal_surface=True)
    require(doc_a == doc_b, "trace runner output should be deterministic")
    first_surface = doc_a["steps"][0]["legal_surface"]
    require(first_surface["decision_type"] == "mulligan", "initial surface should be a mulligan decision")
    require("keep_hand" in first_surface["actions"], "initial mulligan surface should expose keep_hand")
    require(any(action.startswith("mulligan:") for action in first_surface["actions"]), "mulligan card options missing")

    turn_surface = doc_a["steps"][2]["legal_surface"]
    require(turn_surface["decision_type"] == "turn", "after both keep decisions the surface should enter normal turn play")
    require(
        any(action.startswith("play_hand:") and action.endswith(":target=none") for action in turn_surface["actions"]),
        "factorized play_hand source surface missing after mulligan",
    )

    run([sys.executable, "tools/golden/validate_trace_schema.py", "--trace", str(trace_a), "--require-legal-surface"], root)
    run([sys.executable, "tools/golden/trace_diff.py", str(trace_a), str(trace_b)], root)
    run([sys.executable, "tools/golden/legal_surface_diff.py", str(trace_a), str(trace_b), "--summary"], root)
    run([
        sys.executable,
        "tools/golden/seed_sweep.py",
        "--runner", str(args.runner),
        "--script", str(script),
        "--scenario", "m39_seed_sweep",
        "--out-dir", str(out_dir / "seed_sweep"),
        "--seeds", "0:3",
        "--starting-players", "0",
        "--deck", "trace",
        "--effects", "none",
        "--include-legal-surface",
        "--require-all-applied",
    ], root)
    print("OK: m39 trace protocol smoke")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - command-line diagnostics
        print(f"trace protocol smoke failure: {exc}", file=sys.stderr)
        raise SystemExit(1)
