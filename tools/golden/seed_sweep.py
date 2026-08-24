#!/usr/bin/env python3
"""Run deterministic trace scripts across a range of seeds and validate each trace."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from trace_schema import TraceSchemaError, validate_trace_document


def parse_seeds(value: str) -> list[int]:
    if ":" in value:
        parts = [int(part) if part else None for part in value.split(":")]
        if len(parts) not in (2, 3):
            raise ValueError("seed range must be start:end or start:end:step")
        start = 0 if parts[0] is None else parts[0]
        end = parts[1]
        step = 1 if len(parts) == 2 or parts[2] is None else parts[2]
        if end is None:
            raise ValueError("seed range end is required")
        return list(range(start, end, step))
    return [int(part.strip()) for part in value.split(",") if part.strip()]


def run_trace(runner: Path, args: list[str]) -> dict[str, Any]:
    result = subprocess.run([str(runner), *args], check=True, text=True, capture_output=True)
    return json.loads(result.stdout)


def assert_all_applied(doc: dict[str, Any]) -> None:
    for step in doc.get("steps", [])[1:]:
        result = step.get("result")
        if not result or not result.get("applied"):
            raise AssertionError(f"step {step.get('step_index')} was not applied: {step.get('label')}: {result}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument("--scenario", default="seed_sweep")
    parser.add_argument("--seeds", default="0:8", help="Comma list or Python-style start:end[:step] range.")
    parser.add_argument("--starting-players", default="0,1")
    parser.add_argument("--deck", default="trace")
    parser.add_argument("--effects", default="none")
    parser.add_argument("--shuffle", action="store_true")
    parser.add_argument("--include-legal-surface", action="store_true")
    parser.add_argument("--require-all-applied", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    seeds = parse_seeds(args.seeds)
    starting_players = parse_seeds(args.starting_players)
    if not seeds:
        raise ValueError("no seeds selected")
    if not starting_players:
        raise ValueError("no starting players selected")

    total = 0
    for seed in seeds:
        for starting_player in starting_players:
            runner_args = [
                "--scenario", args.scenario,
                "--script", str(args.script),
                "--seed", str(seed),
                "--starting-player", str(starting_player),
                "--deck", args.deck,
                "--effects", args.effects,
            ]
            if args.shuffle:
                runner_args.append("--shuffle")
            if args.include_legal_surface:
                runner_args.append("--include-legal-surface")

            doc1 = run_trace(args.runner, runner_args)
            doc2 = run_trace(args.runner, runner_args)
            validate_trace_document(doc1, require_legal_surface=args.include_legal_surface)
            validate_trace_document(doc2, require_legal_surface=args.include_legal_surface)
            if doc1 != doc2:
                raise AssertionError(f"non-deterministic trace for seed={seed}, starting_player={starting_player}")
            if args.require_all_applied:
                assert_all_applied(doc1)
            if args.out_dir is not None:
                args.out_dir.mkdir(parents=True, exist_ok=True)
                out_path = args.out_dir / f"{args.scenario}.seed{seed}.p{starting_player}.json"
                out_path.write_text(json.dumps(doc1, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            total += 1
            print(f"OK: seed={seed} starting_player={starting_player}")

    print(f"OK: seed sweep completed ({total} traces)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError, json.JSONDecodeError, TraceSchemaError, AssertionError, ValueError) as exc:
        print(f"seed sweep failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
