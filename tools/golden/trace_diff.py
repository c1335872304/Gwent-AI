#!/usr/bin/env python3
"""Diff two golden trace JSON files and print the first semantic difference."""
from __future__ import annotations

import argparse
import json
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


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def parse_key_set(values: list[str]) -> set[str]:
    keys: set[str] = set()
    for value in values:
        for part in value.split(","):
            item = part.strip()
            if item:
                keys.add(item)
    return keys


def strip_keys(value: Any, keys: set[str]) -> Any:
    if isinstance(value, dict):
        return {k: strip_keys(v, keys) for k, v in value.items() if k not in keys}
    if isinstance(value, list):
        return [strip_keys(v, keys) for v in value]
    return value


def comparable(doc: dict[str, Any], args: argparse.Namespace) -> dict[str, Any]:
    result: dict[str, Any] = dict(doc)
    if args.ignore_engine_name:
        result.pop("engine", None)
    if args.ignore_entity_ids:
        result = strip_keys(result, ENTITY_KEYS)
        args.ignore_checksums = True
        args.ignore_actions_results = True
    if args.ignore_key:
        result = strip_keys(result, parse_key_set(args.ignore_key))
    for step in result.get("steps", []):
        if args.ignore_checksums:
            step.pop("checksum", None)
        if args.ignore_actions_results:
            step.pop("action", None)
            step.pop("result", None)
    return result


def short_repr(value: Any, max_len: int) -> str:
    text = repr(value)
    if len(text) > max_len:
        return text[:max_len] + "..."
    return text


def first_diff(left: Any, right: Any, path: str = "$", max_len: int = 180) -> str | None:
    if type(left) is not type(right):
        return f"{path}: type differs: {type(left).__name__} != {type(right).__name__}"
    if isinstance(left, dict):
        left_keys = set(left)
        right_keys = set(right)
        if left_keys != right_keys:
            return f"{path}: keys differ: only_left={sorted(left_keys-right_keys)} only_right={sorted(right_keys-left_keys)}"
        for key in sorted(left):
            diff = first_diff(left[key], right[key], f"{path}.{key}", max_len=max_len)
            if diff:
                return diff
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return f"{path}: list length differs: {len(left)} != {len(right)}"
        for index, (l_item, r_item) in enumerate(zip(left, right)):
            diff = first_diff(l_item, r_item, f"{path}[{index}]", max_len=max_len)
            if diff:
                return diff
        return None
    if left != right:
        return f"{path}: value differs: {short_repr(left, max_len)} != {short_repr(right, max_len)}"
    return None


def step_context(doc: dict[str, Any], path: str) -> str:
    marker = ".steps["
    if marker not in path:
        return ""
    start = path.index(marker) + len(marker)
    end = path.find("]", start)
    if end < 0:
        return ""
    try:
        step_index = int(path[start:end])
    except ValueError:
        return ""
    steps = doc.get("steps", [])
    if step_index < 0 or step_index >= len(steps):
        return ""
    step = steps[step_index]
    return f"step={step_index} label={step.get('label')!r} checksum={step.get('checksum')!r}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--ignore-entity-ids", action="store_true")
    parser.add_argument("--ignore-engine-name", action="store_true")
    parser.add_argument("--ignore-checksums", action="store_true")
    parser.add_argument("--ignore-actions-results", action="store_true")
    parser.add_argument("--ignore-key", action="append", default=[])
    args = parser.parse_args()

    raw_left = load_json(args.left)
    raw_right = load_json(args.right)
    left = comparable(raw_left, args)
    right = comparable(raw_right, args)
    diff = first_diff(left, right)
    if diff is None:
        print("OK: traces match")
        return 0
    print("TRACE MISMATCH")
    print(diff)
    context = step_context(raw_left, diff.split(":", 1)[0])
    if context:
        print(context)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
