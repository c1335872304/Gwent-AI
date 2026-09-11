from __future__ import annotations

import asyncio
from types import SimpleNamespace

import pytest

from app.services.teacher_preview_service import TeacherPreviewService


class FakeGame:
    def __init__(self) -> None:
        self.calls: list[tuple[str, int]] = []

    async def preview_current_human_turn(self, match_id: str, expected_revision: int):
        self.calls.append((match_id, expected_revision))
        await asyncio.sleep(0)
        return SimpleNamespace(
            base_match_id=match_id,
            base_revision=expected_revision,
            schema_version="counterfactual-action-chain-v2",
            model_dump=lambda **_kwargs: {"base_revision": expected_revision},
        )


class FakeTeacher:
    def __init__(self) -> None:
        self.calls = 0

    async def explain_turn(self, _payload):
        self.calls += 1
        await asyncio.sleep(0)
        return {"schema_version": "gwent-teacher-turn-response-v1", "steps": []}


@pytest.mark.asyncio
async def test_preview_cache_singleflights_and_invalidates_after_real_state_change() -> None:
    game = FakeGame()
    teacher = FakeTeacher()
    service = TeacherPreviewService(game, teacher)
    first = SimpleNamespace(match_id="a" * 32, revision=0)

    await asyncio.gather(
        service.preview_turn(first, "beginner", 3),
        service.preview_turn(first, "beginner", 3),
    )
    assert game.calls == [("a" * 32, 0)]
    assert teacher.calls == 1

    await service.invalidate_except("a" * 32, 1)
    second = SimpleNamespace(match_id="a" * 32, revision=1)
    await service.preview_turn(second, "beginner", 3)
    assert game.calls == [("a" * 32, 0), ("a" * 32, 1)]
    assert teacher.calls == 2
