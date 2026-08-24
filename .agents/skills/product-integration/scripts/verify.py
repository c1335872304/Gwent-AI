#!/usr/bin/env python3
"""Deterministic verification entry point for the product-integration Skill."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
BACKEND = ROOT / "apps" / "web" / "backend"
FRONTEND = ROOT / "apps" / "web" / "frontend"


def run(*args: str, cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=cwd, env=env, check=True)


def static_contract_check() -> None:
    server = (ROOT / "tools/server/human_vs_ai.py").read_text(encoding="utf-8")
    model = (BACKEND / "app/models/core_contract.py").read_text(encoding="utf-8")
    types = (FRONTEND / "src/types/game.ts").read_text(encoding="utf-8")
    ui = (FRONTEND / "src/utils/gameUi.ts").read_text(encoding="utf-8")

    checks = {
        "Core adapter exposes api_version": '"api_version": CORE_API_VERSION' in server,
        "BFF rejects unknown contract fields": 'extra="forbid"' in model,
        "insert_position is typed end-to-end": "insert_position: int" in model and "insert_position: number" in types,
        "frontend does not parse legacy target text": all(
            token not in ui for token in ("target.match", "/Melee/i", "/Ranged/i", "/P1/i", "entityIdFromText")
        ),
    }
    failed = [name for name, ok in checks.items() if not ok]
    if failed:
        raise RuntimeError("; ".join(failed))
    print("PASS product static contract check")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frontend", action="store_true", help="also run npm build")
    args = parser.parse_args()
    try:
        static_contract_check()
        env = os.environ.copy()
        env["PYTHONPATH"] = str(BACKEND) + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
        run(sys.executable, "-m", "pytest", "-q", "apps/web/backend/tests", env=env)
        run(sys.executable, "-m", "compileall", "-q", "apps/web/backend/app")
        if args.frontend:
            run("npm", "run", "build", cwd=FRONTEND)
        print("PASS product-integration verification")
        return 0
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"FAIL product-integration verification: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
