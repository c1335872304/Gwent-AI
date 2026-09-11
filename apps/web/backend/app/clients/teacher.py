from __future__ import annotations

from typing import Any

import httpx


class TeacherError(RuntimeError):
    pass


class TeacherClient:
    """HTTP client for the optional read-only Teacher Agent service."""

    def __init__(self, base_url: str, timeout_s: float = 8.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = httpx.Timeout(timeout_s)
        self._client: httpx.AsyncClient | None = None

    async def start(self) -> None:
        if self._client is None:
            self._client = httpx.AsyncClient(
                base_url=self.base_url,
                timeout=self.timeout,
                trust_env=False,
            )

    async def aclose(self) -> None:
        if self._client is not None:
            await self._client.aclose()
            self._client = None

    async def _request_json(self, method: str, path: str, **kwargs: Any) -> Any:
        if self._client is None:
            raise RuntimeError("TeacherClient.start() must be called before use")
        try:
            response = await self._client.request(method, path, **kwargs)
        except httpx.HTTPError as exc:
            raise TeacherError(f"Teacher request failed: {exc}") from exc

        if response.is_error:
            detail: Any = response.text
            try:
                payload = response.json()
                detail = payload.get("detail", payload) if isinstance(payload, dict) else payload
            except ValueError:
                pass
            raise TeacherError(f"Teacher returned HTTP {response.status_code}: {detail}")

        try:
            return response.json()
        except ValueError as exc:
            raise TeacherError("Teacher returned invalid JSON") from exc

    async def health(self) -> dict[str, Any]:
        payload = await self._request_json("GET", "/health")
        if not isinstance(payload, dict):
            raise TeacherError("Teacher health payload must be an object")
        return payload

    async def explain(self, payload: dict[str, Any]) -> dict[str, Any]:
        body = await self._request_json("POST", "/v1/explain", json=payload)
        if not isinstance(body, dict) or not isinstance(body.get("response"), dict):
            raise TeacherError("Teacher explain payload is missing response")
        return dict(body["response"])

    async def explain_turn(self, payload: dict[str, Any]) -> dict[str, Any]:
        body = await self._request_json("POST", "/v1/explain-turn", json=payload)
        if not isinstance(body, dict) or not isinstance(body.get("response"), dict):
            raise TeacherError("Teacher turn explanation payload is missing response")
        return dict(body["response"])
