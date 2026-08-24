"""Typed HTTP contract models for the Web/Core boundary."""

from .core_contract import (
    CORE_API_VERSION,
    AiAction,
    CoreHealth,
    CoreReloadResult,
    DeckSelection,
    GameAction,
    GameObject,
    GameState,
    GameSummary,
    PlayerSummary,
)

__all__ = [
    "CORE_API_VERSION",
    "AiAction",
    "CoreHealth",
    "CoreReloadResult",
    "DeckSelection",
    "GameAction",
    "GameObject",
    "GameState",
    "GameSummary",
    "PlayerSummary",
]
