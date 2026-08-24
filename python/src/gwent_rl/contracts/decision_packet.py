"""Strategy Core -> Teacher Agent 的只读决策数据契约。

注意：
- 当前 Teacher MVP 可以消费这个结构；
- 它仍然不进入训练、collector 或 action-selection 主路径；
- 不要在 PPO 或 legal-action 逻辑中依赖 Teacher。
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any


DECISION_PACKET_SCHEMA_VERSION = "decision-packet-v0"


@dataclass(frozen=True)
class CandidateDecision:
    """一个候选合法动作的最小解释信息。

    字段尽量对应 Strategy Core 已经拥有的 option 信息，避免为了教师层
    重新发明一套动作语义。
    """

    option_index: int
    probability: float | None = None
    logit: float | None = None
    option_kind: str | None = None
    card_id: int | None = None
    source_object_index: int | None = None
    target_object_index: int | None = None
    target_side_id: int | None = None
    target_zone_id: int | None = None
    target_row_id: int | None = None
    insert_position: int | None = None


@dataclass(frozen=True)
class DecisionPacket:
    """提供给 Teacher Agent 的旁路决策包。

    v0 故意保持很薄：先稳定 Strategy Core 与解释层的边界；成熟的
    evidence 字段未来可通过新 schema 版本升级为强类型结构。
    """

    decision_serial: int
    actor_id: int
    decision_kind: str
    recommended_option_index: int
    state_value: float | None = None
    candidates: tuple[CandidateDecision, ...] = ()
    evidence: dict[str, Any] = field(default_factory=dict)
    notes: tuple[str, ...] = ()
    schema_version: str = DECISION_PACKET_SCHEMA_VERSION

    def to_dict(self) -> dict[str, Any]:
        """转换为适合日志、JSON 或未来 LLM 工具调用的普通字典。"""

        return asdict(self)
