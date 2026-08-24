"""Trainer Agent 的确定性训练任务执行层。

LLM/Codex 负责理解问题、设计任务和分析结果；本包负责把已经确定的训练任务
转换成可复现的命令、状态和服务器执行流程。
"""

from .task import TrainingTask, load_training_task
from .planner import TrainingPlan, build_training_plan

__all__ = ["TrainingTask", "TrainingPlan", "load_training_task", "build_training_plan"]
