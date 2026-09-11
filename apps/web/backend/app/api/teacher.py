from __future__ import annotations

from typing import Any, Literal

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel, ConfigDict, Field

from app.clients.gwent_core import GwentCoreError, GwentCoreStaleStateError
from app.clients.teacher import TeacherClient, TeacherError
from app.models.core_contract import AiAction, GameState
from app.services.game_service import GameService
from app.services.teacher_preview_service import TeacherPreviewService

router = APIRouter(prefix="/api/teacher", tags=["teacher"])


class RequestModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class TeacherExplainRequest(RequestModel):
    action_position: int = 0
    level: Literal["beginner", "intermediate", "advanced"] = "beginner"
    top_k: int = Field(default=3, ge=1, le=8)


class TeacherExplainResponse(RequestModel):
    ok: bool
    response: dict[str, Any] | None = None
    error: str | None = None


class TeacherTurnPreviewRequest(RequestModel):
    level: Literal["beginner", "intermediate", "advanced"] = "beginner"
    top_k: int = Field(default=3, ge=1, le=8)
    match_id: str | None = Field(default=None, pattern=r"^[0-9a-f]{32}$")
    expected_revision: int | None = Field(default=None, ge=0)


class TeacherTurnPreviewResponse(RequestModel):
    ok: bool
    base_match_id: str | None = None
    base_revision: int | None = Field(default=None, ge=0)
    stale: bool = False
    response: dict[str, Any] | None = None
    error: str | None = None


def _game(request: Request) -> GameService:
    return request.app.state.game_service


def _teacher(request: Request) -> TeacherClient:
    return request.app.state.teacher_client


def _preview_service(request: Request) -> TeacherPreviewService:
    return request.app.state.teacher_preview_service


def _safe_packet(action: AiAction, game: GameState, position: int) -> dict[str, Any]:
    """Build the live Teacher packet without exposing unplayed AI hand choices.

    The Core HTTP surface only publishes actions that the AI already executed.
    We intentionally send one chosen candidate rather than the full hidden-hand
    candidate set.  The selected action's confidence/value remain useful
    teaching evidence without leaking private game information.
    """

    return {
        "schema_version": "decision-packet-v0",
        "decision_serial": int(game.summary.turn) * 100 + int(position),
        "actor_id": 1,
        "decision_kind": action.kind,
        "recommended_option_index": action.index,
        "state_value": action.value,
        "candidates": [
            {
                "option_index": action.index,
                "probability": action.confidence,
                "option_kind": action.kind,
                "card_id": action.card_id,
                "source_object_index": action.source_object_index,
                "target_object_index": action.target_object_index,
                "target_side_id": action.target_side,
                "target_zone_id": action.target_zone,
                "target_row_id": action.target_row,
                "insert_position": action.insert_position,
            }
        ],
        "evidence": {
            "live_safe": True,
            "hidden_candidates_redacted": True,
            "stable_hash": action.stable_hash,
        },
        "notes": ["hidden AI hand alternatives are intentionally redacted"],
    }


@router.get("/health")
async def health(request: Request) -> dict[str, Any]:
    try:
        payload = await _teacher(request).health()
        return {"ok": bool(payload.get("ok", True)), "teacher": payload}
    except TeacherError as exc:
        return {"ok": False, "error": str(exc)}


@router.post("/explain", response_model=TeacherExplainResponse)
async def explain(req: TeacherExplainRequest, request: Request) -> TeacherExplainResponse:
    game = await _game(request).get_state()
    if game.mode != "human_vs_ai":
        return TeacherExplainResponse(ok=False, error="Teacher is available during human_vs_ai mode only")
    if not game.last_ai_actions:
        return TeacherExplainResponse(ok=False, error="AI has not produced an action to explain yet")

    position = req.action_position
    if position < 0:
        position += len(game.last_ai_actions)
    if position < 0 or position >= len(game.last_ai_actions):
        raise HTTPException(status_code=422, detail="action_position is outside last_ai_actions")

    action = game.last_ai_actions[position]
    packet = _safe_packet(action, game, position)
    public_state = game.model_dump(mode="json")
    # The public Core state already omits the AI hand in human_vs_ai mode.
    # Current human legal actions are not needed by Teacher and are removed to
    # keep the evidence surface minimal.
    public_state["actions"] = []

    try:
        response = await _teacher(request).explain(
            {
                "decision_packet": packet,
                "public_state": public_state,
                "level": req.level,
                "language": "zh-CN",
                "top_k": req.top_k,
            }
        )
    except TeacherError as exc:
        return TeacherExplainResponse(ok=False, error=str(exc))
    return TeacherExplainResponse(ok=True, response=response)


@router.post("/preview-turn", response_model=TeacherTurnPreviewResponse)
async def preview_turn(
    req: TeacherTurnPreviewRequest,
    request: Request,
) -> TeacherTurnPreviewResponse:
    """Explain a Core-produced, read-only AI takeover of the human turn."""

    try:
        state = await _game(request).get_state()
        if req.match_id is not None or req.expected_revision is not None:
            if req.match_id != state.match_id or req.expected_revision != state.revision:
                return TeacherTurnPreviewResponse(
                    ok=False,
                    base_match_id=state.match_id,
                    base_revision=state.revision,
                    stale=True,
                    error="局面已更新，已丢弃旧的教师请求。",
                )
        trace, response = await _preview_service(request).preview_turn(state, req.level, req.top_k)
    except GwentCoreStaleStateError as exc:
        return TeacherTurnPreviewResponse(ok=False, stale=True, error=str(exc))
    except GwentCoreError as exc:
        # A preview is optional teaching UX. A state where no human turn is
        # available should not be surfaced as a gameplay failure.
        return TeacherTurnPreviewResponse(ok=False, error=str(exc))

    except TeacherError as exc:
        return TeacherTurnPreviewResponse(ok=False, error=str(exc))
    return TeacherTurnPreviewResponse(
        ok=True,
        base_match_id=trace.base_match_id,
        base_revision=trace.base_revision,
        response=response,
    )
