#!/usr/bin/env python3
"""Compare two gwent-golden-trace-v1 JSON trace files.

The default comparison is intentionally strict for checksums and state snapshots.
Use --ignore-entity-ids when comparing engines that allocate entities differently;
that option recursively removes known entity-id fields before comparing states.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

ENTITY_KEYS = {
    "entity_id",
    "source_entity_id",
    "target_entity_id",
    "stratagem_entity_id",
    "next_entity_id",
    "listener_id",
    "source_id",
    "legal_card_targets",
}


def parse_ignore_keys(values: list[str] | None) -> set[str]:
    keys: set[str] = set()
    for value in values or []:
        for part in value.split(","):
            part = part.strip()
            if part:
                keys.add(part)
    return keys


def load(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def strip_keys(value: Any, keys: set[str]) -> Any:
    if isinstance(value, dict):
        return {k: strip_keys(v, keys) for k, v in value.items() if k not in keys}
    if isinstance(value, list):
        return [strip_keys(v, keys) for v in value]
    return value


def strip_entity_ids(value: Any) -> Any:
    return strip_keys(value, ENTITY_KEYS)


def first_diff(a: Any, b: Any, path: str = "$", max_string: int = 140) -> str | None:
    if type(a) is not type(b):
        return f"{path}: type differs: {type(a).__name__} != {type(b).__name__}"
    if isinstance(a, dict):
        a_keys = set(a)
        b_keys = set(b)
        if a_keys != b_keys:
            return f"{path}: keys differ: only_left={sorted(a_keys-b_keys)} only_right={sorted(b_keys-a_keys)}"
        for key in sorted(a):
            diff = first_diff(a[key], b[key], f"{path}.{key}", max_string=max_string)
            if diff:
                return diff
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return f"{path}: list length differs: {len(a)} != {len(b)}"
        for i, (x, y) in enumerate(zip(a, b)):
            diff = first_diff(x, y, f"{path}[{i}]", max_string=max_string)
            if diff:
                return diff
        return None
    if a != b:
        av = repr(a)
        bv = repr(b)
        if len(av) > max_string:
            av = av[:max_string] + "..."
        if len(bv) > max_string:
            bv = bv[:max_string] + "..."
        return f"{path}: value differs: {av} != {bv}"
    return None


def comparable(
    doc: dict[str, Any],
    ignore_entity_ids: bool,
    ignore_engine_name: bool,
    ignore_checksums: bool,
    ignore_actions_results: bool,
    ignore_keys: set[str],
) -> dict[str, Any]:
    result = dict(doc)
    if ignore_engine_name:
        result.pop("engine", None)
    if ignore_entity_ids:
        result = strip_entity_ids(result)
        ignore_checksums = True
        ignore_actions_results = True
    if ignore_keys:
        result = strip_keys(result, ignore_keys)
    for step in result.get("steps", []):
        if ignore_checksums:
            step.pop("checksum", None)
        if ignore_actions_results:
            step.pop("action", None)
            step.pop("result", None)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare Python and C++ golden traces.")
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--ignore-entity-ids", action="store_true")
    parser.add_argument("--ignore-engine-name", action="store_true")
    parser.add_argument("--ignore-checksums", action="store_true")
    parser.add_argument("--ignore-actions-results", action="store_true")
    parser.add_argument(
        "--ignore-key",
        action="append",
        default=[],
        help="Recursively ignore a JSON key. May be repeated or comma-separated.",
    )
    args = parser.parse_args()

    ignore_keys = parse_ignore_keys(args.ignore_key)
    left = comparable(
        load(args.left),
        args.ignore_entity_ids,
        args.ignore_engine_name,
        args.ignore_checksums,
        args.ignore_actions_results,
        ignore_keys,
    )
    right = comparable(
        load(args.right),
        args.ignore_entity_ids,
        args.ignore_engine_name,
        args.ignore_checksums,
        args.ignore_actions_results,
        ignore_keys,
    )
    diff = first_diff(left, right)
    if diff is None:
        print("OK: traces match")
        return 0
    print("TRACE MISMATCH")
    print(diff)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
