from __future__ import annotations

from app.clients.gwent_core import GwentCoreClient
from app.models.core_contract import CoreHealth, CoreReloadResult, GameMode, GameState


class GameService:
    def __init__(self, core: GwentCoreClient) -> None:
        self.core = core

    async def health(self) -> CoreHealth:
        return await self.core.health()

    async def get_state(self) -> GameState:
        return await self.core.state()

    async def new_game(
        self,
        seed: int,
        starting_player_id: int,
        player0_deck_id: int,
        player1_deck_id: int,
        mode: GameMode,
    ) -> GameState:
        return await self.core.new_game(
            seed=seed,
            starting_player_id=starting_player_id,
            player0_deck_id=player0_deck_id,
            player1_deck_id=player1_deck_id,
            mode=mode,
        )

    async def step(self, option_index: int) -> GameState:
        # option_index is intentionally passed through unchanged. The
        # authoritative Core validates legality against its latest state.
        return await self.core.step(option_index=option_index)

    async def reload_model(self, checkpoint: str | None) -> CoreReloadResult:
        return await self.core.reload_model(checkpoint=checkpoint)
