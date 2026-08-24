#!/usr/bin/env python3
"""Read-only placeholder for the future card-extension scaffolder.

It intentionally does not edit Core files. The goal is to give the Core Agent a
stable command today while leaving room for future boilerplate generation.
"""

from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--card", default="<new-card>", help="card name or id")
    args = parser.parse_args()

    print(f"Card extension placeholder: {args.card}")
    print("Read: .agents/skills/core-environment/references/CARD_EXTENSION.md")
    print("Inspect these existing extension points:")
    for rel in (
        "data/cards",
        "src/cards",
        "src/game/card_catalog.cpp",
        "src/engine/effect_registry.cpp",
        "src/engine/primitive_effects.cpp",
        "tests/unit",
        "tools/golden",
    ):
        path = ROOT / rel
        print(f"  [{'ok' if path.exists() else 'missing'}] {rel}")
    print("No files were modified. Future versions may generate boilerplate only.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
