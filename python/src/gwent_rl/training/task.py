from __future__ import annotations

from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Mapping

from gwent_rl.config import load_mapping_file
from gwent_rl.schema import ACTION_GRAMMAR_VERSION, DECISION_KIND_NAMES, OPTION_KIND_NAMES, SCHEMA_VERSION


TASK_SCHEMA_VERSION = 3


@dataclass
class AlgorithmSpec:
    """底层训练算法入口。Task 不复制 PPO 参数，只引用现有实验配置。"""

    config: str = "configs/training/ppo_128env_strategic.yaml"


@dataclass
class CompatibilitySpec:
    """任务启动前必须满足的目标环境契约。

    默认跟随当前环境；只有明确需要版本钉死的历史/迁移任务才在 YAML 中写出版本。
    """

    observation_schema: int = SCHEMA_VERSION
    action_grammar: int = ACTION_GRAMMAR_VERSION


@dataclass
class InitializationSpec:
    """首次启动任务时的模型初始化策略。

    `random` 表示从当前代码随机初始化；`warm_start` 表示只迁移模型知识，
    不继承 optimizer/update/game counter。后续如果 run_dir 已存在 latest.pt，
    resume 永远优先于首次 warm start。
    """

    mode: str = "random"  # random | warm_start
    checkpoint: str = ""
    checkpoint_sha256: str = ""
    source_observation_schema: int = 0
    source_action_grammar: int = 0
    reset_optimizer: bool = True
    reset_decision_embeddings: list[str] = field(default_factory=list)
    reset_option_embeddings: list[str] = field(default_factory=list)


@dataclass
class BudgetSpec:
    """明确的成本边界。长期训练必须有完整对局预算。"""

    total_games: int = 0
    games_per_update: int = 0


@dataclass
class RuntimeSpec:
    """服务器资源与吞吐参数；与算法超参数分离。"""

    num_envs: int = 128
    max_batch_size: int = 128
    minibatch_size: int = 2048
    collector_threads: int = 0
    device: str = "auto"


@dataclass
class TrainingControlSpec:
    """Policy ownership for data collection.

    ``self_play`` keeps the historical behavior: one trainable policy controls
    both actors. ``learner_vs_frozen`` is deck-agnostic asymmetric training:
    only decisions made by the learner's deck are optimized, while the
    opponent policy is loaded from a pinned current-contract checkpoint and
    never updated. Player seats alternate by PPO update to avoid seat bias.
    """

    mode: str = "self_play"  # self_play | learner_vs_frozen
    learner_deck: str = ""
    opponent_deck: str = ""
    frozen_checkpoint: str = ""
    frozen_checkpoint_sha256: str = ""
    side_schedule: str = "alternate"


@dataclass
class CheckpointSpec:
    """归档 checkpoint 的频率，单位是 PPO update/generation。"""

    interval_updates: int = 4


@dataclass
class EvaluationSpec:
    """训练过程内部 current-vs-best 的受控评估。"""

    enabled: bool = True
    every_updates: int = 4
    games: int = 512
    num_envs: int = 128
    internal_promote_win_rate: float = 0.55


@dataclass
class ExecutionSpec:
    """一次训练任务的运行位置和恢复策略。"""

    run_dir: str = "runs/tasks/unnamed"
    library: str = "build-release/libgwent_core.so"
    resume: str = "auto"  # auto | never | required


@dataclass
class ProbeSpec:
    """Behavioral probe 预留契约。

    当前 Orchestrator 只把 probe 纳入计划和结果元数据，不会伪装成已经实现。
    后续可为不同 probe 注册确定性执行器。
    """

    status: str = "reserved"  # reserved | enabled | disabled
    every_games: int = 0
    description: str = ""


@dataclass
class TrainingTask:
    """Trainer Agent 与 Python 执行层之间的正式任务契约。"""

    schema_version: int = TASK_SCHEMA_VERSION
    name: str = "unnamed"
    status: str = "draft"  # draft | ready | retired
    description: str = ""
    algorithm: AlgorithmSpec = field(default_factory=AlgorithmSpec)
    compatibility: CompatibilitySpec = field(default_factory=CompatibilitySpec)
    initialization: InitializationSpec = field(default_factory=InitializationSpec)
    budget: BudgetSpec = field(default_factory=BudgetSpec)
    runtime: RuntimeSpec = field(default_factory=RuntimeSpec)
    training_control: TrainingControlSpec = field(default_factory=TrainingControlSpec)
    checkpoint: CheckpointSpec = field(default_factory=CheckpointSpec)
    evaluation: EvaluationSpec = field(default_factory=EvaluationSpec)
    execution: ExecutionSpec = field(default_factory=ExecutionSpec)
    probes: dict[str, ProbeSpec] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def _mapping(value: Any, name: str) -> Mapping[str, Any]:
    if value is None:
        return {}
    if not isinstance(value, Mapping):
        raise ValueError(f"{name} must be a mapping")
    return value


