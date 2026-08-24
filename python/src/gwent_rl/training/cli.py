from __future__ import annotations

import argparse
import json
import shlex
from pathlib import Path

from .orchestrator import TrainingOrchestrator
from .planner import build_training_plan
from .state import StateStore
from .task import load_training_task


def _print_plan(plan) -> None:
    print(f"Task: {plan.task_name}")
    print(f"Run dir: {plan.run_dir}")
    print(f"Budget: {plan.total_games:,} complete games")
    print(f"Games/update: {plan.games_per_update:,}")
    print(f"Expected updates: {plan.expected_updates}")
    if plan.checkpoint_every_games:
        print(f"Checkpoint cadence: ~{plan.checkpoint_every_games:,} games")
    if plan.eval_every_games:
        print(f"Internal eval cadence: ~{plan.eval_every_games:,} games ({plan.expected_eval_count} planned boundaries)")
    else:
        print("Internal eval cadence: disabled")
    print(f"Training mode: {plan.training_mode}")
    if plan.frozen_opponent_checkpoint:
        print(f"Frozen opponent: {plan.frozen_opponent_checkpoint}")
        if plan.frozen_opponent_checkpoint_sha256:
            print(f"Frozen opponent SHA-256: {plan.frozen_opponent_checkpoint_sha256}")
    print(f"Initialization: {plan.initialization_mode}")
    print(f"Initialization checkpoint: {plan.initialization_checkpoint or 'none'}")
    if plan.initialization_checkpoint_sha256:
        print(f"Initialization SHA-256: {plan.initialization_checkpoint_sha256}")
    if plan.initialization_source_action_grammar is not None:
        print(
            f"Initialization source: schema={plan.initialization_source_schema} "
            f"grammar={plan.initialization_source_action_grammar}"
        )
    print(f"Resume policy: {plan.resume_policy}")
    print(f"Resume checkpoint: {plan.resume_checkpoint or 'none'}")
    print("Checks:")
    for check in plan.checks:
        print(f"  [{check.level.upper():7}] {check.name}: {check.detail}")
    if plan.probes:
        print("Behavioral probes:")
        for name, spec in plan.probes.items():
            print(f"  - {name}: status={spec['status']} every_games={spec['every_games']}")
    print("Command:")
    print("  " + shlex.join(plan.command))
    if plan.environment:
        print("Environment:")
        for key, value in plan.environment.items():
            print(f"  {key}={value}")


def _load_plan(args):
    task_path = Path(args.task)
    task = load_training_task(task_path)
    plan = build_training_plan(
        task,
        task_path,
        project_root=getattr(args, "project_root", None),
        resume_policy=getattr(args, "resume", None),
        library_override=getattr(args, "library", None),
    )
    return task, plan


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Gwent Trainer Agent 的确定性训练任务编排器")
    sub = parser.add_subparsers(dest="command", required=True)

    plan_parser = sub.add_parser("plan", help="只解析任务并打印执行计划，不启动训练")
    plan_parser.add_argument("--task", required=True)
    plan_parser.add_argument("--project-root", default=None)
    plan_parser.add_argument("--resume", choices=["auto", "never", "required"], default=None)
    plan_parser.add_argument("--library", default=None)
    plan_parser.add_argument("--json", action="store_true")

    run_parser = sub.add_parser("run", help="执行任务；训练期间由 Python Orchestrator 接管状态和日志")
    run_parser.add_argument("--task", required=True)
    run_parser.add_argument("--project-root", default=None)
    run_parser.add_argument("--resume", choices=["auto", "never", "required"], default=None)
    run_parser.add_argument("--library", default=None)

    status_parser = sub.add_parser("status", help="读取任务 run_dir 下的 trainer/task_state.json")
    status_parser.add_argument("--task", required=True)
    status_parser.add_argument("--project-root", default=None)

    args = parser.parse_args(argv)

    if args.command == "plan":
        _, plan = _load_plan(args)
        if args.json:
            print(json.dumps(plan.to_dict(), ensure_ascii=False, indent=2))
        else:
            _print_plan(plan)
        raise SystemExit(2 if plan.has_errors else 0)

    if args.command == "run":
        task, plan = _load_plan(args)
        _print_plan(plan)
        orchestrator = TrainingOrchestrator(task, plan)
        raise SystemExit(orchestrator.run())

    task, plan = _load_plan(args)
    state_path = Path(plan.run_dir) / "trainer" / "task_state.json"
    state = StateStore(state_path).load()
    if state is None:
        print(f"No task state yet: {state_path}")
        raise SystemExit(1)
    print(json.dumps(state.to_dict(), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
