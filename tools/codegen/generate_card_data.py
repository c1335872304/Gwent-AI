#!/usr/bin/env python3
"""Generate checked-in C++ card definition data from JSON card manifests."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

KNOWN_TYPES = {"unit", "special", "artifact", "leader", "stratagem"}
KNOWN_FACTIONS = {
    "northern_realms": "NorthernRealms",
    "nilfgaard": "Nilfgaard",
    "monsters": "Monsters",
    "scoiatael": "Scoiatael",
    "skellige": "Skellige",
    "syndicate": "Syndicate",
    "neutral": "Neutral",
}


def cpp_string(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )
    return f'"{escaped}"'


def cpp_sv(value: str) -> str:
    return cpp_string(value)


def enum_faction(value: str | None) -> str:
    key = (value or "neutral").lower()
    if key not in KNOWN_FACTIONS:
        raise ValueError(f"unknown faction: {value}")
    return f"Faction::{KNOWN_FACTIONS[key]}"


def make_symbol(card: dict[str, Any]) -> str:
    text = f"{card['id']}_{card['name']}"
    asciiish = re.sub(r"[^0-9A-Za-z]+", "_", text).strip("_").lower()
    if not asciiish:
        asciiish = f"card_{card['id']}"
    if asciiish[0].isdigit():
        asciiish = "card_" + asciiish
    return asciiish


def validate_card_manifest(data: dict[str, Any]) -> None:
    if data.get("schema_version") != "gwent-card-data-v1":
        raise ValueError("schema_version must be gwent-card-data-v1")
    if data.get("card_set") != "supported_cards":
        raise ValueError("card_set must be supported_cards")
    cards = data.get("cards")
    if not isinstance(cards, list) or not cards:
        raise ValueError("cards must be a non-empty list")

    seen: set[str] = set()
    for index, card in enumerate(cards):
        prefix = f"cards[{index}]"
        card_id = card.get("id")
        if not isinstance(card_id, str) or not card_id:
            raise ValueError(f"{prefix}.id must be a non-empty string")
        if card_id in seen:
            raise ValueError(f"duplicate card id: {card_id}")
        seen.add(card_id)

        card_type = card.get("type")
        if card_type not in KNOWN_TYPES:
            raise ValueError(f"{prefix}.type must be one of {sorted(KNOWN_TYPES)}")
        if card_type == "unit" and not isinstance(card.get("power"), int):
            raise ValueError(f"{prefix}.power is required for unit cards")
        if card_type != "unit" and "power" in card:
            raise ValueError(f"{prefix}.power must only be used by unit cards")
        for list_key in ("categories", "effect_ids"):
            if list_key in card and (not isinstance(card[list_key], list) or not all(isinstance(v, str) for v in card[list_key])):
                raise ValueError(f"{prefix}.{list_key} must be a list of strings")
        if "metadata" in card:
            metadata = card["metadata"]
            if not isinstance(metadata, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in metadata.items()):
                raise ValueError(f"{prefix}.metadata must be an object of string:string")


def validate_deck_manifest(data: dict[str, Any], card_ids: set[str], expected_id: str) -> None:
    if data.get("schema_version") != "gwent-deck-v1":
        raise ValueError("deck schema_version must be gwent-deck-v1")
    if data.get("card_set") != "supported_cards":
        raise ValueError("deck card_set must be supported_cards")
    if data.get("id") != expected_id:
        raise ValueError(f"deck id must be {expected_id}")
    for key in ("leader", "stratagem"):
        value = data.get(key)
        if not isinstance(value, str) or value not in card_ids:
            raise ValueError(f"deck.{key} must reference a supported card id")
    cards = data.get("cards")
    if not isinstance(cards, list):
        raise ValueError("deck.cards must be a list")
    for index, entry in enumerate(cards):
        prefix = f"deck.cards[{index}]"
        card_id = entry.get("id") if isinstance(entry, dict) else None
        count = entry.get("count") if isinstance(entry, dict) else None
        if not isinstance(card_id, str) or card_id not in card_ids:
            raise ValueError(f"{prefix}.id must reference a supported card id")
        if not isinstance(count, int) or count <= 0:
            raise ValueError(f"{prefix}.count must be a positive integer")


def card_function(card: dict[str, Any]) -> str:
    name = make_symbol(card)
    card_type = card["type"]
    lines: list[str] = [f"CardDefinition make_{name}() {{"]
    if card_type == "unit":
        lines.append(
            f"    CardDefinition definition = make_unit_definition({cpp_string(card['id'])}, {cpp_string(card['name'])}, {card['power']}, {enum_faction(card.get('faction'))});"
        )
    elif card_type == "special":
        lines.append(
            f"    CardDefinition definition = make_special_definition({cpp_string(card['id'])}, {cpp_string(card['name'])}, {enum_faction(card.get('faction'))});"
        )
    elif card_type == "artifact":
        lines.append(
            f"    CardDefinition definition = make_artifact_definition({cpp_string(card['id'])}, {cpp_string(card['name'])}, {enum_faction(card.get('faction'))});"
        )
    elif card_type == "leader":
        lines.append(
            f"    CardDefinition definition = make_leader_definition({cpp_string(card['id'])}, {cpp_string(card['name'])}, {enum_faction(card.get('faction'))});"
        )
    elif card_type == "stratagem":
        lines.append(
            f"    CardDefinition definition = make_stratagem_definition({cpp_string(card['id'])}, {cpp_string(card['name'])});"
        )
    else:
        raise AssertionError(card_type)

    if "provision" in card:
        lines.append(f"    definition.provision = {int(card['provision'])};")
    if "armor" in card:
        lines.append(f"    definition.base_armor = {int(card['armor'])};")
    if card.get("doomed"):
        lines.append("    definition.doomed = true;")
    if card.get("is_token"):
        lines.append("    definition.is_token = true;")
    if card.get("color", ""):
        lines.append(f"    definition.color = {cpp_string(card['color'])};")
    if card.get("rarity", ""):
        lines.append(f"    definition.rarity = {cpp_string(card['rarity'])};")
    if card.get("description", ""):
        lines.append(f"    definition.description = {cpp_string(card['description'])};")

    effect_ids = card.get("effect_ids", [])
    if effect_ids:
        inner = ", ".join(cpp_string(v) for v in effect_ids)
        lines.append(f"    definition.effect_ids = {{{inner}}};")
    categories = card.get("categories", [])
    if categories:
        inner = ", ".join(cpp_string(v) for v in categories)
        lines.append(f"    definition.categories = {{{inner}}};")
        lines.append("    definition.main_category = definition.categories.front();")

    for key, value in sorted(card.get("metadata", {}).items()):
        lines.append(f"    definition.metadata.emplace({cpp_string(key)}, {cpp_string(value)});")

    lines.append("    return definition;")
    lines.append("}")
    return "\n".join(lines)


def render_header(data: dict[str, Any], manifest_path: str, decks: list[tuple[str, dict[str, Any]]]) -> str:
    deck_paths = ", ".join(path for path, _deck in decks)
    return "\n".join([
        "#pragma once",
        "",
        f"// Generated by tools/codegen/generate_card_data.py from {manifest_path}",
        f"// and {deck_paths}. Do not edit this file by hand.",
        "",
        "#include <string_view>",
        "#include <vector>",
        "",
        "#include \"gwent/core/card_definition.hpp\"",
        "#include \"gwent/game/setup.hpp\"",
        "",
        "namespace gwent {",
        "namespace generated {",
        "",
        "[[nodiscard]] std::string_view supported_card_data_schema_version() noexcept;",
        "[[nodiscard]] std::string_view supported_card_data_source_path() noexcept;",
        "[[nodiscard]] std::string_view deck_a_deck_data_source_path() noexcept;",
        "[[nodiscard]] std::string_view deck_b_deck_data_source_path() noexcept;",
        "[[nodiscard]] std::vector<std::string_view> supported_deck_ids();",
        "[[nodiscard]] DeckSpec make_supported_deck_spec(std::string_view id);",
        "[[nodiscard]] CardDefinition make_supported_card_definition(std::string_view id);",
        "[[nodiscard]] std::vector<CardDefinition> make_supported_card_definitions();",
        "[[nodiscard]] DeckSpec make_deck_a_deck_spec();",
        "[[nodiscard]] DeckSpec make_deck_b_deck_spec();",
        "",
        "}  // namespace generated",
        "}  // namespace gwent",
        "",
    ])


def render_cpp(data: dict[str, Any], decks: list[tuple[str, dict[str, Any]]], manifest_path: str) -> str:
    cards = data["cards"]
    by_id = {card["id"]: card for card in cards}
    function_blocks = "\n\n".join(card_function(card) for card in cards)
    vector_lines = []
    for card in cards:
        vector_lines.append(f"        make_{make_symbol(card)}(),")
    lookup_lines = []
    for card in cards:
        lookup_lines.append(f"    if (id == {cpp_sv(card['id'])}) {{")
        lookup_lines.append(f"        return make_{make_symbol(card)}();")
        lookup_lines.append("    }")
    def render_deck_lines(spec: dict[str, Any]) -> list[str]:
        lines = [
        "    DeckSpec deck;",
        f"    deck.leader = make_{make_symbol(by_id[spec['leader']])}();",
        f"    deck.stratagem = make_{make_symbol(by_id[spec['stratagem']])}();",
        ]
        lines.append("    deck.cards = {")
        for entry in spec["cards"]:
            card = by_id[entry["id"]]
            lines.append(f"        {{make_{make_symbol(card)}(), {int(entry['count'])}}},")
        lines.append("    };")
        lines.append("    return deck;")
        return lines
    deck_by_id = {deck["id"]: (path, deck) for path, deck in decks}
    deck_functions: list[str] = []
    for _path, spec in decks:
        deck_functions.extend([
            f"DeckSpec make_{spec['id']}_deck_spec() {{",
            *render_deck_lines(spec),
            "}",
            "",
        ])
    registry_lookup: list[str] = []
    for _path, spec in decks:
        registry_lookup.extend([
            f"    if (id == {cpp_sv(spec['id'])}) {{",
            f"        return make_{spec['id']}_deck_spec();",
            "    }",
        ])

    return "\n".join([
        "#include \"gwent/generated/supported_card_data.hpp\"",
        "",
        "#include <stdexcept>",
        "#include <string>",
        "#include <utility>",
        "",
        "namespace gwent {",
        "namespace generated {",
        "namespace {",
        "",
        function_blocks,
        "",
        "}  // namespace",
        "",
        "std::string_view supported_card_data_schema_version() noexcept {",
        f"    return {cpp_sv(data['schema_version'])};",
        "}",
        "",
        "std::string_view supported_card_data_source_path() noexcept {",
        f"    return {cpp_sv(manifest_path)};",
        "}",
        "",
        "std::string_view deck_a_deck_data_source_path() noexcept {",
        f"    return {cpp_sv(deck_by_id['deck_a'][0])};",
        "}",
        "",
        "std::string_view deck_b_deck_data_source_path() noexcept {",
        f"    return {cpp_sv(deck_by_id['deck_b'][0])};",
        "}",
        "",
        "std::vector<std::string_view> supported_deck_ids() {",
        "    return {",
        *[f"        {cpp_sv(spec['id'])}," for _path, spec in decks],
        "    };",
        "}",
        "",
        "DeckSpec make_supported_deck_spec(std::string_view id) {",
        *registry_lookup,
        "    throw std::out_of_range(\"unknown supported deck id: \" + std::string(id));",
        "}",
        "",
        "CardDefinition make_supported_card_definition(std::string_view id) {",
        *lookup_lines,
        "    throw std::out_of_range(\"unknown supported card id: \" + std::string(id));",
        "}",
        "",
        "std::vector<CardDefinition> make_supported_card_definitions() {",
        "    return {",
        *vector_lines,
        "    };",
        "}",
        "",
        *deck_functions,
        "",
        "}  // namespace generated",
        "}  // namespace gwent",
        "",
    ])


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        data = json.load(handle)
    validate_card_manifest(data)
    return data


def load_deck(path: Path, card_ids: set[str], expected_id: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        data = json.load(handle)
    validate_deck_manifest(data, card_ids, expected_id)
    return data


def write_or_check(path: Path, content: str, check: bool) -> bool:
    if check:
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current != content:
            print(f"out of date: {path}", file=sys.stderr)
            return False
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default="data/cards/supported_cards.json")
    parser.add_argument("--deck-dir", default="data/decks")
    parser.add_argument("--header", default="include/gwent/generated/supported_card_data.hpp")
    parser.add_argument("--source", default="src/generated/supported_card_data.cpp")
    parser.add_argument("--check", action="store_true", help="fail if generated files differ from the manifest")
    args = parser.parse_args()

    root = Path.cwd()
    manifest = root / args.manifest
    data = load_manifest(manifest)
    card_ids = {str(card["id"]) for card in data["cards"]}
    decks: list[tuple[str, dict[str, Any]]] = []
    for path in sorted((root / args.deck_dir).glob("*.json")):
        relative = path.relative_to(root).as_posix()
        expected_id = path.stem
        decks.append((relative, load_deck(path, card_ids, expected_id)))
    if not decks or {deck["id"] for _path, deck in decks} < {"deck_a", "deck_b"}:
        raise ValueError("deck registry must contain deck_a and deck_b")
    header_content = render_header(data, args.manifest, decks)
    source_content = render_cpp(data, decks, args.manifest)
    ok_header = write_or_check(root / args.header, header_content, args.check)
    ok_source = write_or_check(root / args.source, source_content, args.check)
    return 0 if ok_header and ok_source else 1


if __name__ == "__main__":
    raise SystemExit(main())
