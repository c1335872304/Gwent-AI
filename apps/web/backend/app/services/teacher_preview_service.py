from __future__ import annotations

import asyncio
from collections.abc import Awaitable, Callable
from typing import Any, TypeVar

from app.clients.gwent_core import GwentCoreProtocolError
from app.clients.teacher import TeacherClient
from app.models.core_contract import CounterfactualActionChainTrace, GameState
from app.services.game_service import GameService


ResultT = TypeVar("ResultT")
TraceKey = tuple[str, int, str]
TeacherKey = tuple[str, int, str, str, int]


class TeacherPreviewService:
    """Cache public, read-only turn previews by authoritative Core revision.

    This service owns only orchestration.  Core still produces the branch and
    Teacher still turns its evidence into prose.  The epoch prevents a slow,
    invalidated request from repopulating a cache for an old match revision.
    """

    def __init__(self, game: GameService, teacher: TeacherClient) -> None:
        self._game = game
        self._teacher = teacher
        self._lock = asyncio.Lock()
        self._epoch = 0
        self._trace_cache: dict[TraceKey, CounterfactualActionChainTrace] = {}
        self._teacher_cache: dict[TeacherKey, dict[str, Any]] = {}
        self._trace_inflight: dict[TraceKey, asyncio.Task[CounterfactualActionChainTrace]] = {}
        self._teacher_inflight: dict[TeacherKey, asyncio.Task[dict[str, Any]]] = {}

    async def invalidate_except(self, match_id: str, revision: int) -> None:
        """Discard previews from every prior real game state after /new or /step."""

        async with self._lock:
            self._epoch += 1
            self._trace_cache = {
                key: value
                for key, value in self._trace_cache.items()
                if key[0] == match_id and key[1] == revision
            }
            self._teacher_cache = {
                key: value
                for key, value in self._teacher_cache.items()
                if key[0] == match_id and key[1] == revision
            }

    async def _singleflight(
        self,
        cache: dict[Any, ResultT],
        inflight: dict[Any, asyncio.Task[ResultT]],
        key: Any,
        factory: Callable[[], Awaitable[ResultT]],
    ) -> ResultT:
        async with self._lock:
            cached = cache.get(key)
            if cached is not None:
                return cached
            task = inflight.get(key)
            if task is None:
                task = asyncio.create_task(factory())
                inflight[key] = task
            epoch = self._epoch

        try:
            result = await task
        finally:
            if task.done():
                async with self._lock:
                    if inflight.get(key) is task:
                        inflight.pop(key, None)

        async with self._lock:
            if self._epoch == epoch:
                cache[key] = result
        return result

    async def _trace_for(self, state: GameState) -> CounterfactualActionChainTrace:
        key: TraceKey = (state.match_id, state.revision, "counterfactual-action-chain-v2")

        async def create() -> CounterfactualActionChainTrace:
            trace = await self._game.preview_current_human_turn(
                match_id=state.match_id,
                expected_revision=state.revision,
            )
            if trace.base_match_id != state.match_id or trace.base_revision != state.revision:
                raise GwentCoreProtocolError("Core preview base revision does not match its requested state")
            return trace

        return await self._singleflight(self._trace_cache, self._trace_inflight, key, create)

    async def preview_turn(
        self,
        state: GameState,
        level: str,
        top_k: int,
    ) -> tuple[CounterfactualActionChainTrace, dict[str, Any]]:
        trace = await self._trace_for(state)
        key: TeacherKey = (
            trace.base_match_id,
            trace.base_revision,
            trace.schema_version,
            level,
            top_k,
        )

        async def create() -> dict[str, Any]:
            return await self._teacher.explain_turn(
                {
                    "turn_trace": trace.model_dump(mode="json"),
                    "level": level,
                    "language": "zh-CN",
                    "top_k": top_k,
                }
            )

        return trace, await self._singleflight(self._teacher_cache, self._teacher_inflight, key, create)
