#!/usr/bin/env python3
"""Run deterministic C++ golden trace scripts and validate key invariants."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from trace_schema import validate_trace_document


def load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def card_ids(cards: list[dict[str, Any]]) -> list[str]:
    return [str(card.get("card_id")) for card in cards]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_case(runner: Path, out_dir: Path, name: str, args: list[str]) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{name}.json"
    cmd = [str(runner), *args]
    result = subprocess.run(cmd, check=True, text=True, capture_output=True)
    out_path.write_text(result.stdout, encoding="utf-8")
    return load(out_path)


def validate_trace_smoke(doc: dict[str, Any]) -> None:
    require(doc["schema_version"] == "gwent-golden-trace-v1", "schema version mismatch")
    require(len(doc["steps"]) == 9, "trace_smoke should contain initial + 2 keep + 6 play actions")
    final = doc["steps"][-1]["state"]
    require(final["round_no"] == 2, "trace_smoke should advance to round 2 after both players pass")
    require(final["current_player_id"] == 1, "tie should make previous round starter's opponent start next round")
    require(final["players"][0]["round_wins"] == 1, "tie should award player 0 a round win in this rules core")
    require(final["players"][1]["round_wins"] == 1, "tie should award player 1 a round win in this rules core")


def validate_deck_a_stratagem_order(doc: dict[str, Any]) -> None:
    require(len(doc["steps"]) == 4, "stratagem case should contain initial + 2 keep + use_order")
    final = doc["steps"][-1]["state"]
    p0 = final["players"][0]
    require(card_ids(p0["zones"]["banished"]) == ["202497"], "stratagem should be removed after its instruction resolves")
    require(p0["zones"]["melee"] == [], "stratagem should no longer remain on the battle row after use")
    first_hand = p0["zones"]["hand"][0]
    require(first_hand["card_id"] == "203099", "first hand card should remain Regis in no-shuffle Deck A")
    require(first_hand["power"] == 4, "Armorer's Workshop should boost the selected hand unit by 3")
    require(first_hand["armor"] == 2, "Armorer's Workshop should add 2 armor to the selected hand unit")
    require(final["current_player_id"] == 0, "order action should not advance without explicit end_turn")


def validate_deck_a_special_plain(doc: dict[str, Any]) -> None:
    require(len(doc["steps"]) == 7, "Naglfar case should contain initial + 2 keep + play special + choose target + choose row + choose insert")
    pending = doc["steps"][3]["state"].get("pending_choice")
    require(pending is not None, "Naglfar should request a deck-card pending choice")
    require(len(pending.get("legal_card_targets", [])) == 2, "Naglfar should reveal two FastRng-selected gold candidates")
    row_pending = doc["steps"][4]["state"].get("pending_choice")
    require(row_pending is not None and row_pending.get("kind") == "RowTarget", "selected Naglfar unit should ask for a deploy row")
    insert_pending = doc["steps"][5]["state"].get("pending_choice")
    require(insert_pending is not None and insert_pending.get("kind") == "InsertPosition", "selected Naglfar unit should ask for a dynamic insert position after row selection")
    final = doc["steps"][-1]["state"]
    p0 = final["players"][0]
    require(card_ids(p0["zones"]["cemetery"]) == ["200301"], "Naglfar should resolve to cemetery after the card choice")
    require(p0["zones"]["stay"] == [], "Naglfar should not remain in Stay after finishing")
    melee_ids = card_ids(p0["zones"]["melee"])
    require(melee_ids[:2] == ["201698", "202497"], "chosen FastRng-revealed gold should respect the selected insert position on the own melee row")
    require(card_ids(p0["zones"]["deck"])[0] == "202221", "unplayed revealed gold should move to deck top")
    require(final["current_player_id"] == 0, "resolved Naglfar deck play should wait for explicit end_turn")


CASES = [
    (
        "trace_smoke",
        ["--scenario", "trace_smoke", "--script", "tools/golden/cases/trace_smoke.trace", "--seed", "0", "--starting-player", "0", "--deck", "trace", "--effects", "none"],
        validate_trace_smoke,
    ),
    (
        "deck_a_stratagem_order",
        ["--scenario", "deck_a_stratagem_order", "--script", "tools/golden/cases/deck_a_stratagem_order.trace", "--seed", "0", "--starting-player", "0", "--deck", "deck-a", "--effects", "deck-a"],
        validate_deck_a_stratagem_order,
    ),
    (
        "deck_a_special_plain",
        ["--scenario", "deck_a_special_plain", "--script", "tools/golden/cases/deck_a_special_plain.trace", "--seed", "0", "--starting-player", "0", "--deck", "deck-a", "--effects", "deck-a"],
        validate_deck_a_special_plain,
    ),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True, help="Path to gwent_trace_runner")
    parser.add_argument("--out-dir", type=Path, default=Path("build/golden/cpp"))
    args = parser.parse_args()

    for name, runner_args, validator in CASES:
        doc1 = run_case(args.runner, args.out_dir, name, runner_args)
        doc2 = run_case(args.runner, args.out_dir, name + ".repeat", runner_args)
        validate_trace_document(doc1)
        validate_trace_document(doc2)
        require(doc1 == doc2, f"{name} is not deterministic across repeated runs")
        validator(doc1)
        print(f"OK: {name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - command-line diagnostics
        print(f"golden case failure: {exc}", file=sys.stderr)
        raise SystemExit(1)
