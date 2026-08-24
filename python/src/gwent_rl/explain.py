"""Small debug helpers for explaining a live inference batch row."""
from __future__ import annotations

from .schema import DECISION_KIND_NAMES, OPTION_KIND_NAMES


def explain_batch_row(batch, row: int = 0, max_objects: int = 8, max_options: int = 12) -> str:
    if row < 0 or row >= batch.count:
        raise IndexError(row)
    lines: list[str] = []
    decision = DECISION_KIND_NAMES.get(int(batch.decision_kinds[row]), f"decision_{int(batch.decision_kinds[row])}")
    lines.append(
        f"env={int(batch.env_indices[row])} serial={int(batch.decision_serials[row])} "
        f"actor={int(batch.actor_ids[row])} decision={decision} "
        f"objects={int(batch.object_counts[row])} options={int(batch.option_counts[row])} "
        f"source_card={int(batch.source_card_ids[row])}"
    )

    object_count = min(int(batch.object_counts[row]), max_objects)
    lines.append("objects:")
    for i in range(object_count):
        if batch.object_mask[row, i] == 0:
            continue
        features = batch.object_features[row, i]
        lines.append(
            f"  [{i}] entity={int(batch.object_entity_ids[row, i])} "
            f"card={int(batch.object_card_ids[row, i])} owner={int(batch.object_owner_ids[row, i])} "
            f"zone={int(batch.object_zone_ids[row, i])} row={int(batch.object_row_ids[row, i])} "
            f"slot={int(batch.object_slot_indices[row, i])} "
            f"power={float(features[8]):.0f} armor={float(features[10]):.0f} "
            f"bleeding={float(features[11]):.0f} locked={float(features[15]):.0f}"
        )

    option_count = min(int(batch.option_counts[row]), max_options)
    lines.append("options:")
    for i in range(option_count):
        if batch.option_mask[row, i] == 0:
            continue
        kind = OPTION_KIND_NAMES.get(int(batch.option_kind_ids[row, i]), f"kind_{int(batch.option_kind_ids[row, i])}")
        lines.append(
            f"  [{i}] {kind} card={int(batch.option_card_ids[row, i])} "
            f"src_obj={int(batch.option_source_object_indices[row, i])} "
            f"tgt_obj={int(batch.option_target_object_indices[row, i])} "
            f"row={int(batch.option_target_row_ids[row, i])} "
            f"insert_position={int(batch.option_insert_positions[row, i])} "
            f"hand_slot={int(batch.option_hand_slot_indices[row, i])}"
        )
    return "\n".join(lines)
