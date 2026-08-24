from __future__ import annotations

import json
import os
import tempfile
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


STATE_SCHEMA_VERSION = 1


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


@dataclass
class TrainingTaskState:
    schema_version: int = STATE_SCHEMA_VERSION
    task_name: str = ""
    status: str = "CREATED"
    task_file: str = ""
    run_dir: str = ""
    started_at: str | None = None
    updated_at: str | None = None
    finished_at: str | None = None
    pid: int | None = None
    return_code: int | None = None
    resume_checkpoint: str | None = None
    initialization_mode: str = "random"
    initialization_checkpoint: str | None = None
    latest_checkpoint: str | None = None
    message: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


class StateStore:
    """训练任务状态的原子 JSON 存储。

    Orchestrator 进程崩溃时，已有 checkpoint 仍由底层训练器保证；这个文件用于让
    Trainer/人工知道上一次执行停在哪个阶段。
    """

    def __init__(self, path: Path):
        self.path = path

    def load(self) -> TrainingTaskState | None:
        if not self.path.exists():
            return None
        raw = json.loads(self.path.read_text(encoding="utf-8"))
        return TrainingTaskState(**raw)

    def save(self, state: TrainingTaskState) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        state.updated_at = utc_now()
        payload = json.dumps(state.to_dict(), ensure_ascii=False, indent=2, sort_keys=True)
        fd, tmp_name = tempfile.mkstemp(prefix=self.path.name + ".", dir=self.path.parent)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                handle.write(payload)
                handle.write("\n")
            os.replace(tmp_name, self.path)
        finally:
            if os.path.exists(tmp_name):
                os.unlink(tmp_name)
