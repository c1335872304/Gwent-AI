from __future__ import annotations

import time

from fastapi import APIRouter, Depends, HTTPException, Request
from pydantic import BaseModel, ConfigDict, Field

from app.clients.gwent_core import GwentCoreCommandError, GwentCoreError, GwentCoreStaleStateError
from app.models.core_contract import CoreHealth, CoreReloadResult, GameMode, GameState
from app.services.game_service import GameService

router = APIRouter(prefix="/api", tags=["game"])


class RequestModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class NewGameRequest(RequestModel):
    seed: int = Field(default_factory=lambda: int(time.time() * 1000))
    starting_player_id: int = -1
    # Deck validity belongs to Core. BFF only rejects nonsensical negative ids.
    player0_deck_id: int = Field(default=0, ge=0)
    player1_deck_id: int = Field(default=0, ge=0)
    mode: GameMode = "manual_test"


class StepRequest(RequestModel):
    option_index: int = Field(ge=0)
    match_id: str | None = Field(default=None, pattern=r"^[0-9a-f]{32}$")
    expected_revision: int | None = Field(default=None, ge=0)


class ReloadRequest(RequestModel):
    checkpoint: str | None = None


class HealthResponse(RequestModel):
    ok: bool
    service: str
    core: CoreHealth | None = None
    core_error: str | None = None


def service(request: Request) -> GameService:
    return request.app.state.game_service


def as_http_error(exc: GwentCoreError) -> HTTPException:
    if isinstance(exc, GwentCoreStaleStateError):
        return HTTPException(status_code=409, detail={"code": exc.code, "message": str(exc)})
    if isinstance(exc, GwentCoreCommandError):
        return HTTPException(status_code=exc.status_code, detail={"code": exc.code, "message": str(exc)})
    return HTTPException(status_code=502, detail=str(exc))


@router.get("/health", response_model=HealthResponse)
async def health(game: GameService = Depends(service)) -> HealthResponse:
    try:
        core_health = await game.health()
        return HealthResponse(ok=True, service="gwent-app", core=core_health)
    except GwentCoreError as exc:
        return HealthResponse(ok=False, service="gwent-app", core_error=str(exc))


@router.get("/game/state", response_model=GameState)
async def state(game: GameService = Depends(service)) -> GameState:
    try:
        return await game.get_state()
    except GwentCoreError as exc:
        raise as_http_error(exc) from exc


@router.post("/game/new", response_model=GameState)
async def new_game(req: NewGameRequest, request: Request, game: GameService = Depends(service)) -> GameState:
    try:
        state = await game.new_game(
            req.seed,
            req.starting_player_id,
            req.player0_deck_id,
            req.player1_deck_id,
            req.mode,
        )
        await request.app.state.teacher_preview_service.invalidate_except(state.match_id, state.revision)
        return state
    except GwentCoreError as exc:
        raise as_http_error(exc) from exc


@router.post("/game/step", response_model=GameState)
async def step(req: StepRequest, request: Request, game: GameService = Depends(service)) -> GameState:
    try:
        state = await game.step(req.option_index, req.match_id, req.expected_revision)
        await request.app.state.teacher_preview_service.invalidate_except(state.match_id, state.revision)
        return state
    except GwentCoreError as exc:
        raise as_http_error(exc) from exc


@router.post("/model/reload", response_model=CoreReloadResult)
async def reload_model(req: ReloadRequest, game: GameService = Depends(service)) -> CoreReloadResult:
    try:
        return await game.reload_model(req.checkpoint)
    except GwentCoreError as exc:
        raise as_http_error(exc) from exc
