from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator

# Product-facing HTTP contract version. This is intentionally independent from
# the RL observation schema/action-grammar versions.
CORE_API_VERSION = 1


class ContractModel(BaseModel):
    """Strict boundary model: contract drift should fail loudly at the BFF."""

    model_config = ConfigDict(extra="forbid")


class PlayerSummary(ContractModel):
    score: int
    hand: int = Field(ge=0)
    wins: int = Field(ge=0)
    passed: bool


class GameSummary(ContractModel):
    round: int = Field(ge=0)
    turn: int = Field(ge=0)
    actor: int
    decision_kind: int = Field(ge=0)
    decision: str
    done: bool
    winner_id: int
    p0: PlayerSummary
    p1: PlayerSummary


class GameObject(ContractModel):
    object_index: int = Field(ge=0)
    entity_id: int
    card_id: int
    name: str
    type: str
    ability_text: str
    owner: int
    controller: int
    zone: int
    zone_name: str
    row: int
    row_name: str
    slot: int
    power: int
    armor: int
    status: list[str]


class RowEffect(ContractModel):
    side: int = Field(ge=0, le=1)
    row: int = Field(ge=0, le=1)
    id: str
    name: str
    duration: int = Field(gt=0)


class GameAction(ContractModel):
    index: int = Field(ge=0)
    kind_id: int = Field(ge=0)
    kind: str
    label: str
    card_id: int
    source: str
    target: str
    hand_slot: int

    # uint64 is emitted as a decimal string to preserve all 64 bits in JS.
    stable_hash: str = Field(pattern=r"^\d+$")

    # Structured presentation metadata emitted for every option. -1 means N/A.
    # Product code must never parse source/target labels to reconstruct these.
    source_object_index: int
    target_object_index: int
    target_side: int
    target_zone: int
    target_row: int
    insert_position: int

    @field_validator("stable_hash")
    @classmethod
    def stable_hash_fits_uint64(cls, value: str) -> str:
        if int(value) > 0xFFFF_FFFF_FFFF_FFFF:
            raise ValueError("stable_hash exceeds uint64 range")
        return value

    @model_validator(mode="after")
    def validate_structured_choice_metadata(self) -> "GameAction":
        if self.kind == "choose_insert_position":
            if self.insert_position < 0 or self.target_side < 0 or self.target_row < 0:
                raise ValueError("choose_insert_position requires side, row and insert_position")
        if self.kind == "choose_row" and (self.target_side < 0 or self.target_row < 0):
            raise ValueError("choose_row requires side and row")
        return self


class AiAction(GameAction):
    confidence: float | None = None
    value: float | None = None
    ms: float | None = Field(default=None, ge=0)
    status: str | None = None
    reward_p0: float | None = None
    reward_p1: float | None = None


class DeckSelection(ContractModel):
    p0: int = Field(ge=0)
    p1: int = Field(ge=0)


GameMode = Literal["human_vs_ai", "manual_test"]


class GameState(ContractModel):
    api_version: int
    match_id: str = Field(pattern=r"^[0-9a-f]{32}$")
    revision: int = Field(ge=0)
    summary: GameSummary
    objects: list[GameObject]
    row_effects: list[RowEffect]
    actions: list[GameAction]
    last_human_action: AiAction | None
    last_ai_actions: list[AiAction]
    checkpoint: str
    checkpoint_update: int
    device: str
    decks: DeckSelection
    mode: GameMode = "manual_test"


class CounterfactualActionChainStep(AiAction):
    """One Core/Strategy action executed only inside a preview branch."""

    decision_serial: int = Field(ge=1)
    parent_decision_serial: int | None = Field(default=None, ge=1)
    role: Literal["root_action", "required_choice"]
    actor_id: Literal[0]
    source_card_id: int
    target_card_id: int
    source_zone: int
    summary_before: GameSummary
    summary_after: GameSummary


class CounterfactualActionChainRoot(ContractModel):
    """The one voluntary action selected by the policy for this guidance."""

    decision_serial: int = Field(ge=1)
    kind: str
    card_id: int
    source_object_index: int


class CounterfactualActionChainTrace(ContractModel):
    """Read-only AI guidance: one root action plus Core-required choices."""

    api_version: int
    schema_version: Literal["counterfactual-action-chain-v2"]
    base_match_id: str = Field(pattern=r"^[0-9a-f]{32}$")
    base_revision: int = Field(ge=0)
    base_state_signature: str = Field(pattern=r"^[0-9a-f]{64}$")
    controlled_player: Literal[0]
    boundary: Literal["one_root_action_with_required_choices"]
    root: CounterfactualActionChainRoot
    status: Literal["complete", "stopped"]
    stopped_reason: str
    steps: list[CounterfactualActionChainStep] = Field(min_length=1)
    start_summary: GameSummary
    end_summary: GameSummary

    @model_validator(mode="after")
    def validate_action_chain(self) -> "CounterfactualActionChainTrace":
        roots = [step for step in self.steps if step.role == "root_action"]
        if len(roots) != 1:
            raise ValueError("action-chain trace must contain exactly one root_action")
        root_step = roots[0]
        if root_step.decision_serial != self.root.decision_serial:
            raise ValueError("root decision_serial must match the root_action step")
        if root_step.parent_decision_serial is not None:
            raise ValueError("root_action cannot have a parent_decision_serial")
        for step in self.steps:
            if step.role == "required_choice" and step.parent_decision_serial != self.root.decision_serial:
                raise ValueError("required_choice must belong to the root action")
        return self


class CoreHealth(ContractModel):
    ok: bool
    api_version: int
    schema_version: int
    checkpoint: str
    checkpoint_update: int
    device: str
    actor: int


class CoreReloadResult(ContractModel):
    api_version: int
    checkpoint: str
    device: str
    update: int
    total_decisions: int = Field(ge=0)
