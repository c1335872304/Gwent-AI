#!/usr/bin/env python3
"""Update/check the small set of generated RL contract version mirrors.

The human-edited source of truth is config/rl_contract.json.
Typical version bump:

    python scripts/bump_rl_contract.py --schema 8
    python scripts/bump_rl_contract.py --grammar 4

The command updates the manifest and regenerates the C/Python mirrors. Current
prose docs intentionally do not hard-code these numbers, so a normal version
bump should not require a documentation sweep.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "config" / "rl_contract.json"
C_HEADER = ROOT / "include" / "gwent" / "c" / "rl_contract_versions.h"
PY_MODULE = ROOT / "python" / "src" / "gwent_rl" / "_contract_versions.py"

KEYS = ("observation_schema", "action_grammar", "reward_config")


def load_manifest() -> dict[str, int]:
    raw = json.loads(MANIFEST.read_text(encoding="utf-8"))
    missing = [key for key in KEYS if key not in raw]
    extra = sorted(set(raw) - set(KEYS))
    if missing or extra:
        raise ValueError(f"invalid {MANIFEST.relative_to(ROOT)}: missing={missing}, extra={extra}")
    values = {key: int(raw[key]) for key in KEYS}
    if any(value <= 0 for value in values.values()):
        raise ValueError("all RL contract versions must be positive integers")
    return values


def render_c(values: dict[str, int]) -> str:
    return f'''#pragma once

/* Generated from config/rl_contract.json by scripts/bump_rl_contract.py.
 * Do not edit version numbers here by hand.
 */
#define GWENT_RL_SCHEMA_VERSION {values["observation_schema"]}
#define GWENT_RL_ACTION_GRAMMAR_VERSION {values["action_grammar"]}
#define GWENT_RL_REWARD_CONFIG_VERSION {values["reward_config"]}
'''


def render_python(values: dict[str, int]) -> str:
    return f'''"""Generated RL contract versions. Do not edit by hand."""

SCHEMA_VERSION = {values["observation_schema"]}
ACTION_GRAMMAR_VERSION = {values["action_grammar"]}
REWARD_CONFIG_VERSION = {values["reward_config"]}
'''


def write_generated(values: dict[str, int]) -> None:
    C_HEADER.parent.mkdir(parents=True, exist_ok=True)
    PY_MODULE.parent.mkdir(parents=True, exist_ok=True)
    C_HEADER.write_text(render_c(values), encoding="utf-8")
    PY_MODULE.write_text(render_python(values), encoding="utf-8")


def check_generated(values: dict[str, int]) -> list[str]:
    expected = {
        C_HEADER: render_c(values),
        PY_MODULE: render_python(values),
    }
    errors: list[str] = []
    for path, content in expected.items():
        if not path.exists():
            errors.append(f"missing generated file: {path.relative_to(ROOT)}")
        elif path.read_text(encoding="utf-8") != content:
            errors.append(
                f"stale generated file: {path.relative_to(ROOT)}; "
                "run python scripts/bump_rl_contract.py --sync"
            )
    return errors


def save_manifest(values: dict[str, int]) -> None:
    MANIFEST.write_text(json.dumps(values, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=int, help="new observation schema version")
    parser.add_argument("--grammar", type=int, help="new action grammar version")
    parser.add_argument("--reward", type=int, help="new reward-config ABI version")
    parser.add_argument("--sync", action="store_true", help="regenerate mirrors from the manifest")
    parser.add_argument("--check", action="store_true", help="verify generated mirrors without modifying files")
    args = parser.parse_args()

    values = load_manifest()
    requested = {
        "observation_schema": args.schema,
        "action_grammar": args.grammar,
        "reward_config": args.reward,
    }
    updates = {key: value for key, value in requested.items() if value is not None}
    if args.check and (updates or args.sync):
        parser.error("--check cannot be combined with update/sync options")
    if any(value <= 0 for value in updates.values()):
        parser.error("versions must be positive integers")

    if args.check:
        errors = check_generated(values)
        if errors:
            for error in errors:
                print(f"ERROR {error}")
            return 1
        print(
            "PASS RL contract mirrors: "
            f"schema=v{values['observation_schema']} "
            f"grammar=v{values['action_grammar']} "
            f"reward=v{values['reward_config']}"
        )
        return 0

    if updates:
        old = dict(values)
        values.update(updates)
        save_manifest(values)
        write_generated(values)
        print(
            "UPDATED RL contract: "
            f"schema v{old['observation_schema']}→v{values['observation_schema']}, "
            f"grammar v{old['action_grammar']}→v{values['action_grammar']}, "
            f"reward v{old['reward_config']}→v{values['reward_config']}"
        )
        return 0

    if args.sync:
        write_generated(values)
        print("SYNCED generated RL contract mirrors")
        return 0

    print(json.dumps(values, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
