#!/usr/bin/env python3
"""Import the external deduplicated card archive as a reference-only catalog."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path
from typing import Any

SOURCE_MEMBER = "gwent_cards/all_cards.json"

TYPE_MAP = {
    "Ability": "leader",
    "Artifact": "artifact",
    "Special": "special",
    "Stratagem": "stratagem",
    "Unit": "unit",
}

FACTION_MAP = {
    "Monster": "monsters",
    "Nilfgaard": "nilfgaard",
    "Northern Realms": "northern_realms",
    "Scoiatael": "scoiatael",
    "Skellige": "skellige",
    "Syndicate": "syndicate",
    "Neutral": "neutral",
}


def _nonempty(value: Any) -> str | None:
    text = str(value).strip() if value is not None else ""
    return text or None


def _related_ids(value: Any) -> list[str]:
    if value is None:
        return []
    return sorted(set(re.findall(r"\d+", str(value))))


def normalize_card(source: dict[str, Any]) -> dict[str, Any]:
    attributes = source.get("attributes")
    identifiers = source.get("id")
    if not isinstance(attributes, dict) or not isinstance(identifiers, dict):
        raise ValueError("each source card must contain id and attributes objects")

    card_id = identifiers.get("card")
    if not isinstance(card_id, int) or card_id <= 0:
        raise ValueError(f"invalid card id: {card_id!r}")
    source_type = attributes.get("type")
    if source_type not in TYPE_MAP:
        raise ValueError(f"unknown source card type {source_type!r} for {card_id}")
    source_faction = attributes.get("faction")
    if source_faction not in FACTION_MAP:
        raise ValueError(f"unknown source faction {source_faction!r} for {card_id}")

    card: dict[str, Any] = {
        "id": str(card_id),
        "name": str(source.get("name", "")).strip(),
        "type": TYPE_MAP[source_type],
        "faction": FACTION_MAP[source_faction],
    }
    secondary = _nonempty(attributes.get("factionSecondary"))
    if secondary:
        if secondary not in FACTION_MAP:
            raise ValueError(f"unknown secondary faction {secondary!r} for {card_id}")
        card["secondary_faction"] = FACTION_MAP[secondary]

    for source_key, target_key in (
        ("power", "power"),
        ("armor", "armor"),
        ("provision", "provision"),
    ):
        value = attributes.get(source_key)
        if isinstance(value, int):
            card[target_key] = value

    for source_key, target_key in (
        ("color", "color"),
        ("rarity", "rarity"),
        ("set", "set"),
    ):
        value = _nonempty(attributes.get(source_key))
        if value:
            card[target_key] = value

    category = _nonempty(source.get("category"))
    if category:
        card["categories"] = [part.strip() for part in re.split(r"[,，]", category) if part.strip()]
    description = _nonempty(source.get("ability"))
    if description:
        card["description"] = description
    related = _related_ids(attributes.get("related"))
    if related:
        card["related_card_ids"] = related

    if not card["name"]:
        raise ValueError(f"card {card_id} has no name")
    return card


def import_archive(archive: Path) -> dict[str, Any]:
    source_sha256 = hashlib.sha256(archive.read_bytes()).hexdigest()
    with zipfile.ZipFile(archive) as handle:
        try:
            with handle.open(SOURCE_MEMBER) as stream:
                source_member = stream.read()
        except KeyError as exc:
            raise ValueError(f"archive does not contain {SOURCE_MEMBER}") from exc
    source_member_sha256 = hashlib.sha256(source_member).hexdigest()
    source_cards = json.loads(source_member.decode("utf-8-sig"))
    if not isinstance(source_cards, list):
        raise ValueError(f"{SOURCE_MEMBER} must contain a JSON array")

    cards = [normalize_card(card) for card in source_cards]
    cards.sort(key=lambda card: int(card["id"]))
    ids = [card["id"] for card in cards]
    if len(ids) != len(set(ids)):
        raise ValueError("source archive contains duplicate card ids")
    cards_payload = json.dumps(cards, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")

    return {
        "schema_version": "gwent-reference-card-catalog-v1",
        "card_set": "reference_catalog",
        "card_count": len(cards),
        "provenance": {
            "source_artifact": archive.name,
            "source_sha256": source_sha256,
            "source_member": SOURCE_MEMBER,
            "source_member_sha256": source_member_sha256,
            "normalized_cards_sha256": hashlib.sha256(cards_payload).hexdigest(),
            "upstream_source": "unspecified",
            "game_version": "unspecified",
            "language": "zh-CN",
            "license": "unspecified",
            "notice": "Reference data only; provenance, game version, and redistribution license were not supplied with the archive.",
        },
        "cards": cards,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, default=Path("data/cards/reference/all_cards.json"))
    args = parser.parse_args()

    catalog = import_archive(args.archive)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"imported {len(catalog['cards'])} reference cards into {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
