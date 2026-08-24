#!/usr/bin/env python3
"""Compare normalized legal-action surfaces between the Python and C++ cores.

This audit intentionally compares a normalized surface rather than each engine's
raw decision representation.  Python exposes a hierarchical decision tree
(TURN -> PLAY -> CARD -> LOCATION), while C++ exposes atomic actions.  The
surface expands Python play choices and sorts actions so that representation
shape does not create false mismatches.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

CASES = [
    {"name": "deck_a_semantic_named_hand_play", "seed": 0, "coverage": "initial hand play legal surface"},
    {"name": "deck_a_semantic_dettlaff_row_choice_fixed", "seed": 0, "coverage": "pending row target legal surface", "manual_pending": True},
    {"name": "deck_a_semantic_naglfar_named_deck_choice", "seed": 0, "coverage": "nested deck-play card and row choices", "manual_pending": True},
    {"name": "deck_a_semantic_geels_named_deck_choice", "seed": 2, "coverage": "Geels nested deck-play card and row choices", "manual_pending": True},
    {"name": "deck_a_semantic_queen_melee_deploy_fixed", "seed": 2, "coverage": "Queen melee deploy target surface", "manual_pending": True},
    {"name": "deck_a_semantic_queen_ranged_purify_fixed", "seed": 2, "coverage": "Queen ranged purify target surface", "manual_pending": True},
    {"name": "deck_a_semantic_imlerith_melee_discard_fixed", "seed": 8, "coverage": "Imlerith all-hand discard target surface", "manual_pending": True},
    {"name": "deck_a_semantic_riptide_melee_clash_fixed", "seed": 1, "coverage": "Riptide melee deploy/clash legal surface"},
    {"name": "deck_a_semantic_centipede", "seed": 3, "coverage": "Giant Centipede deploy legal surface"},
    {"name": "deck_a_semantic_verena", "seed": 1, "coverage": "Verena target legal surface", "manual_pending": True},
    {"name": "deck_a_semantic_elder", "seed": 1, "coverage": "Unseen Elder target legal surface", "manual_pending": True},
    {
        "name": "deck_a_semantic_blood_scent_leader_fixed",
        "seed": 0,
        "coverage": "Blood Scent post-leader legal surface",
        "manual_pending": True,
    },
    {
        "name": "deck_a_semantic_garkain_order_cooldown_fixed",
        "seed": 0,
        "coverage": "Garkain post-order legal surface",
        "manual_pending": True,
    },
    {"name": "deck_a_semantic_bloodscented_predator", "seed": 2, "coverage": "Predator pending target legal surface", "manual_pending": True},
]


def run_to_json(cmd: list[str], out_path: Path) -> dict[str, Any]:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(cmd, text=True, capture_output=True)
    if result.returncode != 0:
        out_path.with_suffix(out_path.suffix + ".stderr.txt").write_text(result.stderr, encoding="utf-8")
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(cmd)}\n{result.stderr}")
    out_path.write_text(result.stdout, encoding="utf-8")
    return json.loads(result.stdout)


def surfaces(doc: dict[str, Any]) -> list[dict[str, Any]]:
    result = []
    for step in doc.get("steps", []):
        result.append({
            "step_index": step.get("step_index"),
            "label": step.get("label"),
            "legal_surface": step.get("legal_surface"),
        })
    return result


def compare_surfaces(py_doc: dict[str, Any], cpp_doc: dict[str, Any]) -> tuple[bool, list[str]]:
    py = surfaces(py_doc)
    cpp = surfaces(cpp_doc)
    details: list[str] = []
    if len(py) != len(cpp):
        details.append(f"step count differs: python={len(py)} cpp={len(cpp)}")
        return False, details
    ok = True
    for p_step, c_step in zip(py, cpp):
        if p_step["label"] != c_step["label"]:
            ok = False
            details.append(f"label differs at step {p_step['step_index']}: py={p_step['label']!r} cpp={c_step['label']!r}")
            continue
        if p_step["legal_surface"] != c_step["legal_surface"]:
            ok = False
            details.append(f"legal surface mismatch at step {p_step['step_index']} {p_step['label']!r}")
            details.append(f"  python={p_step['legal_surface']}")
            details.append(f"  cpp={c_step['legal_surface']}")
    return ok, details


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp-runner", type=Path, required=True)
    parser.add_argument("--python-core", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, default=Path("build/golden/legal_actions_parity"))
    parser.add_argument("--case", action="append", help="Run only selected case(s); may be repeated")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    selected = set(args.case or [])
    cases = [case for case in CASES if not selected or case["name"] in selected]
    if selected and len(cases) != len(selected):
        known = {case["name"] for case in CASES}
        unknown = selected - known
        raise SystemExit(f"unknown legal parity case(s): {sorted(unknown)}")

    results = []
    failures = 0
    for case in cases:
        name = case["name"]
        seed = int(case.get("seed", 0))
        script = here / "cases" / f"{name}.trace"
        cpp_path = args.out_dir / f"cpp_{name}.json"
        py_path = args.out_dir / f"python_{name}.json"
        cpp_doc = run_to_json([
            str(args.cpp_runner),
            "--scenario", name,
            "--script", str(script),
            "--seed", str(seed),
            "--starting-player", "0",
            "--shuffle",
            "--deck", "deck-a",
            "--effects", "deck-a",
            "--include-legal-surface",
        ], cpp_path)
        py_cmd = [
            sys.executable,
            str(here / "python_trace_exporter.py"),
            "--python-core", str(args.python_core),
            "--scenario", name,
            "--script", str(script),
            "--seed", str(seed),
            "--starting-player", "0",
            "--deck", "deck-a",
            "--effects", "deck-a",
            "--include-legal-surface",
        ]
        if "pending" in name or bool(case.get("manual_pending", False)):
            py_cmd.append("--manual-pending")
        py_doc = run_to_json(py_cmd, py_path)
        surfaces_equal, details = compare_surfaces(py_doc, cpp_doc)
        actual = "pass" if surfaces_equal else "mismatch"
        expectation = case.get("expectation", "pass")
        ok = actual == expectation
        if not ok:
            failures += 1
        results.append({
            "name": name,
            "seed": seed,
            "coverage": case.get("coverage", ""),
            "expectation": expectation,
            "actual": actual,
            "known_reason": case.get("known_reason", ""),
            "ok": ok,
            "details": details,
            "python_trace": str(py_path),
            "cpp_trace": str(cpp_path),
        })
        print(("OK" if ok else "UNEXPECTED") + f": {name} expected={expectation} actual={actual}")
        if case.get("known_reason"):
            print("  known_reason: " + case["known_reason"])
        for line in details[:6]:
            print("  " + line)

    report = {
        "schema_version": "deck-a-legal-actions-parity-v1",
        "summary": {
            "cases": len(results),
            "passed_expectation": sum(1 for item in results if item["ok"]),
            "unexpected": failures,
            "verdict": "legal_surface_audit_passed_expectations" if failures == 0 else "legal_surface_unexpected_mismatch",
        },
        "results": results,
    }
    args.out_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.out_dir / "legal_actions_parity_report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Wrote legal-actions parity report -> {report_path}")
    return 0 if failures == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
