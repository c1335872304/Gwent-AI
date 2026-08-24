#!/usr/bin/env python3
"""Small, dependency-free validators for gwent golden trace artifacts."""
from __future__ import annotations

import re
from typing import Any

TRACE_SCHEMA_VERSION = "gwent-golden-trace-v1"
CHECKSUM_RE = re.compile(r"^[0-9a-f]{16}$")
VALID_DECISION_TYPES = {"none", "mulligan", "turn", "post_play", "effect_card", "place_row"}
VALID_ACTION_PREFIXES = {
    "pass",
    "end_turn",
    "play_hand",
    "discard_hand",
    "play_special",
    "mulligan",
    "keep_hand",
    "use_order",
    "use_leader",
    "choose_target",
    "choose_row",
}
VALID_STATE_ZONES = {"deck", "hand", "stay", "melee", "ranged", "cemetery", "banished", "leader"}


class TraceSchemaError(AssertionError):
    """Raised when a trace artifact violates the stable schema contract."""


def _fail(path: str, message: str) -> None:
    raise TraceSchemaError(f"{path}: {message}")


def _require(condition: bool, path: str, message: str) -> None:
    if not condition:
        _fail(path, message)


def _require_type(value: Any, expected: type | tuple[type, ...], path: str) -> None:
    if not isinstance(value, expected):
        if isinstance(expected, tuple):
            names = ", ".join(t.__name__ for t in expected)
        else:
            names = expected.__name__
        _fail(path, f"expected {names}, got {type(value).__name__}")


def _require_int(value: Any, path: str) -> None:
    _require_type(value, int, path)
    _require(not isinstance(value, bool), path, "expected int, got bool")


def validate_card(card: Any, path: str) -> None:
    _require_type(card, dict, path)
    for key in ("entity_id", "exists", "card_id", "name", "type", "owner", "controller", "has_power"):
        _require(key in card, path, f"missing {key}")
    _require_int(card["entity_id"], f"{path}.entity_id")
    _require_type(card["exists"], bool, f"{path}.exists")
    _require_type(card["card_id"], str, f"{path}.card_id")
    _require_type(card["name"], str, f"{path}.name")
    _require_type(card["type"], str, f"{path}.type")
    _require_int(card["owner"], f"{path}.owner")
    _require_int(card["controller"], f"{path}.controller")
    _require_type(card["has_power"], bool, f"{path}.has_power")
    if card["has_power"]:
        for key in ("base_power", "power"):
            _require(key in card, path, f"missing {key} for powered card")
            _require_int(card[key], f"{path}.{key}")
    _require("statuses" in card, path, "missing statuses")
    _require_type(card["statuses"], dict, f"{path}.statuses")


def validate_player(player: Any, index: int, path: str) -> None:
    _require_type(player, dict, path)
    for key in (
        "player_id",
        "passed",
        "leader_used",
        "round_wins",
        "mulligans_available",
        "cards_drawn_this_round",
        "melee_score",
        "ranged_score",
        "board_score",
        "zones",
    ):
        _require(key in player, path, f"missing {key}")
    _require(player["player_id"] == index, f"{path}.player_id", f"expected {index}")
    _require_type(player["passed"], bool, f"{path}.passed")
    _require_type(player["leader_used"], bool, f"{path}.leader_used")
    for key in ("round_wins", "mulligans_available", "cards_drawn_this_round", "melee_score", "ranged_score", "board_score"):
        _require_int(player[key], f"{path}.{key}")
    zones = player["zones"]
    _require_type(zones, dict, f"{path}.zones")
    _require(set(zones) == VALID_STATE_ZONES, f"{path}.zones", f"expected zones {sorted(VALID_STATE_ZONES)}, got {sorted(zones)}")
    for zone, cards in zones.items():
        if zone == "leader":
            validate_card(cards, f"{path}.zones.{zone}")
            continue
        _require_type(cards, list, f"{path}.zones.{zone}")
        for i, card in enumerate(cards):
            validate_card(card, f"{path}.zones.{zone}[{i}]")


def validate_pending_choice(value: Any, path: str) -> None:
    if value is None:
        return
    _require_type(value, dict, path)
    for key in ("kind", "player_id", "source_entity_id", "effect_id", "legal_card_targets", "legal_row_targets"):
        _require(key in value, path, f"missing {key}")
    _require_type(value["kind"], str, f"{path}.kind")
    _require_int(value["player_id"], f"{path}.player_id")
    _require_int(value["source_entity_id"], f"{path}.source_entity_id")
    _require_type(value["effect_id"], str, f"{path}.effect_id")
    _require_type(value["legal_card_targets"], list, f"{path}.legal_card_targets")
    _require_type(value["legal_row_targets"], list, f"{path}.legal_row_targets")
    for i, entity_id in enumerate(value["legal_card_targets"]):
        _require_int(entity_id, f"{path}.legal_card_targets[{i}]")
    for i, row in enumerate(value["legal_row_targets"]):
        _require_type(row, dict, f"{path}.legal_row_targets[{i}]")
        _require("side" in row and "zone" in row, f"{path}.legal_row_targets[{i}]", "missing side/zone")
        _require_int(row["side"], f"{path}.legal_row_targets[{i}].side")
        _require_type(row["zone"], str, f"{path}.legal_row_targets[{i}].zone")


