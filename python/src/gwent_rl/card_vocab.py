from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class CardTextRecord:
    card_id: int
    name: str
    type: str
    text: str
    # Human-readable ability text is static card metadata.  Keep it separate
    # from ``text`` because the latter also includes training features such as
    # categories, effect ids and implementation metadata.
    description: str = ""


def find_supported_cards_json(start: Path | None = None) -> Path:
    here = start or Path(__file__).resolve()
    for root in [Path.cwd(), *here.parents]:
        candidate = root / "data" / "cards" / "supported_cards.json"
        if candidate.exists():
            return candidate
    raise FileNotFoundError("could not locate data/cards/supported_cards.json")


def load_supported_card_records(path: str | Path | None = None) -> list[CardTextRecord]:
    source = Path(path) if path is not None else find_supported_cards_json()
    data = json.loads(source.read_text(encoding="utf-8-sig"))
    records: list[CardTextRecord] = []
    for card in data.get("cards", []):
        card_id = int(card["id"])
        description = str(card.get("description") or "")
        categories = " ".join(card.get("categories", []))
        effects = " ".join(card.get("effect_ids", []))
        metadata = " ".join(f"{k}:{v}" for k, v in sorted(card.get("metadata", {}).items()))
        text = " ".join(str(x) for x in [card.get("name", ""), card.get("type", ""), description, categories, effects, metadata] if x)
        records.append(
            CardTextRecord(
                card_id=card_id,
                name=str(card.get("name", card_id)),
                type=str(card.get("type", "")),
                text=text,
                description=description,
            )
        )
    return records


def find_deck_a_json(start: Path | None = None) -> Path:
    return find_supported_cards_json(start)


def load_deck_a_card_records(path: str | Path | None = None) -> list[CardTextRecord]:
    return load_supported_card_records(path)


def card_id_to_index(records: list[CardTextRecord]) -> dict[int, int]:
    return {rec.card_id: i + 1 for i, rec in enumerate(records)}  # 0 is unknown/pad
