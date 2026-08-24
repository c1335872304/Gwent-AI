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
