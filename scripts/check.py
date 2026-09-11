#!/usr/bin/env python3
"""Single project validation entry point.

Usage:
  python scripts/check.py quick   # metadata/docs/schema/training definitions
  python scripts/check.py test    # quick + focused C/Python regression tests
  python scripts/check.py full    # quick + full C/Python test suites
  python scripts/check.py train   # quick + shortest collector/PPO smoke
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11
    import tomli as tomllib
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
PYTHON_SRC = ROOT / "python" / "src"


def run(*args: str, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, env=env, check=True)


def check_agent_package() -> None:
    required = {
        ".codex/agents/core.toml",
        ".codex/agents/trainer.toml",
        ".codex/agents/product.toml",
        ".codex/agents/teacher.toml",
        ".agents/skills/core-environment/SKILL.md",
        ".agents/skills/training-config/SKILL.md",
        ".agents/skills/product-integration/SKILL.md",
        ".agents/skills/teacher-explanation/SKILL.md",
        ".agents/skills/teacher-explanation/references/EVIDENCE_CONTRACT.md",
        ".agents/skills/teacher-explanation/references/PRIVACY_BOUNDARY.md",
        ".agents/skills/teacher-explanation/scripts/check_teacher.py",
        ".agents/skills/core-environment/references/CARD_EXTENSION.md",
        ".agents/skills/core-environment/references/SCHEMA_CONTRACT.md",
        ".agents/skills/core-environment/scripts/check_schema.py",
        ".agents/skills/training-config/scripts/validate_training.py",
        "tools/server",
        "apps/web/frontend/src",
        "apps/web/backend/app",
        "services/teacher/agent.py",
        "services/teacher/evidence.py",
        "services/teacher/api.py",
        "services/teacher/README.md",
        "docs/current/TEACHER_AND_WEB.md",
        "docs/current/TRAINING_AND_MODEL.md",
        "models/v3/README.md",
        "scripts/install_model.py",
        "scripts/prepare_warmstart_source.py",
        "scripts/prepare_learner_vs_frozen_task.py",
        "docs/current/CORE_CONTRACTS.md",
                "training/tasks/insert_position_v1_warmstart_smoke.yaml",
        "training/tasks/insert_position_v1_warmstart_50k.yaml",
        "runs",
    }
    missing = sorted(path for path in required if not (ROOT / path).exists())
    if missing:
        raise RuntimeError("missing required project paths: " + ", ".join(missing))

    agents = {}
    for name in ("core", "trainer", "product", "teacher"):
        data = tomllib.loads((ROOT / ".codex" / "agents" / f"{name}.toml").read_text(encoding="utf-8"))
        if data.get("name") != name:
            raise RuntimeError(f"agent {name} has invalid name={data.get('name')!r}")
        agents[name] = data

    skills = {p.name for p in (ROOT / ".agents" / "skills").iterdir() if p.is_dir()}
    expected_skills = {"core-environment", "training-config", "product-integration", "teacher-explanation"}
    if skills != expected_skills:
        raise RuntimeError(f"expected exactly four skills, got {sorted(skills)}")
    print("PASS agent package: 4 agents / 4 skills")


def check_docs() -> None:
    link_re = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
    skip = {"build", "build-release", "build-debug", ".build", ".git", "node_modules", "dist", ".venv", "__pycache__"}
    broken: list[str] = []
    checked = 0
    for md in ROOT.rglob("*.md"):
        if any(part in skip or part.startswith("build-") for part in md.parts):
            continue
        if (ROOT / "docs" / "history") in md.parents:
            continue
        text = md.read_text(encoding="utf-8", errors="replace")
        for raw in link_re.findall(text):
            target = raw.strip().split(maxsplit=1)[0].strip("<>")
            if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = unquote(target.split("#", 1)[0])
            if not target:
                continue
            checked += 1
            if not (md.parent / target).resolve().exists():
                broken.append(f"{md.relative_to(ROOT)} -> {raw}")
    if broken:
        raise RuntimeError("broken markdown links:\n  " + "\n  ".join(broken[:30]))
    print(f"PASS docs: {checked} local links")



def check_python_tool_syntax() -> None:
    """Parse maintained Python entry-point/tool files without creating __pycache__."""
    roots = [
        ROOT / "scripts",
        ROOT / "tools" / "codegen",
        ROOT / "tools" / "server",
        ROOT / ".agents" / "skills" / "core-environment" / "scripts",
        ROOT / ".agents" / "skills" / "training-config" / "scripts",
        ROOT / ".agents" / "skills" / "teacher-explanation" / "scripts",
        ROOT / "apps" / "web" / "backend" / "app",
        ROOT / "apps" / "web" / "tools",
        ROOT / "services" / "teacher",
        ROOT / "tools" / "packaging",
    ]
    checked = 0
    for base in roots:
        if not base.exists():
            continue
        for path in sorted(base.rglob("*.py")):
            source = path.read_text(encoding="utf-8", errors="strict")
            try:
                compile(source, str(path), "exec")
            except SyntaxError as exc:
                raise RuntimeError(
                    f"Python syntax error: {path.relative_to(ROOT)}:{exc.lineno}: {exc.msg}"
                ) from exc
            checked += 1
    print(f"PASS Python tool syntax: {checked} files")

def check_product_contract() -> None:
    server = (ROOT / "tools" / "server" / "human_vs_ai.py").read_text(encoding="utf-8")
    game_types = (ROOT / "apps" / "web" / "frontend" / "src" / "types" / "game.ts").read_text(encoding="utf-8")
    game_ui = (ROOT / "apps" / "web" / "frontend" / "src" / "utils" / "gameUi.ts").read_text(encoding="utf-8")
    board_row = (ROOT / "apps" / "web" / "frontend" / "src" / "components" / "BoardRow.tsx").read_text(encoding="utf-8")
    backend_contract = (ROOT / "apps" / "web" / "backend" / "app" / "models" / "core_contract.py").read_text(encoding="utf-8")
    package_json = (ROOT / "apps" / "web" / "frontend" / "package.json").read_text(encoding="utf-8")

    required = {
        "server option_insert_positions": "option_insert_positions" in server,
        "server prefix_insert_positions": "prefix_insert_positions" in server,
        "HTTP api_version": '"api_version": CORE_API_VERSION' in server,
        "lossless stable_hash": '"stable_hash": str(int(' in server,
        "frontend required insert_position": "insert_position: number" in game_types and "insert_position?:" not in game_types,
        "frontend insertion slots": "insert-slot" in board_row,
        "backend strict contract": 'extra="forbid"' in backend_contract,
        "backend typed insert_position": "insert_position: int" in backend_contract,
        "frontend has no legacy target parser": all(token not in game_ui for token in ("target.match", "/Melee/i", "/Ranged/i", "/P1/i", "entityIdFromText")),
        "frontend dependencies pinned": '"latest"' not in package_json,
    }
    missing = [name for name, ok in required.items() if not ok]
    if missing:
        raise RuntimeError("product/core contract hardening incomplete: " + ", ".join(missing))
    print("PASS product contract: typed, versioned, no legacy parsing")


def check_local_model_slot() -> None:
    server = (ROOT / "tools" / "server" / "human_vs_ai.py").read_text(encoding="utf-8")
    gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
    model_readme = (ROOT / "models" / "v3" / "README.md").read_text(encoding="utf-8")
    installer = (ROOT / "scripts" / "install_model.py").read_text(encoding="utf-8")

    checks = {
        "canonical local model default": 'models/v3/policy.pt' in server,
        "model binary excluded from source tree": 'models/**/*.pt' in gitignore,
        "no fake shipped checkpoint": not (ROOT / "models" / "v3" / "policy.pt").exists(),
        "model slot documented": 'models/v3/policy.pt' in model_readme,
        "installer uses production loader": 'policy_from_checkpoint' in installer,
        "installer uses atomic replace": 'os.replace' in installer,
    }
    failed = [name for name, ok in checks.items() if not ok]
    if failed:
        raise RuntimeError("local model slot incomplete: " + ", ".join(failed))
    print("PASS local model slot: validated, no fake checkpoint")


def quick() -> None:
    check_agent_package()
    check_docs()
    check_python_tool_syntax()
    check_product_contract()
    check_local_model_slot()
    run(sys.executable, "tools/codegen/generate_card_data.py", "--check")
    run(sys.executable, "tools/codegen/validate_card_data.py")
    run(sys.executable, ".agents/skills/core-environment/scripts/check_schema.py")
    run(sys.executable, ".agents/skills/training-config/scripts/validate_training.py", "--all")


def build(mode: str) -> tuple[Path, dict[str, str]]:
    build_dir = ROOT / ".build" / mode
    jobs = os.environ.get("GWENT_JOBS", str(os.cpu_count() or 4))
    run(
        "cmake", "-S", ".", "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIBS=ON", "-DGWENT_BUILD_TESTS=ON",
        f"-DGWENT_BUILD_TRACE_TOOLS={'ON' if mode == 'full' else 'OFF'}",
    )
    env = os.environ.copy()
    env["PYTHONPATH"] = str(PYTHON_SRC) + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    lib = build_dir / ("gwent_core.dll" if os.name == "nt" else "libgwent_core.so")
    if sys.platform == "darwin":
        lib = build_dir / "libgwent_core.dylib"
    env["GWENT_CORE_LIBRARY"] = str(lib)
    env["GWENT_JOBS"] = jobs
    return build_dir, env


def focused_tests() -> None:
    build_dir, env = build("test")
    jobs = env["GWENT_JOBS"]
    targets = [
        "gwent_core_model_tests", "gwent_legal_actions_tests", "gwent_c_api_smoke_tests",
        "gwent_rl_c_api_tests", "gwent_rl_collector_c_api_tests",
    ]
    run("cmake", "--build", str(build_dir), "-j", jobs, "--target", *targets)
    run("ctest", "--test-dir", str(build_dir), "--output-on-failure", "-R",
        "gwent_core_model_tests|gwent_legal_actions_tests|gwent_c_api_smoke_tests|gwent_rl_c_api_tests|gwent_rl_collector_c_api_tests")
    run(
        sys.executable, "-m", "pytest", "-q",
        "python/tests/test_ctypes_collector.py",
        "python/tests/test_training_loop.py",
        "python/tests/test_training_initialization.py",
        "python/tests/test_training_orchestrator.py",
        "python/tests/test_evaluation_budget.py",
        env=env,
    )
    web_env = env.copy()
    web_env["PYTHONPATH"] = str(ROOT / "apps" / "web" / "backend") + os.pathsep + web_env.get("PYTHONPATH", "")
    run(sys.executable, "-m", "pytest", "-q", "apps/web/backend/tests", env=web_env)
    teacher_env = env.copy()
    teacher_env["PYTHONPATH"] = str(ROOT) + os.pathsep + teacher_env.get("PYTHONPATH", "")
    run(sys.executable, "-m", "pytest", "-q", "services/teacher/tests", env=teacher_env)


def full_tests() -> None:
    build_dir, env = build("full")
    run("cmake", "--build", str(build_dir), "-j", env["GWENT_JOBS"])
    run("ctest", "--test-dir", str(build_dir), "--output-on-failure")
    run(sys.executable, "-m", "pytest", "-q", "python/tests", env=env)
    web_env = env.copy()
    web_env["PYTHONPATH"] = str(ROOT / "apps" / "web" / "backend") + os.pathsep + web_env.get("PYTHONPATH", "")
    run(sys.executable, "-m", "pytest", "-q", "apps/web/backend/tests", env=web_env)
    teacher_env = env.copy()
    teacher_env["PYTHONPATH"] = str(ROOT) + os.pathsep + teacher_env.get("PYTHONPATH", "")
    run(sys.executable, "-m", "pytest", "-q", "services/teacher/tests", env=teacher_env)


def train_smoke() -> None:
    build_dir, env = build("train")
    run("cmake", "--build", str(build_dir), "-j", env["GWENT_JOBS"])
    lib = env["GWENT_CORE_LIBRARY"]
    run(sys.executable, "python/tests/smoke_ctypes_collector.py", "--num-envs", "4", "--rounds", "4", "--library", lib, env=env)
    run(sys.executable, "python/tests/smoke_training_loop.py", "--library", lib, env=env)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("quick", "test", "full", "train"), nargs="?", default="quick")
    args = parser.parse_args()
    try:
        quick()
        if args.mode == "test":
            focused_tests()
        elif args.mode == "full":
            full_tests()
        elif args.mode == "train":
            train_smoke()
        print(f"PASS project check: {args.mode}")
        return 0
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"FAIL project check: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
