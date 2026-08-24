#!/usr/bin/env python3
"""Run the current Deck A Python/C++ semantic parity audit.

This is intentionally an audit, not a proof of full equivalence.  Cases marked
``expectation=mismatch`` document known gaps and must continue to mismatch until
that gap is fixed and the manifest is updated.  Use ``--strict-equivalence`` to
turn every mismatch into a failure while actively closing gaps.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

DEFAULT_IGNORES = "mulligans_available,cards_drawn_this_round,order_charges,memory,listeners,pending_choice"


def run_to_file(cmd: list[str], path: Path) -> subprocess.CompletedProcess[str]:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stdout_file:
        result = subprocess.run(cmd, text=True, stdout=stdout_file, stderr=subprocess.PIPE, timeout=60)
    if result.returncode != 0:
        # Keep stderr close to the trace name for debugging failed exporters/runners.
        path.with_suffix(path.suffix + ".stderr.txt").write_text(result.stderr or "", encoding="utf-8")
    return result


def compare(here: Path, python_trace: Path, cpp_trace: Path, ignore_key: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(here / "compare_trace.py"),
            str(python_trace),
            str(cpp_trace),
            "--ignore-engine-name",
            "--ignore-checksums",
            "--ignore-actions-results",
            "--ignore-key",
            ignore_key,
        ],
        text=True,
        capture_output=True,
        timeout=60,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp-runner", type=Path, required=True)
    parser.add_argument("--python-core", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=None)
    parser.add_argument("--out-dir", type=Path, default=Path("build/golden/deck_a_full_parity"))
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--starting-player", type=int, default=0)
    parser.add_argument("--ignore-key", default=DEFAULT_IGNORES)
    parser.add_argument("--strict-equivalence", action="store_true", help="Return non-zero if any case mismatches, including documented gaps")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    manifest_path = args.manifest or here / "deck_a_parity_manifest.json"
    manifest: dict[str, Any] = json.loads(manifest_path.read_text(encoding="utf-8"))
    cases = manifest.get("semantic_cases", [])
    if not cases:
        raise SystemExit("manifest has no semantic_cases")

    results: list[dict[str, Any]] = []
    unexpected = 0
    strict_mismatches = 0

    for idx, case in enumerate(cases, start=1):
        name = case["name"]
        print(f"[{idx}/{len(cases)}] running {name}", flush=True)
        expectation = case.get("expectation", "pass")
        script = here / "cases" / f"{name}.trace"
        if not script.exists():
            results.append({"name": name, "expectation": expectation, "actual": "missing_script", "ok": False})
            unexpected += 1
            continue

        case_seed = int(case.get("seed", args.seed))
        case_starting_player = int(case.get("starting_player", args.starting_player))
        cpp_trace = args.out_dir / f"cpp_{name}.json"
        python_trace = args.out_dir / f"python_{name}.json"
        cpp_cmd = [
            str(args.cpp_runner),
            "--scenario", name,
            "--script", str(script),
            "--seed", str(case_seed),
            "--starting-player", str(case_starting_player),
            "--shuffle",
            "--deck", "deck-a",
            "--effects", "deck-a",
        ]
        py_cmd = [
            sys.executable,
            str(here / "python_trace_exporter.py"),
            "--python-core", str(args.python_core),
            "--scenario", name,
            "--script", str(script),
            "--seed", str(case_seed),
            "--starting-player", str(case_starting_player),
            "--deck", "deck-a",
            "--effects", "deck-a",
        ]
        if "pending" in name or bool(case.get("manual_pending", False)):
            py_cmd.append("--manual-pending")

        cpp = run_to_file(cpp_cmd, cpp_trace)
        py = run_to_file(py_cmd, python_trace)
        if cpp.returncode != 0 or py.returncode != 0:
            actual = "tool_error"
            details = (cpp.stderr or "") + (py.stderr or "")
        else:
            diff = compare(here, python_trace, cpp_trace, args.ignore_key)
            actual = "pass" if diff.returncode == 0 else "mismatch"
            details = (diff.stdout + diff.stderr).strip()

        ok = actual == expectation
        if args.strict_equivalence and actual == "mismatch":
            ok = False
            strict_mismatches += 1
        if not ok:
            unexpected += 1
        print(f"[{idx}/{len(cases)}] {name}: expected={expectation} actual={actual} ok={ok}", flush=True)
        results.append(
            {
                "name": name,
                "coverage": case.get("coverage", ""),
                "seed": case_seed,
                "starting_player": case_starting_player,
                "expectation": expectation,
                "actual": actual,
                "ok": ok,
                "details": details.splitlines()[:8],
                "python_trace": str(python_trace),
                "cpp_trace": str(cpp_trace),
            }
        )

    report = {
        "schema_version": "deck-a-parity-audit-result-v1",
        "manifest": str(manifest_path),
        "seed": args.seed,
        "starting_player": args.starting_player,
        "strict_equivalence": args.strict_equivalence,
        "summary": {
            "cases": len(results),
            "passed_expectation": sum(1 for r in results if r["ok"]),
            "unexpected": unexpected,
            "strict_mismatches": strict_mismatches,
            "verdict": "equivalent" if args.strict_equivalence and strict_mismatches == 0 and unexpected == 0 else manifest.get("summary", {}).get("verdict", "not_equivalent_yet"),
        },
        "results": results,
    }

    args.out_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.out_dir / "deck_a_full_parity_audit_report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    for r in results:
        marker = "OK" if r["ok"] else "UNEXPECTED"
        print(f"{marker}: {r['name']} expected={r['expectation']} actual={r['actual']}")
        if r["details"]:
            print("  first detail:", r["details"][0])
    print(f"Wrote audit report -> {report_path}")

    return 0 if unexpected == 0 else 2


if __name__ == "__main__":
    # Some embedded Python/game-core combinations can leave non-daemon cleanup
    # state alive after all subprocess work has finished.  Flush and use
    # os._exit so CTest observes the audit result immediately.
    import os

    code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(code)
