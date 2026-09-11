from __future__ import annotations

from typing import Any, TypeVar

import httpx
from pydantic import BaseModel, ValidationError

from app.models.core_contract import (
    CORE_API_VERSION,
    CounterfactualActionChainTrace,
    CoreHealth,
    CoreReloadResult,
    GameMode,
    GameState,
)

ModelT = TypeVar("ModelT", bound=BaseModel)


class GwentCoreError(RuntimeError):
    pass


class GwentCoreCommandError(GwentCoreError):
    def __init__(self, status_code: int, code: str, message: str) -> None:
        super().__init__(message)
        self.status_code = status_code
        self.code = code


class GwentCoreStaleStateError(GwentCoreCommandError):
    pass


class GwentCoreProtocolError(GwentCoreError):
    pass


class GwentCoreClient:
    """Typed HTTP adapter for the authoritative Core/Strategy service.

    The app layer must not import gwent_rl, load the C++ shared library, or
    reconstruct legal actions. Boundary payloads are validated here before the
    rest of the product sees them.
    """

    def __init__(self, base_url: str, timeout_s: float = 15.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = httpx.Timeout(timeout_s)
        self._client: httpx.AsyncClient | None = None

    async def start(self) -> None:
        if self._client is None:
            self._client = httpx.AsyncClient(
                base_url=self.base_url,
                timeout=self.timeout,
                # Core is a local sidecar (127.0.0.1 by default).  Conda/Windows
                # environments may define HTTP_PROXY/HTTPS_PROXY; trusting those
                # variables can route localhost through a proxy and surface as a
                # misleading HTTP 502 even while :8008 is healthy.
                trust_env=False,
            )

    async def aclose(self) -> None:
        if self._client is not None:
            await self._client.aclose()
            self._client = None

    async def _request_json(self, method: str, path: str, **kwargs: Any) -> Any:
        if self._client is None:
            raise RuntimeError("GwentCoreClient.start() must be called before use")
        try:
            response = await self._client.request(method, path, **kwargs)
        except httpx.HTTPError as exc:
            raise GwentCoreError(f"Core request failed: {exc}") from exc

        if response.is_error:
            detail: Any = response.text
            try:
                payload = response.json()
                detail = payload.get("detail", payload) if isinstance(payload, dict) else payload
            except ValueError:
                pass
            if isinstance(detail, dict):
                code = str(detail.get("code", "core_error"))
                message = str(detail.get("message", detail))
                if response.status_code == 409 and code == "stale_state":
                    raise GwentCoreStaleStateError(response.status_code, code, message)
                if response.status_code in (409, 422):
                    raise GwentCoreCommandError(response.status_code, code, message)
            raise GwentCoreError(f"Core returned HTTP {response.status_code}: {detail}")

        try:
            return response.json()
        except ValueError as exc:
            raise GwentCoreProtocolError("Core returned invalid JSON") from exc

    @staticmethod
    def _validate(model: type[ModelT], payload: Any) -> ModelT:
        try:
            parsed = model.model_validate(payload)
        except ValidationError as exc:
            raise GwentCoreProtocolError(f"Core payload violates HTTP contract: {exc}") from exc

        api_version = getattr(parsed, "api_version", CORE_API_VERSION)
        if api_version != CORE_API_VERSION:
            raise GwentCoreProtocolError(
                f"Unsupported Core API version {api_version}; product expects {CORE_API_VERSION}"
            )
        return parsed

    async def health(self) -> CoreHealth:
        payload = await self._request_json("GET", "/health")
        return self._validate(CoreHealth, payload)

    async def state(self) -> GameState:
        payload = await self._request_json("GET", "/state")
        return self._validate(GameState, payload)

    async def new_game(
        self,
        seed: int,
        starting_player_id: int = -1,
        player0_deck_id: int = 0,
        player1_deck_id: int = 0,
        mode: GameMode = "manual_test",
    ) -> GameState:
        payload = await self._request_json(
            "POST",
            "/new",
            json={
                "seed": seed,
                "starting_player_id": starting_player_id,
                "player0_deck_id": player0_deck_id,
                "player1_deck_id": player1_deck_id,
                "mode": mode,
            },
        )
        return self._validate(GameState, payload)

    async def step(
        self,
        option_index: int,
        match_id: str | None = None,
        expected_revision: int | None = None,
    ) -> GameState:
        payload = await self._request_json(
            "POST",
            "/step",
            json={
                "option_index": option_index,
                "match_id": match_id,
                "expected_revision": expected_revision,
            },
        )
        return self._validate(GameState, payload)

    async def preview_current_human_turn(
        self,
        match_id: str | None = None,
        expected_revision: int | None = None,
    ) -> CounterfactualActionChainTrace:
        payload = await self._request_json(
            "POST",
            "/preview/current-human-turn",
            json={"match_id": match_id, "expected_revision": expected_revision},
        )
        return self._validate(CounterfactualActionChainTrace, payload)

    async def reload_model(self, checkpoint: str | None = None) -> CoreReloadResult:
        payload = await self._request_json("POST", "/reload", json={"checkpoint": checkpoint})
        return self._validate(CoreReloadResult, payload)
