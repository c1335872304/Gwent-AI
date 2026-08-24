#!/usr/bin/env python3
"""Validate the reference/supported/deck card-data boundaries."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any


REFERENCE_TYPES = {"unit", "special", "artifact", "leader", "stratagem"}
REFERENCE_FACTIONS = {
    "monsters", "nilfgaard", "northern_realms", "scoiatael",
    "skellige", "syndicate", "neutral",
}
REFERENCE_FIELDS = {
    "id", "name", "type", "faction", "secondary_faction", "power", "armor",
    "provision", "color", "rarity", "set", "categories", "description",
    "related_card_ids",
}


def load_object(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def card_ids(path: Path, data: dict[str, Any]) -> set[str]:
    cards = data.get("cards")
    if not isinstance(cards, list):
        raise ValueError(f"{path}: cards must be a list")
    ids: list[str] = []
    for index, card in enumerate(cards):
        card_id = card.get("id") if isinstance(card, dict) else None
        if not isinstance(card_id, str) or not card_id:
            raise ValueError(f"{path}: cards[{index}].id must be a non-empty string")
        ids.append(card_id)
    if len(ids) != len(set(ids)):
        raise ValueError(f"{path}: card ids must be unique")
    return set(ids)


def validate_reference(path: Path, data: dict[str, Any]) -> set[str]:
    if data.get("schema_version") != "gwent-reference-card-catalog-v1":
        raise ValueError(f"{path}: unexpected schema_version")
    if data.get("card_set") != "reference_catalog":
        raise ValueError(f"{path}: card_set must be reference_catalog")
    provenance = data.get("provenance")
    if not isinstance(provenance, dict) or not all(
        isinstance(provenance.get(key), str)
        for key in (
            "source_artifact",
            "source_sha256",
            "source_member",
            "source_member_sha256",
            "normalized_cards_sha256",
            "upstream_source",
            "game_version",
            "language",
            "license",
            "notice",
        )
    ):
        raise ValueError(f"{path}: incomplete provenance metadata")
    for key in ("source_sha256", "source_member_sha256", "normalized_cards_sha256"):
        digest = provenance[key]
        if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
            raise ValueError(f"{path}: provenance.{key} must be lowercase SHA-256")
    cards = data.get("cards")
    if not isinstance(cards, list) or not cards:
        raise ValueError(f"{path}: cards must be a non-empty list")
    if data.get("card_count") != len(cards):
        raise ValueError(f"{path}: card_count does not match cards length")

    previous_id = -1
    for index, card in enumerate(cards):
        prefix = f"{path}: cards[{index}]"
        if not isinstance(card, dict):
            raise ValueError(f"{prefix} must be an object")
        unknown = sorted(set(card) - REFERENCE_FIELDS)
        if unknown:
            raise ValueError(f"{prefix} has unknown fields: {', '.join(unknown)}")
        card_id = card.get("id")
        if not isinstance(card_id, str) or re.fullmatch(r"[1-9][0-9]*", card_id) is None:
            raise ValueError(f"{prefix}.id must be a positive numeric string")
        numeric_id = int(card_id)
        if numeric_id <= previous_id:
            raise ValueError(f"{path}: cards must be sorted by increasing numeric id")
        previous_id = numeric_id
        if not isinstance(card.get("name"), str) or not card["name"].strip():
            raise ValueError(f"{prefix}.name must be a non-empty string")
        if card.get("type") not in REFERENCE_TYPES:
            raise ValueError(f"{prefix}.type is invalid")
        if card.get("faction") not in REFERENCE_FACTIONS:
            raise ValueError(f"{prefix}.faction is invalid")
        if "secondary_faction" in card and card["secondary_faction"] not in REFERENCE_FACTIONS:
            raise ValueError(f"{prefix}.secondary_faction is invalid")
        for key in ("power", "armor", "provision"):
            if key in card and (not isinstance(card[key], int) or isinstance(card[key], bool)):
                raise ValueError(f"{prefix}.{key} must be an integer")
        for key in ("color", "rarity", "set", "description"):
            if key in card and (not isinstance(card[key], str) or not card[key]):
                raise ValueError(f"{prefix}.{key} must be a non-empty string")
        for key in ("categories", "related_card_ids"):
            if key in card and (
                not isinstance(card[key], list)
                or not all(isinstance(value, str) and value for value in card[key])
            ):
                raise ValueError(f"{prefix}.{key} must be a list of non-empty strings")
        if "effect_ids" in card or "metadata" in card:
            raise ValueError(f"{prefix} must not contain runtime effect bindings")

    cards_payload = json.dumps(cards, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    if hashlib.sha256(cards_payload).hexdigest() != provenance["normalized_cards_sha256"]:
        raise ValueError(f"{path}: normalized card content hash mismatch")
    return card_ids(path, data)


def validate_deck(path: Path, data: dict[str, Any], supported_by_id: dict[str, dict[str, Any]]) -> None:
    if data.get("schema_version") != "gwent-deck-v1":
        raise ValueError(f"{path}: schema_version must be gwent-deck-v1")
    if data.get("card_set") != "supported_cards":
        raise ValueError(f"{path}: card_set must be supported_cards")
    for key in ("leader", "stratagem"):
        card_id = data.get(key)
        if not isinstance(card_id, str) or card_id not in supported_by_id:
            raise ValueError(f"{path}: {key} must reference a supported card id")
    if supported_by_id[data["leader"]].get("type") != "leader":
        raise ValueError(f"{path}: leader must reference a supported leader card")
    if supported_by_id[data["stratagem"]].get("type") != "stratagem":
        raise ValueError(f"{path}: stratagem must reference a supported stratagem card")
    cards = data.get("cards")
    if not isinstance(cards, list):
        raise ValueError(f"{path}: cards must be a list")
    seen: set[str] = set()
    for index, entry in enumerate(cards):
        card_id = entry.get("id") if isinstance(entry, dict) else None
        count = entry.get("count") if isinstance(entry, dict) else None
        if not isinstance(card_id, str) or card_id not in supported_by_id:
            raise ValueError(f"{path}: cards[{index}].id must reference a supported card id")
        supported_card = supported_by_id[card_id]
        if supported_card.get("is_token"):
            raise ValueError(f"{path}: cards[{index}].id must not reference a runtime token")
        if supported_card.get("type") in {"leader", "stratagem"}:
            raise ValueError(f"{path}: cards[{index}].id must be a main-deck card")
        if card_id in seen:
            raise ValueError(f"{path}: duplicate main-deck card id {card_id}")
        seen.add(card_id)
        if not isinstance(count, int) or isinstance(count, bool) or count <= 0:
            raise ValueError(f"{path}: cards[{index}].count must be a positive integer")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=Path("data/cards/reference/all_cards.json"))
    parser.add_argument("--supported", type=Path, default=Path("data/cards/supported_cards.json"))
    parser.add_argument("--decks", type=Path, default=Path("data/decks"))
    args = parser.parse_args()

    reference = load_object(args.reference)
    supported = load_object(args.supported)
    reference_ids = validate_reference(args.reference, reference)
    if supported.get("card_set") != "supported_cards":
        raise ValueError(f"{args.supported}: card_set must be supported_cards")
    supported_ids = card_ids(args.supported, supported)
    supported_cards = supported.get("cards", [])
    supported_by_id = {
        card["id"]: card
        for card in supported_cards
        if isinstance(card, dict) and isinstance(card.get("id"), str)
    }
    catalog_backed_ids = {
        card["id"]
        for card in supported_cards
        if isinstance(card, dict) and not card.get("is_token")
    }
    derived_forms = {
        card["id"] for card in supported_cards
        if isinstance(card, dict)
        and (
            card.get("metadata", {}).get("derived_evolution_form") == "true"
            or card.get("metadata", {}).get("derived_choice_form") == "true"
        )
    }
    referenced_related_ids = {
        related_id
        for card in reference.get("cards", [])
        if isinstance(card, dict)
        for related_id in card.get("related_card_ids", [])
    }
    unsupported_derived_forms = sorted(derived_forms - referenced_related_ids)
    if unsupported_derived_forms:
        raise ValueError(
            "derived evolution forms must be related ids in the reference catalog: "
            + ", ".join(unsupported_derived_forms)
        )
    missing = sorted(catalog_backed_ids - reference_ids - derived_forms)
    if missing:
        raise ValueError(f"supported card ids missing from reference catalog: {', '.join(missing)}")

    deck_paths = sorted(args.decks.glob("*.json"))
    if not deck_paths:
        raise ValueError(f"{args.decks}: no deck manifests found")
    for deck_path in deck_paths:
        validate_deck(deck_path, load_object(deck_path), supported_by_id)

    print(
        f"card data valid: {len(reference_ids)} reference, "
        f"{len(supported_ids)} supported ({len(supported_ids - catalog_backed_ids)} runtime token), "
        f"{len(deck_paths)} deck manifest(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
