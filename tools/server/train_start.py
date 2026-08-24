#!/usr/bin/env python3
"""Start one validated Training Task inside tmux on a Linux server.

The task file owns training/runtime settings. This launcher intentionally does
not duplicate schema versions, collector thread counts, run directories, or
machine-specific Conda paths.

Examples:

    python tools/server/train_start.py --task training/tasks/smoke.yaml
    GWENT_TASK=training/tasks/my_run.yaml python tools/server/train_start.py

Optional environment overrides:

    GWENT_TASK, GWENT_CORE_LIBRARY, GWENT_PYTHON, GWENT_TMUX_SESSION
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shlex
import shutil
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LIBRARY = ROOT / "build-release" / "libgwent_core.so"


def resolve_project_path(value: str | Path) -> Path:
    path = Path(value).expanduser()
    return path.resolve() if path.is_absolute() else (ROOT / path).resolve()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--task",
        default=os.environ.get("GWENT_TASK"),
        help="training/tasks/*.yaml (or set GWENT_TASK)",
    )
    parser.add_argument(
        "--library",
        default=os.environ.get("GWENT_CORE_LIBRARY", str(DEFAULT_LIBRARY)),
        help="shared core library (or set GWENT_CORE_LIBRARY)",
    )
    parser.add_argument(
        "--python",
        dest="python_executable",
        default=os.environ.get("GWENT_PYTHON", sys.executable),
        help="Python interpreter used inside tmux (or set GWENT_PYTHON)",
    )
    parser.add_argument(
        "--session",
        default=os.environ.get("GWENT_TMUX_SESSION"),
        help="tmux session name; defaults to gwent-<task-name>",
    )
    args = parser.parse_args()
    if not args.task:
        parser.error("--task is required unless GWENT_TASK is set")
    return args


def session_name_for(task: Path, requested: str | None) -> str:
    if requested:
        return requested
    stem = re.sub(r"[^A-Za-z0-9_.-]+", "-", task.stem).strip("-.") or "training"
    return f"gwent-{stem}"


def check_file(path: Path, name: str) -> None:
    if not path.is_file():
        raise RuntimeError(f"找不到{name}: {path}")


def check_command(command: str) -> None:
    if shutil.which(command) is None:
        raise RuntimeError(f"找不到命令: {command}")


def tmux_session_exists(session: str) -> bool:
    result = subprocess.run(
        ["tmux", "has-session", "-t", session],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0


def build_environment(library: Path) -> dict[str, str]:
    env = os.environ.copy()
    python_src = str(ROOT / "python" / "src")
    old_pythonpath = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = python_src + (os.pathsep + old_pythonpath if old_pythonpath else "")
    env["GWENT_CORE_LIBRARY"] = str(library)
    return env


def run_plan(python_executable: Path, task: Path, library: Path, env: dict[str, str]) -> None:
    print("\n" + "=" * 60)
    print("检查训练计划")
    print("=" * 60)
    command = [
        str(python_executable),
        "-m",
        "gwent_rl.training.cli",
        "plan",
        "--task",
        str(task),
        "--library",
        str(library),
    ]
    print("Python :", python_executable)
    print("Task   :", task)
    print("Library:", library)
    result = subprocess.run(command, cwd=ROOT, env=env, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"Training plan 检查失败 (exit={result.returncode})")
    print("[OK] Training plan 检查通过。")


def start_training(
    *,
    python_executable: Path,
    task: Path,
    library: Path,
    session: str,
) -> None:
    python_src = ROOT / "python" / "src"
    shell_command = "\n".join(
        [
            "set -e",
            f"cd {shlex.quote(str(ROOT))}",
            f"export PYTHONPATH={shlex.quote(str(python_src))}:\"${{PYTHONPATH:-}}\"",
            f"export GWENT_CORE_LIBRARY={shlex.quote(str(library))}",
            "echo '========================================'",
            "echo 'Gwent training started'",
            "echo \"Host: $(hostname)\"",
            "echo \"Project: $(pwd)\"",
            f"echo 'Python: {shlex.quote(str(python_executable))}'",
            f"echo 'Task: {shlex.quote(str(task))}'",
            "echo '========================================'",
            f"exec {shlex.quote(str(python_executable))} -m gwent_rl.training.cli run "
            f"--task {shlex.quote(str(task))} --library {shlex.quote(str(library))}",
        ]
    )
    subprocess.run(["tmux", "new-session", "-d", "-s", session, shell_command], check=True)


def main() -> int:
    try:
        args = parse_args()
        task = resolve_project_path(args.task)
        library = resolve_project_path(args.library)
        python_executable = Path(args.python_executable).expanduser().resolve()
        session = session_name_for(task, args.session)

        print("=" * 60)
        print("Gwent Training Launcher")
        print("=" * 60)
        print("Host       :", socket.gethostname())
        print("System     :", platform.system())
        print("Project    :", ROOT)
        print("Python     :", python_executable)
        print("Task       :", task)
        print("Library    :", library)
        print("tmux       :", session)

        if platform.system() != "Linux":
            raise RuntimeError("tools/server/train_start.py 仅支持 Linux 服务器")

        check_file(task, "训练任务")
        check_file(library, "C++ Core")
        check_file(python_executable, "Python interpreter")
        check_command("tmux")

        if tmux_session_exists(session):
            print(f"[WARNING] tmux session 已存在: {session}")
            print(f"查看训练: tmux attach -t {shlex.quote(session)}")
            return 0

        env = build_environment(library)
        run_plan(python_executable, task, library, env)

        print("\n" + "=" * 60)
        print("启动训练")
        print("=" * 60)
        start_training(
            python_executable=python_executable,
            task=task,
            library=library,
            session=session,
        )

        if not tmux_session_exists(session):
            raise RuntimeError("tmux 会话创建后立即消失；训练可能启动失败")

        print("[OK] 训练已启动")
        print("tmux session:", session)
        print(f"查看训练: tmux attach -t {shlex.quote(session)}")
        return 0
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
