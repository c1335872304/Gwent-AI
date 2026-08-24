#!/usr/bin/env python3
"""Validate gwent-golden-trace-v1 JSON emitted by gwent_trace_runner."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from trace_schema import TraceSchemaError, validate_trace_document


def load_trace(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def run_runner(runner: Path, runner_args: list[str]) -> dict[str, Any]:
    result = subprocess.run([str(runner), *runner_args], check=True, text=True, capture_output=True)
    return json.loads(result.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, help="Existing trace JSON to validate.")
    parser.add_argument("--runner", type=Path, help="gwent_trace_runner path. Arguments after -- are passed to it.")
    parser.add_argument("--require-legal-surface", action="store_true")
    parser.add_argument("runner_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    if (args.trace is None) == (args.runner is None):
        parser.error("provide exactly one of --trace or --runner")

    runner_args = args.runner_args
    if runner_args and runner_args[0] == "--":
        runner_args = runner_args[1:]

    if args.trace is not None:
        doc = load_trace(args.trace)
        source = str(args.trace)
    else:
        doc = run_runner(args.runner, runner_args)
        source = str(args.runner)

    validate_trace_document(doc, require_legal_surface=args.require_legal_surface)
    print(f"OK: trace schema valid ({source})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError, json.JSONDecodeError, TraceSchemaError) as exc:
        print(f"trace schema validation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
