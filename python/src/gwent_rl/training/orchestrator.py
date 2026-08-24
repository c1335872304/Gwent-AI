from __future__ import annotations

import json
import os
import shlex
import subprocess
from pathlib import Path
from typing import TextIO

from .planner import PlanCheck, TrainingPlan
from .state import StateStore, TrainingTaskState, utc_now
from .task import TrainingTask


class TrainingOrchestrator:
    """将 Training Task 交给现有 PPO 训练引擎执行。

    关键原则：这里不复制 collector/PPO/eval 逻辑。Orchestrator 是控制面，
    `gwent_rl.train_ppo` 是训练引擎。这样未来替换算法时，任务生命周期仍保持稳定。
    """

    def __init__(self, task: TrainingTask, plan: TrainingPlan):
        self.task = task
        self.plan = plan
        self.run_dir = Path(plan.run_dir)
        self.meta_dir = self.run_dir / "trainer"
        self.state_store = StateStore(self.meta_dir / "task_state.json")

    def prepare(self) -> TrainingTaskState:
        self.meta_dir.mkdir(parents=True, exist_ok=True)
        (self.meta_dir / "task_snapshot.json").write_text(
            json.dumps(self.task.to_dict(), ensure_ascii=False, indent=2), encoding="utf-8"
        )
        self.plan.write_json(self.meta_dir / "plan.json")
        state = self.state_store.load() or TrainingTaskState(
            task_name=self.task.name,
            task_file=self.plan.task_file,
            run_dir=self.plan.run_dir,
        )
        state.status = "PREFLIGHTED"
        state.resume_checkpoint = self.plan.resume_checkpoint
        state.initialization_mode = self.plan.initialization_mode
        state.initialization_checkpoint = self.plan.initialization_checkpoint
        state.message = "训练计划已生成，等待执行。"
        self.state_store.save(state)
        return state

    def run(self) -> int:
        state = self.prepare()
        errors = [c for c in self.plan.checks if c.level == "error"]
        # plan 阶段允许在尚未构建 shared library 时只给 warning；真正 run 前必须把
        # 运行时依赖钉死，避免启动长任务后才发现路径错误。
        command = self.plan.command
        if "--library" in command:
            library_path = Path(command[command.index("--library") + 1])
            if not library_path.exists():
                errors.append(PlanCheck("error", "core_library", f"not found: {library_path}"))
        if self.plan.resume_policy == "never":
            latest = self.run_dir / "checkpoints" / "latest.pt"
            if latest.exists():
                errors.append(
                    PlanCheck(
                        "error",
                        "run_dir_collision",
                        f"resume=never refuses to overwrite existing checkpoint: {latest}",
                    )
                )
        if errors:
            state.status = "FAILED"
            state.finished_at = utc_now()
            state.message = "preflight failed: " + "; ".join(f"{c.name}: {c.detail}" for c in errors)
            self.state_store.save(state)
            raise RuntimeError(state.message)

        env = os.environ.copy()
        # 保留用户已有 PYTHONPATH，同时把当前 checkout 放最前面。
        task_pythonpath = self.plan.environment.get("PYTHONPATH")
        if task_pythonpath:
            previous = env.get("PYTHONPATH", "")
            env["PYTHONPATH"] = task_pythonpath if not previous else task_pythonpath + os.pathsep + previous
        for key, value in self.plan.environment.items():
            if key != "PYTHONPATH":
                env[key] = value

        state.status = "RUNNING"
        state.started_at = state.started_at or utc_now()
        state.finished_at = None
        state.return_code = None
        state.message = "训练子进程运行中。"
        self.state_store.save(state)

        log_path = self.meta_dir / "orchestrator.log"
        with log_path.open("a", encoding="utf-8", buffering=1) as log:
            self._write_header(log)
            try:
                process = subprocess.Popen(
                    self.plan.command,
                    cwd=self.plan.project_root,
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                )
                state.pid = process.pid
                self.state_store.save(state)
                assert process.stdout is not None
                for line in process.stdout:
                    print(line, end="")
                    log.write(line)
                return_code = process.wait()
            except KeyboardInterrupt:
                state.status = "INTERRUPTED"
                state.finished_at = utc_now()
                state.message = "收到人工中断；可根据 latest.pt 使用 auto resume 继续。"
                self._refresh_latest_checkpoint(state)
                self.state_store.save(state)
                raise
            except Exception as exc:
                state.status = "FAILED"
                state.finished_at = utc_now()
                state.message = f"orchestrator exception: {exc}"
                self._refresh_latest_checkpoint(state)
                self.state_store.save(state)
                raise

        state.return_code = return_code
        state.finished_at = utc_now()
        self._refresh_latest_checkpoint(state)
        if return_code == 0:
            state.status = "COMPLETED"
            state.message = "底层训练进程正常完成。Behavioral probe / 外部 promotion 仍需按任务约定单独评估。"
        else:
            state.status = "FAILED"
            state.message = f"底层训练进程退出码={return_code}；保留 checkpoint 供诊断或恢复。"
        self.state_store.save(state)
        return return_code

    def _refresh_latest_checkpoint(self, state: TrainingTaskState) -> None:
        latest = self.run_dir / "checkpoints" / "latest.pt"
        state.latest_checkpoint = str(latest) if latest.exists() else None

    def _write_header(self, log: TextIO) -> None:
        log.write("\n=== Trainer Orchestrator Run ===\n")
        log.write(f"task={self.task.name}\n")
        log.write(f"command={shlex.join(self.plan.command)}\n")
        for key, value in self.plan.environment.items():
            log.write(f"env.{key}={value}\n")
        log.write("================================\n")
