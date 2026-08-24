"""Structured policy-decision traces for diagnostics and the future Teacher Agent.

This module is intentionally read-only with respect to the game/training path:
it serializes observations, legal options, policy scores, and outcomes.  It does
not add game rules, alter rewards, or affect action legality.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
import json
from pathlib import Path
from typing import Any, Mapping

import numpy as np
import torch

from .schema import (
    DECISION_KIND_NAMES,
    GLOBAL_FEATURE_NAMES,
    OBJECT_FEATURE_NAMES,
    OPTION_FEATURE_NAMES,
    OPTION_KIND_NAMES,
)

TRACE_SCHEMA_VERSION = "gwent-policy-decision-trace-v1"


def _int(value: Any) -> int:
    return int(value)


def _float(value: Any) -> float:
    return float(value)


def load_card_catalog(root: str | Path | None = None) -> dict[int, dict[str, Any]]:
    if root is None:
        root = Path(__file__).resolve().parents[3]
    path = Path(root) / "data" / "cards" / "reference" / "all_cards.json"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    return {int(card["id"]): dict(card) for card in payload.get("cards", [])}


def _card_name(card_id: int, catalog: Mapping[int, Mapping[str, Any]]) -> str | None:
    card = catalog.get(int(card_id))
    return str(card.get("name")) if card and card.get("name") else None


@dataclass(frozen=True)
class TraceOption:
    option_index: int
    option_kind: str
    probability: float
    logit: float
    card_id: int
    card_name: str | None
    source_object_index: int
    target_object_index: int
    target_side_id: int
    target_zone_id: int
    target_row_id: int
    insert_position: int
    hand_slot_index: int
    stable_hash: int
    features: dict[str, float] = field(default_factory=dict)


@dataclass(frozen=True)
class TraceObject:
    object_index: int
    entity_id: int
    card_id: int
    card_name: str | None
    owner_id: int
    controller_id: int
    zone_id: int
    row_id: int
    slot_index: int
    features: dict[str, float] = field(default_factory=dict)


@dataclass(frozen=True)
class TraceDecision:
    env_index: int
    decision_serial: int
    actor_id: int
    actor_deck: str
    opponent_deck: str
    decision_kind: str
    source_card_id: int
    source_card_name: str | None
    state_value: float
    chosen_option_index: int
    chosen_probability: float
    globals: dict[str, float]
    objects: tuple[TraceObject, ...]
    legal_options: tuple[TraceOption, ...]
    result: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def decision_from_batch_row(
    batch,
    row: int,
    *,
    logits: torch.Tensor | np.ndarray,
    values: torch.Tensor | np.ndarray,
    chosen_option_index: int,
    actor_deck: str,
    opponent_deck: str,
    card_catalog: Mapping[int, Mapping[str, Any]],
) -> TraceDecision:
    """Snapshot one live collector row before the next collector call invalidates it."""
    if row < 0 or row >= batch.count:
        raise IndexError(row)

    logits_np = logits.detach().cpu().numpy() if isinstance(logits, torch.Tensor) else np.asarray(logits)
    values_np = values.detach().cpu().numpy() if isinstance(values, torch.Tensor) else np.asarray(values)
    option_count = int(batch.option_counts[row])
    object_count = int(batch.object_counts[row])

    row_logits = np.asarray(logits_np[row, :option_count], dtype=np.float64)
    # Stable softmax over legal options only.
    shifted = row_logits - np.max(row_logits) if option_count else row_logits
    exp = np.exp(shifted) if option_count else shifted
    probs = exp / np.sum(exp) if option_count else exp

    globals_dict = {
        name: _float(batch.global_features[row, i])
        for i, name in enumerate(GLOBAL_FEATURE_NAMES)
    }

    objects: list[TraceObject] = []
    for i in range(object_count):
        if int(batch.object_mask[row, i]) == 0:
            continue
        card_id = _int(batch.object_card_ids[row, i])
        features = {
            name: _float(batch.object_features[row, i, j])
            for j, name in enumerate(OBJECT_FEATURE_NAMES)
        }
        objects.append(
            TraceObject(
                object_index=i,
                entity_id=_int(batch.object_entity_ids[row, i]),
                card_id=card_id,
                card_name=_card_name(card_id, card_catalog),
                owner_id=_int(batch.object_owner_ids[row, i]),
                controller_id=_int(batch.object_controller_ids[row, i]),
                zone_id=_int(batch.object_zone_ids[row, i]),
                row_id=_int(batch.object_row_ids[row, i]),
                slot_index=_int(batch.object_slot_indices[row, i]),
                features=features,
            )
        )

    options: list[TraceOption] = []
    for i in range(option_count):
        if int(batch.option_mask[row, i]) == 0:
            continue
        kind_id = _int(batch.option_kind_ids[row, i])
        card_id = _int(batch.option_card_ids[row, i])
        features = {
            name: _float(batch.option_features[row, i, j])
            for j, name in enumerate(OPTION_FEATURE_NAMES)
        }
        options.append(
            TraceOption(
                option_index=i,
                option_kind=OPTION_KIND_NAMES.get(kind_id, f"kind_{kind_id}"),
                probability=float(probs[i]),
                logit=float(row_logits[i]),
                card_id=card_id,
                card_name=_card_name(card_id, card_catalog),
                source_object_index=_int(batch.option_source_object_indices[row, i]),
                target_object_index=_int(batch.option_target_object_indices[row, i]),
                target_side_id=_int(batch.option_target_side_ids[row, i]),
                target_zone_id=_int(batch.option_target_zone_ids[row, i]),
                target_row_id=_int(batch.option_target_row_ids[row, i]),
                insert_position=_int(batch.option_insert_positions[row, i]),
                hand_slot_index=_int(batch.option_hand_slot_indices[row, i]),
                stable_hash=_int(batch.option_stable_hashes[row, i]),
                features=features,
            )
        )

    source_card_id = _int(batch.source_card_ids[row])
    chosen_prob = float(probs[chosen_option_index]) if 0 <= chosen_option_index < len(probs) else 0.0
    return TraceDecision(
        env_index=_int(batch.env_indices[row]),
        decision_serial=_int(batch.decision_serials[row]),
        actor_id=_int(batch.actor_ids[row]),
        actor_deck=str(actor_deck),
        opponent_deck=str(opponent_deck),
        decision_kind=DECISION_KIND_NAMES.get(_int(batch.decision_kinds[row]), f"decision_{_int(batch.decision_kinds[row])}"),
        source_card_id=source_card_id,
        source_card_name=_card_name(source_card_id, card_catalog),
        state_value=float(values_np[row]),
        chosen_option_index=int(chosen_option_index),
        chosen_probability=chosen_prob,
        globals=globals_dict,
        objects=tuple(objects),
        legal_options=tuple(options),
    )


def attach_apply_result(decision: TraceDecision, results, result_row: int) -> dict[str, Any]:
    data = decision.to_dict()
    data["result"] = {
        "result_code": _int(results.result_codes[result_row]),
        "action_status": _int(results.action_statuses[result_row]),
        "done": bool(results.dones[result_row]),
        "winner_id": _int(results.winner_ids[result_row]),
        "reward_p0": _float(results.rewards[result_row, 0]),
        "reward_p1": _float(results.rewards[result_row, 1]),
    }
    return data


def write_trace(path: str | Path, *, metadata: Mapping[str, Any], decisions: list[dict[str, Any]], games: list[dict[str, Any]]) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    document = {
        "schema_version": TRACE_SCHEMA_VERSION,
        "metadata": dict(metadata),
        "games": list(games),
        "decisions": list(decisions),
    }
    path.write_text(json.dumps(document, ensure_ascii=False, indent=2), encoding="utf-8")
    return path
