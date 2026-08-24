#!/usr/bin/env python3
"""Compare only the legal_surface portions of two golden traces."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from compare_trace import first_diff
from trace_schema import TraceSchemaError, validate_trace_document


def load(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def legal_projection(doc: dict[str, Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for step in doc.get("steps", []):
        result.append(
            {
                "step_index": step.get("step_index"),
                "label": step.get("label"),
                "legal_surface": step.get("legal_surface"),
            }
        )
    return result


def action_groups(surface: dict[str, Any] | None) -> dict[str, int]:
    groups: dict[str, int] = {}
    if not surface:
        return groups
    for action in surface.get("actions", []):
        prefix = str(action).split(":", 1)[0]
        groups[prefix] = groups.get(prefix, 0) + 1
    return groups


def print_group_summary(name: str, doc: dict[str, Any]) -> None:
    print(f"{name} legal-surface summary:")
    for step in doc.get("steps", []):
        surface = step.get("legal_surface")
        print(f"  step {step.get('step_index')}: {step.get('label')!r} {action_groups(surface)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--summary", action="store_true", help="Print action-category counts before comparing.")
    args = parser.parse_args()

    left = load(args.left)
    right = load(args.right)
    validate_trace_document(left, require_legal_surface=True)
    validate_trace_document(right, require_legal_surface=True)

    if args.summary:
        print_group_summary("left", left)
        print_group_summary("right", right)

    diff = first_diff(legal_projection(left), legal_projection(right), path="$.legal_surface")
    if diff is None:
        print("OK: legal surfaces match")
        return 0
    print("LEGAL SURFACE MISMATCH")
    print(diff)
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, json.JSONDecodeError, TraceSchemaError) as exc:
        print(f"legal surface diff failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
