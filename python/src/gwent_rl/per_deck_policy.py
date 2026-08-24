from __future__ import annotations

from pathlib import Path
from typing import Any

import torch

from gwent_rl.experiment import policy_from_checkpoint


class PerDeckPolicyRouter:
    """Load and route one policy checkpoint per deck."""

    def __init__(
        self,
        deck_a_checkpoint: str | Path,
        deck_b_checkpoint: str | Path,
        device: str | torch.device = "auto",
    ) -> None:
        self.device = device

        self.checkpoints = {
            "a": Path(deck_a_checkpoint),
            "b": Path(deck_b_checkpoint),
        }

        for deck, path in self.checkpoints.items():
            if not path.is_file():
                raise FileNotFoundError(
                    f"checkpoint for deck {deck!r} does not exist: {path}"
                )

        self.policies: dict[str, Any] = {}
        self.metadata: dict[str, dict[str, Any]] = {}

        for deck in ("a", "b"):
            policy, metadata = policy_from_checkpoint(
                self.checkpoints[deck],
                device=self.device,
            )
            self.policies[deck] = policy
            self.metadata[deck] = metadata

    def get(self, deck: str):
        deck = str(deck).strip().lower()

        if deck in {"deck_a", "a"}:
            deck = "a"
        elif deck in {"deck_b", "b"}:
            deck = "b"
        else:
            raise ValueError(
                f"unsupported deck {deck!r}; expected 'a', 'b', "
                f"'deck_a', or 'deck_b'"
            )

        return self.policies[deck]

    def get_metadata(self, deck: str) -> dict[str, Any]:
        deck = str(deck).strip().lower()

        if deck == "deck_a":
            deck = "a"
        elif deck == "deck_b":
            deck = "b"

        if deck not in self.metadata:
            raise ValueError(f"unsupported deck {deck!r}")

        return self.metadata[deck]

    def checkpoint_for(self, deck: str) -> Path:
        deck = str(deck).strip().lower()

        if deck == "deck_a":
            deck = "a"
        elif deck == "deck_b":
            deck = "b"

        if deck not in self.checkpoints:
            raise ValueError(f"unsupported deck {deck!r}")

        return self.checkpoints[deck]