def validate_state(state: Any, path: str) -> None:
    _require_type(state, dict, path)
    for key in (
        "status",
        "phase",
        "round_no",
        "turn_no",
        "current_player_id",
        "starting_player_id",
        "round_starting_player_id",
        "winner_id",
        "next_entity_id",
        "pending_choice",
        "players",
    ):
        _require(key in state, path, f"missing {key}")
    _require_type(state["status"], str, f"{path}.status")
    _require_type(state["phase"], str, f"{path}.phase")
    for key in ("round_no", "turn_no", "current_player_id", "starting_player_id", "round_starting_player_id", "next_entity_id"):
        _require_int(state[key], f"{path}.{key}")
    if state["winner_id"] is not None:
        _require_int(state["winner_id"], f"{path}.winner_id")
    validate_pending_choice(state["pending_choice"], f"{path}.pending_choice")
    players = state["players"]
    _require_type(players, list, f"{path}.players")
    _require(len(players) == 2, f"{path}.players", "expected two players")
    for i, player in enumerate(players):
        validate_player(player, i, f"{path}.players[{i}]")


def validate_target(target: Any, path: str) -> None:
    _require_type(target, dict, path)
    for key in ("kind", "side", "zone", "entity_id"):
        _require(key in target, path, f"missing {key}")
    _require_type(target["kind"], str, f"{path}.kind")
    _require_int(target["side"], f"{path}.side")
    _require_type(target["zone"], str, f"{path}.zone")
    _require_int(target["entity_id"], f"{path}.entity_id")


def validate_action(action: Any, path: str) -> None:
    if action is None:
        return
    _require_type(action, dict, path)
    for key in ("type", "player_id", "source_entity_id", "target"):
        _require(key in action, path, f"missing {key}")
    _require_type(action["type"], str, f"{path}.type")
    _require_int(action["player_id"], f"{path}.player_id")
    _require_int(action["source_entity_id"], f"{path}.source_entity_id")
    validate_target(action["target"], f"{path}.target")


def validate_result(result: Any, path: str) -> None:
    if result is None:
        return
    _require_type(result, dict, path)
    for key in ("applied", "status", "message", "events"):
        _require(key in result, path, f"missing {key}")
    _require_type(result["applied"], bool, f"{path}.applied")
    _require_type(result["status"], str, f"{path}.status")
    _require_type(result["message"], str, f"{path}.message")
    _require_type(result["events"], list, f"{path}.events")


def validate_legal_surface(surface: Any, path: str) -> None:
    _require_type(surface, dict, path)
    for key in ("decision_type", "player_id", "actions"):
        _require(key in surface, path, f"missing {key}")
    _require_type(surface["decision_type"], str, f"{path}.decision_type")
    _require(surface["decision_type"] in VALID_DECISION_TYPES, f"{path}.decision_type", f"unknown decision type {surface['decision_type']!r}")
    _require_int(surface["player_id"], f"{path}.player_id")
    _require_type(surface["actions"], list, f"{path}.actions")
    _require(surface["actions"] == sorted(surface["actions"]), f"{path}.actions", "actions must be sorted for deterministic diffs")
    _require(len(surface["actions"]) == len(set(surface["actions"])), f"{path}.actions", "actions must be unique")
    for i, action in enumerate(surface["actions"]):
        _require_type(action, str, f"{path}.actions[{i}]")
        prefix = action.split(":", 1)[0]
        _require(prefix in VALID_ACTION_PREFIXES, f"{path}.actions[{i}]", f"unknown action prefix {prefix!r}")


def validate_trace_document(doc: Any, *, require_legal_surface: bool = False) -> None:
    _require_type(doc, dict, "$")
    for key in ("schema_version", "engine", "scenario", "seed", "starting_player_id", "steps"):
        _require(key in doc, "$", f"missing {key}")
    _require(doc["schema_version"] == TRACE_SCHEMA_VERSION, "$.schema_version", f"expected {TRACE_SCHEMA_VERSION}")
    _require_type(doc["engine"], str, "$.engine")
    _require_type(doc["scenario"], str, "$.scenario")
    _require_int(doc["seed"], "$.seed")
    _require_int(doc["starting_player_id"], "$.starting_player_id")
    _require_type(doc["steps"], list, "$.steps")
    _require(len(doc["steps"]) >= 1, "$.steps", "trace must include at least initial state")
    for i, step in enumerate(doc["steps"]):
        path = f"$.steps[{i}]"
        _require_type(step, dict, path)
        for key in ("step_index", "label", "checksum", "action", "result", "state"):
            _require(key in step, path, f"missing {key}")
        _require(step["step_index"] == i, f"{path}.step_index", f"expected {i}")
        _require_type(step["label"], str, f"{path}.label")
        _require_type(step["checksum"], str, f"{path}.checksum")
        _require(CHECKSUM_RE.match(step["checksum"]) is not None, f"{path}.checksum", "expected 16 lowercase hex chars")
        validate_action(step["action"], f"{path}.action")
        validate_result(step["result"], f"{path}.result")
        if require_legal_surface or "legal_surface" in step:
            _require("legal_surface" in step, path, "missing legal_surface")
            validate_legal_surface(step["legal_surface"], f"{path}.legal_surface")
        validate_state(step["state"], f"{path}.state")
