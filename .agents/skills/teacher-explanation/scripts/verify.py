#!/usr/bin/env python3
"""Deterministic verification entry point for the teacher-explanation Skill."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
BACKEND = ROOT / "apps" / "web" / "backend"


def run(*args: str, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, env=env, check=True)


def main() -> int:
    try:
        run(sys.executable, ".agents/skills/teacher-explanation/scripts/check_teacher.py")
        teacher_env = os.environ.copy()
        teacher_env["PYTHONPATH"] = str(ROOT) + (os.pathsep + teacher_env["PYTHONPATH"] if teacher_env.get("PYTHONPATH") else "")
        run(sys.executable, "-m", "pytest", "-q", "services/teacher/tests", env=teacher_env)
        web_env = os.environ.copy()
        web_env["PYTHONPATH"] = str(BACKEND) + (os.pathsep + web_env["PYTHONPATH"] if web_env.get("PYTHONPATH") else "")
        run(sys.executable, "-m", "pytest", "-q", "apps/web/backend/tests/test_teacher_api.py", env=web_env)
        print("PASS teacher-explanation verification")
        return 0
    except subprocess.CalledProcessError as exc:
        print(f"FAIL teacher-explanation verification: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