def _strict_dataclass(cls, raw: Mapping[str, Any], section: str):
    allowed = set(cls.__dataclass_fields__)
    unknown = sorted(set(raw) - allowed)
    if unknown:
        raise ValueError(f"unknown keys in {section}: {', '.join(unknown)}")
    return cls(**dict(raw))



def _parse_initialization(raw: Mapping[str, Any]) -> InitializationSpec:
    """解析初始化配置。简易 YAML 目前不支持原生列表，因此允许逗号分隔名称。"""

    values = dict(raw)
    for key in ("reset_decision_embeddings", "reset_option_embeddings"):
        value = values.get(key, [])
        if isinstance(value, str):
            values[key] = [item.strip() for item in value.split(",") if item.strip()]
        elif value is None:
            values[key] = []
        elif not isinstance(value, list):
            raise ValueError(f"initialization.{key} must be a list or comma-separated string")
    return _strict_dataclass(InitializationSpec, values, "initialization")

def load_training_task(path: str | Path) -> TrainingTask:
    """读取并做结构校验。

    这里故意使用严格字段检查：训练任务出现拼写错误时应该直接失败，不能静默忽略。
    """

    p = Path(path)
    raw = load_mapping_file(p)
    allowed = {
        "schema_version",
        "name",
        "status",
        "description",
        "algorithm",
        "compatibility",
        "initialization",
        "budget",
        "runtime",
        "training_control",
        "checkpoint",
        "evaluation",
        "execution",
        "probes",
    }
    unknown = sorted(set(raw) - allowed)
    if unknown:
        raise ValueError(f"unknown task keys: {', '.join(unknown)}")

    probes_raw = _mapping(raw.get("probes"), "probes")
    probes: dict[str, ProbeSpec] = {}
    for probe_name, probe_value in probes_raw.items():
        probes[str(probe_name)] = _strict_dataclass(
            ProbeSpec, _mapping(probe_value, f"probes.{probe_name}"), f"probes.{probe_name}"
        )

    task = TrainingTask(
        schema_version=int(raw.get("schema_version", TASK_SCHEMA_VERSION)),
        name=str(raw.get("name", "unnamed")),
        status=str(raw.get("status", "draft")),
        description=str(raw.get("description", "")),
        algorithm=_strict_dataclass(AlgorithmSpec, _mapping(raw.get("algorithm"), "algorithm"), "algorithm"),
        compatibility=_strict_dataclass(
            CompatibilitySpec, _mapping(raw.get("compatibility"), "compatibility"), "compatibility"
        ),
        initialization=_parse_initialization(_mapping(raw.get("initialization"), "initialization")),
        budget=_strict_dataclass(BudgetSpec, _mapping(raw.get("budget"), "budget"), "budget"),
        runtime=_strict_dataclass(RuntimeSpec, _mapping(raw.get("runtime"), "runtime"), "runtime"),
        training_control=_strict_dataclass(
            TrainingControlSpec, _mapping(raw.get("training_control"), "training_control"), "training_control"
        ),
        checkpoint=_strict_dataclass(
            CheckpointSpec, _mapping(raw.get("checkpoint"), "checkpoint"), "checkpoint"
        ),
        evaluation=_strict_dataclass(
            EvaluationSpec, _mapping(raw.get("evaluation"), "evaluation"), "evaluation"
        ),
        execution=_strict_dataclass(
            ExecutionSpec, _mapping(raw.get("execution"), "execution"), "execution"
        ),
        probes=probes,
    )
    validate_training_task(task)
    return task


