from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any, Literal

TeacherLevel = Literal["beginner", "intermediate", "advanced"]


@dataclass(frozen=True)
class TeacherRequest:
    """Input boundary for one explanation request.

    ``decision_packet`` may be a DecisionPacket v0 dictionary or one
    TraceDecision dictionary emitted by ``gwent_rl.decision_trace``.  The
    Teacher only reads these facts; it does not create a new action.
    """

    decision_packet: dict[str, Any]
    public_state: dict[str, Any] = field(default_factory=dict)
    level: TeacherLevel = "beginner"
    language: str = "zh-CN"
    top_k: int = 3


@dataclass(frozen=True)
class TeacherAlternative:
    option_index: int
    label: str
    probability: float | None = None


@dataclass(frozen=True)
class TeacherResponse:
    schema_version: str
    decision_serial: int | None
    level: TeacherLevel
    headline: str
    explanation: str
    action_label: str
    policy_probability: float | None
    state_value: float | None
    action_role: str | None = None
    parent_decision_serial: int | None = None
    alternatives: tuple[TeacherAlternative, ...] = ()
    grounded_facts: tuple[str, ...] = ()
    caveats: tuple[str, ...] = ()
    prompt: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class TeacherTurnRequest:
    """A Core-produced, read-only current-turn branch trace."""

    turn_trace: dict[str, Any]
    level: TeacherLevel = "beginner"
    language: str = "zh-CN"
    top_k: int = 3


@dataclass(frozen=True)
class TeacherTurnResponse:
    schema_version: str
    level: TeacherLevel
    headline: str
    explanation: str
    stopped_reason: str
    steps: tuple[TeacherResponse, ...]
    root_decision_serial: int | None = None
    boundary: str | None = None
    grounded_facts: tuple[str, ...] = ()
    caveats: tuple[str, ...] = ()

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)