def validate_training_task(task: TrainingTask) -> None:
    if task.schema_version != TASK_SCHEMA_VERSION:
        raise ValueError(
            f"unsupported training task schema_version={task.schema_version}; expected {TASK_SCHEMA_VERSION}"
        )
    if not task.name or task.name == "unnamed":
        raise ValueError("training task requires a stable name")
    if task.status not in {"draft", "ready", "retired"}:
        raise ValueError(f"invalid task status: {task.status}")
    if task.initialization.mode not in {"random", "warm_start"}:
        raise ValueError("initialization.mode must be random or warm_start")
    if task.initialization.mode == "warm_start":
        if not task.initialization.checkpoint:
            raise ValueError("warm_start requires initialization.checkpoint")
        digest = task.initialization.checkpoint_sha256.strip().lower()
        if task.status == "ready" and not digest:
            raise ValueError("ready warm_start task requires initialization.checkpoint_sha256")
        if digest and (len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest)):
            raise ValueError("initialization.checkpoint_sha256 must be a 64-char lowercase hex SHA-256")
        if task.initialization.source_observation_schema <= 0:
            raise ValueError("warm_start requires source_observation_schema > 0")
        if task.initialization.source_action_grammar <= 0:
            raise ValueError("warm_start requires source_action_grammar > 0")
        if not task.initialization.reset_optimizer:
            raise ValueError("warm_start currently requires reset_optimizer=true")
        valid_decisions = {name.lower() for name in DECISION_KIND_NAMES.values()}
        invalid_decisions = [
            name for name in task.initialization.reset_decision_embeddings
            if str(name).strip().lower() not in valid_decisions
        ]
        if invalid_decisions:
            raise ValueError(
                "unknown initialization.reset_decision_embeddings: " + ", ".join(invalid_decisions)
            )
        valid_options = {name.lower() for name in OPTION_KIND_NAMES.values()}
        invalid_options = [
            name for name in task.initialization.reset_option_embeddings
            if str(name).strip().lower() not in valid_options
        ]
        if invalid_options:
            raise ValueError(
                "unknown initialization.reset_option_embeddings: " + ", ".join(invalid_options)
            )
    if task.budget.total_games <= 0:
        raise ValueError("budget.total_games must be > 0")
    if task.budget.games_per_update <= 0:
        raise ValueError("budget.games_per_update must be > 0")
    if task.runtime.num_envs <= 0 or task.runtime.max_batch_size <= 0:
        raise ValueError("runtime num_envs/max_batch_size must be > 0")
    if task.runtime.minibatch_size <= 0:
        raise ValueError("runtime.minibatch_size must be > 0")
    if task.training_control.mode not in {"self_play", "learner_vs_frozen"}:
        raise ValueError("training_control.mode must be self_play or learner_vs_frozen")
    if task.training_control.mode == "learner_vs_frozen":
        if not task.training_control.learner_deck or not task.training_control.opponent_deck:
            raise ValueError("learner_vs_frozen requires learner_deck and opponent_deck")
        if not task.training_control.frozen_checkpoint:
            raise ValueError("learner_vs_frozen requires frozen_checkpoint")
        digest = task.training_control.frozen_checkpoint_sha256.strip().lower()
        if task.status == "ready" and not digest:
            raise ValueError("ready learner_vs_frozen task requires frozen_checkpoint_sha256")
        if digest and (len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest)):
            raise ValueError("training_control.frozen_checkpoint_sha256 must be a 64-char lowercase hex SHA-256")
        if task.training_control.side_schedule != "alternate":
            raise ValueError("learner_vs_frozen currently requires side_schedule=alternate")
        if task.budget.games_per_update < task.runtime.num_envs:
            raise ValueError("learner_vs_frozen games_per_update must be >= runtime.num_envs")
    if task.checkpoint.interval_updates < 0:
        raise ValueError("checkpoint.interval_updates must be >= 0")
    if task.evaluation.every_updates < 0 or task.evaluation.games <= 0 or task.evaluation.num_envs <= 0:
        raise ValueError("evaluation schedule is invalid")
    if not 0.0 <= task.evaluation.internal_promote_win_rate <= 1.0:
        raise ValueError("evaluation.internal_promote_win_rate must be in [0, 1]")
    if task.execution.resume not in {"auto", "never", "required"}:
        raise ValueError("execution.resume must be auto, never, or required")
    for name, probe in task.probes.items():
        if probe.status not in {"reserved", "enabled", "disabled"}:
            raise ValueError(f"probe {name} has invalid status={probe.status}")
        if probe.every_games < 0:
            raise ValueError(f"probe {name}.every_games must be >= 0")
