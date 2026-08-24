#!/usr/bin/env python3
"""Export a Python-core trace in the same high-level schema used by C++.

M16 adds semantic replay for the shared ``.trace`` scripts consumed by the C++
``gwent_trace_runner``.  A single semantic line such as
``play_hand p=0 h=0 row=melee`` may expand to several Python decision selections
(``play`` -> hand card -> placement -> optional end_turn), but it appends one
trace step for the original semantic action.

The legacy option-index replay format is still supported for debugging with
``option index=N`` lines.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Callable


def add_python_core(path: Path | None) -> None:
    if path is not None:
        sys.path.insert(0, str(path.resolve()))


def status_value(value: Any) -> str:
    return getattr(value, "name", str(value))


def zone_cards(state: Any, ids: list[int]) -> list[dict[str, Any]]:
    return [card_ref(state, eid) for eid in ids]


def statuses(card: Any) -> dict[str, int]:
    s = card.state
    result: dict[str, int] = {}
    bool_fields = {
        "Shield": "shield",
        "Infiltration": "infiltration",
        "Spying": "spying",
        "Locked": "locked",
        "Resilience": "resilience",
        "Doomed": "doomed",
        "Veil": "veil",
        "Bounty": "bounty",
        "Immune": "immune",
        "Defender": "defender",
        "Rupture": "rupture",
    }
    for name, attr in bool_fields.items():
        if bool(getattr(s, attr, False)):
            result[name] = 1
    for name, attr in (("Poison", "poison"), ("Bleeding", "bleeding"), ("Vitality", "vitality")):
        amount = int(getattr(s, attr, 0))
        if amount > 0:
            result[name] = amount
    return result


def card_ref(state: Any, eid: int) -> dict[str, Any]:
    card = state.cards.get(eid)
    if card is None:
        return {"entity_id": eid, "exists": False}
    return {
        "entity_id": eid,
        "exists": True,
        "card_id": str(card.definition.id),
        "name": card.definition.name,
        "type": status_value(card.definition.card_type),
        "owner": card.owner_id,
        "controller": card.controller_id,
        "has_power": bool(card.has_power),
        "base_power": int(card.state.base_power),
        "power": int(card.state.power),
        "armor": int(card.state.armor),
        "order_charges": int(getattr(card.state, "order_charges", 0)),
        "timer": int(getattr(card.state, "timer", 0)),
        "cooldown": int(getattr(card.state, "cooldown", 0)),
        "countdown": int(getattr(card.state, "countdown", 0)),
        "statuses": statuses(card),
    }


def row_score(state: Any, pid: int, zone: Any) -> int:
    total = 0
    for eid in state.players[pid].rows[zone]:
        card = state.cards[eid]
        if bool(card.has_power):
            total += int(card.state.power)
    return total


def player_summary(state: Any, pid: int) -> dict[str, Any]:
    from gwent.model.enums import Zone

    p = state.players[pid]
    melee_score = row_score(state, pid, Zone.MELEE)
    ranged_score = row_score(state, pid, Zone.RANGED)
    mulligans_available = getattr(p, "mulligans_available", None)
    cards_drawn_this_round = getattr(p, "cards_drawn_this_round", None)
    return {
        "player_id": pid,
        "passed": bool(p.passed),
        "leader_used": bool(p.leader_used),
        "round_wins": int(p.round_wins),
        "mulligans_available": mulligans_available,
        "cards_drawn_this_round": cards_drawn_this_round,
        "melee_score": melee_score,
        "ranged_score": ranged_score,
        "board_score": melee_score + ranged_score,
        "zones": {
            "deck": zone_cards(state, list(p.deck)),
            "hand": zone_cards(state, list(p.hand)),
            "stay": zone_cards(state, list(p.stay)),
            "melee": zone_cards(state, list(p.rows[Zone.MELEE])),
            "ranged": zone_cards(state, list(p.rows[Zone.RANGED])),
            "cemetery": zone_cards(state, list(p.cemetery)),
            "banished": zone_cards(state, list(p.banished)),
            "leader": None if p.leader is None else card_ref(state, p.leader),
        },
    }


def state_snapshot(state: Any) -> dict[str, Any]:
    listeners = getattr(state, "listeners", {})
    return {
        "status": status_value(state.status),
        "phase": status_value(state.phase),
        "round_no": int(state.round_no),
        "turn_no": int(state.turn_no),
        "current_player_id": int(state.current_player_id),
        "starting_player_id": int(state.starting_player_id),
        "round_starting_player_id": int(state.round_starting_player_id),
        "winner_id": state.winner_id,
        "next_entity_id": int(state.next_entity_id),
        "pending_choice": None,
        "players": [player_summary(state, 0), player_summary(state, 1)],
        "listeners": [
            {
                "listener_id": int(listener.listener_id),
                "event": listener.event,
                "handler": listener.handler,
                "source_id": listener.source_id,
                "once": bool(listener.once),
            }
            for _, listener in sorted(listeners.items())
        ],
    }


def checksum(snapshot: dict[str, Any]) -> str:
    payload = json.dumps(snapshot, ensure_ascii=False, sort_keys=False, separators=(",", ":"))
    h = 14695981039346656037
    for byte in payload.encode("utf-8"):
        h ^= byte
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"




def zone_lower(value: Any) -> str:
    return getattr(value, "name", str(value)).lower()


def card_signature_for_legal(state: Any, entity_id: int) -> str:
    card = state.cards.get(int(entity_id))
    text = f"eid={int(entity_id)}"
    if card is not None:
        text += f":cid={card.definition.id}:name={card.definition.name}"
    return text


def legal_surface(kernel: Any) -> dict[str, Any]:
    # This is intentionally a normalized comparison surface, not a dump of the
    # Python hierarchical decision tree.  It expands Python's PLAY -> CARD ->
    # LOCATION path into the same atomic play_hand entries used by C++ while
    # keeping post-play/pending decisions as explicit surfaces.
    d = kernel.current_decision()
    if d is None:
        return {"decision_type": "none", "player_id": int(kernel.state.current_player_id), "actions": []}

    kind = getattr(d.kind, "value", str(d.kind))
    player_id = int(d.actor_id)
    actions: list[str] = []

    def add_order_and_leader() -> None:
        for entity_id in kernel._ready_order_cards(player_id):
            actions.append("use_order:" + card_signature_for_legal(kernel.state, int(entity_id)))
        leader = kernel._ready_leader(player_id)
        if leader is not None:
            actions.append("use_leader:" + card_signature_for_legal(kernel.state, int(leader)))

    if kind == "turn":
        # C++ currently has no public general discard action.  Python's DISCARD
        # option is intentionally excluded from this parity surface until that
        # gameplay policy is resolved.
        if any(getattr(o.kind, "value", str(o.kind)) == "pass" for o in d.options):
            actions.append("pass")
        add_order_and_leader()
        if any(getattr(o.kind, "value", str(o.kind)) == "play" for o in d.options):
            for entity_id in kernel._playable_hand_cards(player_id):
                card = kernel.state.cards[int(entity_id)]
                if getattr(card.definition.card_type, "name", "") == "SPECIAL":
                    actions.append("play_hand:" + card_signature_for_legal(kernel.state, int(entity_id)) + ":target=none")
                else:
                    for loc in kernel._legal_locations(player_id, int(entity_id)):
                        actions.append(
                            "play_hand:" + card_signature_for_legal(kernel.state, int(entity_id))
                            + f":side={int(loc.side)}:row={zone_lower(loc.zone)}"
                        )
        decision_type = "turn"
    elif kind == "post_play":
        add_order_and_leader()
        if any(getattr(o.kind, "value", str(o.kind)) == "end_turn" for o in d.options):
            actions.append("end_turn")
        decision_type = "post_play"
    elif kind == "place":
        for option in d.options:
            if getattr(option.kind, "value", str(option.kind)) != "location":
                continue
            loc = option.value
            actions.append(f"choose_row:side={int(loc.side)}:row={zone_lower(loc.zone)}")
        decision_type = "place_row"
    elif kind in {"effect", "effect_multi"}:
        # Most Deck A target choices are entity choices; Dettlaff/Naglfar/Geels
        # row choices are represented as Location candidates.
        saw_location = False
        for option in d.options:
            opt_kind = getattr(option.kind, "value", str(option.kind))
            if opt_kind == "entity":
                actions.append("choose_target:" + card_signature_for_legal(kernel.state, int(option.value)))
            elif opt_kind == "location":
                saw_location = True
                loc = option.value
                actions.append(f"choose_row:side={int(loc.side)}:row={zone_lower(loc.zone)}")
            elif opt_kind == "value" and isinstance(option.value, (list, tuple)) and len(option.value) == 2:
                value_side, value_zone = option.value
                saw_location = True
                actions.append(f"choose_row:side={int(value_side)}:row={zone_lower(value_zone)}")
            elif opt_kind == "finish":
                actions.append("finish")
        decision_type = "place_row" if saw_location else "effect_card"
    elif kind == "play_card":
        for entity_id in kernel._playable_hand_cards(player_id):
            actions.append("select_hand:" + card_signature_for_legal(kernel.state, int(entity_id)))
        decision_type = "select_hand"
    else:
        decision_type = kind

    return {"decision_type": decision_type, "player_id": player_id, "actions": sorted(set(actions))}

def append_step(doc: dict[str, Any], label: str, kernel: Any, action: Any = None, result: Any = None, include_legal_surface: bool = False) -> None:
    state = state_snapshot(kernel.state)
    doc["steps"].append(
        {
            "step_index": len(doc["steps"]),
            "label": label,
            "checksum": checksum(state),
            "action": action,
            "result": result,
            **({"legal_surface": legal_surface(kernel)} if include_legal_surface else {}),
            "state": state,
        }
    )


# ---------------------------------------------------------------------------
# Shared .trace semantic replay
# ---------------------------------------------------------------------------


def strip_comment(line: str) -> str:
    return line.split("#", 1)[0].strip()


def parse_kv_line(line: str) -> tuple[str, dict[str, str]] | None:
    line = strip_comment(line)
    if not line:
        return None
    parts = line.split()
    op = parts[0]
    kv: dict[str, str] = {}
    for token in parts[1:]:
        if "=" not in token:
            # Preserve old 'option 0' spelling in the legacy parser.
            kv.setdefault("_positional", token)
            continue
        key, value = token.split("=", 1)
        kv[key] = value
    return op, kv


def parse_int(kv: dict[str, str], *keys: str, default: int | None = None) -> int:
    for key in keys:
        if key in kv:
            return int(kv[key])
    if default is None:
        raise ValueError(f"missing integer argument among {keys}")
    return default


def parse_side(value: str, actor: int) -> int:
    value = value.lower()
    if value in {"self", "me", "actor"}:
        return actor
    if value in {"enemy", "opponent"}:
        return 1 - actor
    if value in {"0", "1"}:
        return int(value)
    raise ValueError(f"unknown side: {value}")


def normalize_zone(value: str) -> str:
    value = value.lower()
    aliases = {"graveyard": "cemetery", "gy": "cemetery"}
    return aliases.get(value, value).upper()


def zone_list(state: Any, side: int, zone_name: str) -> list[int]:
    from gwent.model.enums import Zone

    p = state.players[side]
    zone_name = normalize_zone(zone_name)
    if zone_name == "DECK":
        return list(p.deck)
    if zone_name == "HAND":
        return list(p.hand)
    if zone_name == "STAY":
        return list(p.stay)
    if zone_name == "MELEE":
        return list(p.rows[Zone.MELEE])
    if zone_name == "RANGED":
        return list(p.rows[Zone.RANGED])
    if zone_name == "CEMETERY":
        return list(p.cemetery)
    if zone_name == "BANISHED":
        return list(p.banished)
    raise ValueError(f"zone is not index-addressable: {zone_name}")


def entity_at(state: Any, side: int, zone_name: str, index: int) -> int:
    cards = zone_list(state, side, zone_name)
    if index < 0 or index >= len(cards):
        raise ValueError(f"zone ref out of range: side={side} zone={zone_name} index={index}")
    return int(cards[index])


def parse_ref(state: Any, spec: str, actor: int) -> int:
    parts = spec.split(":")
    if len(parts) != 3:
        raise ValueError("expected card ref side:zone:index")
    side = parse_side(parts[0], actor)
    zone = normalize_zone(parts[1])
    return entity_at(state, side, zone, int(parts[2]))




def has_card_selector(kv: dict[str, str]) -> bool:
    return any(
        key in kv
        for key in (
            "entity", "entity_id", "eid",
            "card_id", "cid", "name", "card_name",
            "target_card_id", "target_cid", "target_name",
        )
    )


def selector_value(kv: dict[str, str], *keys: str) -> str | None:
    for key in keys:
        if key in kv:
            return kv[key]
    return None


def select_entity_from_candidates(state: Any, candidates: list[int], kv: dict[str, str], context: str) -> int | None:
    if not has_card_selector(kv):
        return None
    entity_text = selector_value(kv, "entity", "entity_id", "eid")
    if entity_text is not None:
        entity = int(entity_text)
        if entity not in candidates:
            raise ValueError(f"{context} selector entity is not legal in this candidate set: {entity}")
        return entity

    card_id = selector_value(kv, "card_id", "cid", "target_card_id", "target_cid")
    name = selector_value(kv, "name", "card_name", "target_name")
    copy = int(kv.get("copy", "0"))
    matched_copy = 0
    for entity in candidates:
        card = state.cards.get(int(entity))
        if card is None:
            continue
        if card_id is not None and str(card.definition.id) != str(card_id):
            continue
        if name is not None and str(card.definition.name) != str(name):
            continue
        if matched_copy == copy:
            return int(entity)
        matched_copy += 1
    raise ValueError(f"{context} selector did not match any candidate; card_id={card_id!r} name={name!r}")


def entity_from_zone_selector_or_index(
    state: Any,
    side: int,
    zone_name: str,
    kv: dict[str, str],
    context: str,
    index_key: str,
    fallback_key: str,
    default_index: int,
) -> int:
    candidates = zone_list(state, side, zone_name)
    selected = select_entity_from_candidates(state, candidates, kv, context)
    if selected is not None:
        return selected
    index = parse_int(kv, index_key, fallback_key, default=default_index)
    return entity_at(state, side, zone_name, index)


def current_decision(env: Any) -> dict[str, Any]:
    d = env.decision()
    if d is None:
        raise ValueError("no current Python decision")
    return d


def select_option(env: Any, predicate: Callable[[dict[str, Any]], bool], label: str) -> dict[str, Any]:
    d = current_decision(env)
    matches = [(i, o) for i, o in enumerate(d["options"]) if predicate(o)]
    if not matches:
        available = [(i, o.get("kind"), o.get("payload")) for i, o in enumerate(d["options"])]
        raise ValueError(f"could not find option for {label}; decision={d['kind']} options={available}")
    index = matches[0][0]
    return env.step(index).get("info")


def option_entity_id(option: dict[str, Any]) -> int | None:
    payload = option.get("payload")
    if isinstance(payload, dict):
        value = payload.get("entity_id")
        return None if value is None else int(value)
    return None


def option_location(option: dict[str, Any]) -> dict[str, Any] | None:
    payload = option.get("payload")
    if isinstance(payload, dict) and payload.get("type") == "location":
        return payload
    return None


def choose_turn_button(env: Any, kind: str) -> dict[str, Any]:
    return select_option(env, lambda o: o.get("kind") == kind, kind)


def choose_card_entity(env: Any, entity_id: int, expected_kind: str = "card") -> dict[str, Any]:
    return select_option(
        env,
        lambda o: o.get("kind") == expected_kind and option_entity_id(o) == entity_id,
        f"{expected_kind} entity={entity_id}",
    )


def choose_location(env: Any, side: int, row: str, index: int | None = None) -> dict[str, Any]:
    row = normalize_zone(row)
    d = current_decision(env)
    matches: list[tuple[int, dict[str, Any]]] = []
    for i, option in enumerate(d["options"]):
        loc = option_location(option)
        if loc is None:
            continue
        if int(loc.get("side")) != side or normalize_zone(str(loc.get("zone"))) != row:
            continue
        if index is not None and int(loc.get("index")) != index:
            continue
        matches.append((i, option))
    if not matches:
        available = [(i, o.get("kind"), o.get("payload")) for i, o in enumerate(d["options"])]
        raise ValueError(f"could not find location side={side} row={row} index={index}; decision={d['kind']} options={available}")
    # C++ ActionTarget names a row, not an insertion index, and the current
    # reducer appends to that row.  When the semantic script does not request an
    # explicit index, select the last legal Python insertion slot to match that
    # row-append behavior.
    chosen = matches[-1] if index is None else matches[0]
    return env.step(chosen[0]).get("info")


def choose_pending_row(env: Any, side: int, row: str) -> dict[str, Any]:
    d = current_decision(env)
    if d.get("kind") not in {"effect", "effect_multi"}:
        # Python deck-play may expose row placement as a normal place decision
        # rather than an effect/value decision. Use the same location chooser so
        # named traces stay engine-agnostic.
        return choose_location(env, side, row)
    row_norm = normalize_zone(row)

    def row_value_matches(option: dict[str, Any]) -> bool:
        payload = option.get("payload")
        if isinstance(payload, dict) and payload.get("type") == "value":
            value = payload.get("value")
            if isinstance(value, (list, tuple)) and len(value) == 2:
                value_side, value_zone = value
                return int(value_side) == side and normalize_zone(str(status_value(value_zone))) == row_norm
        return False

    matches = [(i, o) for i, o in enumerate(d["options"]) if row_value_matches(o)]
    if matches:
        return env.step(matches[0][0]).get("info")
    return choose_location(env, side, row)

def choose_pending_target(env: Any, target_id: int) -> dict[str, Any]:
    d = current_decision(env)
    if d.get("kind") not in {"effect", "effect_multi"}:
        raise ValueError(f"expected pending effect decision, got {d.get('kind')}")
    return select_option(env, lambda o: option_entity_id(o) == target_id, f"pending target entity={target_id}")


def choose_end_turn_if_present(env: Any) -> dict[str, Any] | None:
    d = env.decision()
    if d is None or d.get("kind") != "post_play":
        return None
    return select_option(env, lambda o: o.get("kind") == "end_turn", "post-play end_turn")


def drain_pending_or_end(env: Any, target_id: int | None, *, auto_end_turn: bool, manual_pending: bool = False) -> list[dict[str, Any]]:
    infos: list[dict[str, Any]] = []
    d = env.decision()
    if d is not None and d.get("kind") in {"effect", "effect_multi"}:
        if target_id is None:
            if manual_pending:
                return infos
            # Match the C++ trace runner's choose_target default: first legal target.
            infos.append(select_option(env, lambda _o: True, "first pending target"))
        else:
            infos.append(choose_pending_target(env, target_id))
    if auto_end_turn:
        info = choose_end_turn_if_present(env)
        if info is not None:
            infos.append(info)
    return infos


def replay_semantic_action(env: Any, raw_line: str, *, auto_end_turn: bool = True, manual_pending: bool = False) -> dict[str, Any]:
    parsed = parse_kv_line(raw_line)
    if parsed is None:
        return {"skipped": True}
    op, kv = parsed
    actor = parse_int(kv, "p", "player", default=env.actor_id if env.actor_id is not None else 0)
    infos: list[dict[str, Any]] = []

    if op in {"option", "step"}:
        index = int(kv.get("index", kv.get("_positional", "0")))
        return {"mode": "option", "selections": [env.step(index).get("info")]}

    if op == "pass":
        infos.append(choose_turn_button(env, "pass"))
        return {"mode": "semantic", "op": op, "selections": infos}

    if op == "end_turn":
        info = choose_end_turn_if_present(env)
        if info is None:
            # Some Python decisions expose end_turn directly as a turn option in later cores.
            infos.append(choose_turn_button(env, "end_turn"))
        else:
            infos.append(info)
        return {"mode": "semantic", "op": op, "selections": infos}

    if op == "mulligan":
        entity = entity_from_zone_selector_or_index(env.kernel.state, actor, "hand", kv, "mulligan", "h", "hand", 0)
        # Python mulligan is usually a pending effect/mulligan decision in setup-focused flows.
        infos.append(choose_card_entity(env, entity, expected_kind="card"))
        return {"mode": "semantic", "op": op, "source_entity_id": entity, "selections": infos}

    if op == "play_hand" or op == "play_special":
        entity = entity_from_zone_selector_or_index(env.kernel.state, actor, "hand", kv, op, "h", "hand", 0)
        before_card = env.kernel.state.cards[entity]
        before_type = status_value(before_card.definition.card_type)
        infos.append(choose_turn_button(env, "play"))
        infos.append(choose_card_entity(env, entity, expected_kind="card"))
        if before_type != "SPECIAL":
            row = kv.get("row", "melee")
            side = parse_side(kv.get("side", "self"), actor)
            row_index = int(kv["index"]) if "index" in kv else None
            infos.append(choose_location(env, side, row, row_index))
        target_id = parse_ref(env.kernel.state, kv["target"], actor) if "target" in kv else None
        infos.extend(drain_pending_or_end(env, target_id, auto_end_turn=auto_end_turn, manual_pending=manual_pending))
        return {
            "mode": "semantic",
            "op": op,
            "source_entity_id": entity,
            "card_type": before_type,
            "selections": infos,
        }

    if op == "use_order":
        source_id = parse_ref(env.kernel.state, kv.get("source", kv.get("order", "self:melee:0")), actor)
        infos.append(choose_card_entity(env, source_id, expected_kind="order"))
        target_id = parse_ref(env.kernel.state, kv["target"], actor) if "target" in kv else None
        infos.extend(drain_pending_or_end(env, target_id, auto_end_turn=auto_end_turn, manual_pending=manual_pending))
        return {"mode": "semantic", "op": op, "source_entity_id": source_id, "selections": infos}

    if op == "use_leader":
        leader_id = env.kernel.state.players[actor].leader
        if leader_id is None:
            raise ValueError(f"player {actor} has no leader")
        infos.append(choose_card_entity(env, int(leader_id), expected_kind="leader"))
        target_id = parse_ref(env.kernel.state, kv["target"], actor) if "target" in kv else None
        infos.extend(drain_pending_or_end(env, target_id, auto_end_turn=auto_end_turn, manual_pending=manual_pending))
        return {"mode": "semantic", "op": op, "source_entity_id": int(leader_id), "selections": infos}

    if op == "choose_target":
        target_id = parse_ref(env.kernel.state, kv["target"], actor) if "target" in kv else None
        if target_id is None and has_card_selector(kv):
            d = current_decision(env)
            candidates = [option_entity_id(o) for o in d.get("options", [])]
            target_id = select_entity_from_candidates(env.kernel.state, [int(c) for c in candidates if c is not None], kv, "choose_target")
        # An explicit choose_target line should resolve the pending choice even when
        # --manual-pending is active for earlier play/order lines.
        infos.extend(drain_pending_or_end(env, target_id, auto_end_turn=auto_end_turn, manual_pending=False))
        return {"mode": "semantic", "op": op, "target_entity_id": target_id, "selections": infos}

    if op == "choose_row":
        side = parse_side(kv.get("side", "enemy"), actor)
        row = kv.get("row", "melee")
        infos.append(choose_pending_row(env, side, row))
        if auto_end_turn:
            info = choose_end_turn_if_present(env)
            if info is not None:
                infos.append(info)
        return {"mode": "semantic", "op": op, "selections": infos}

    raise ValueError(f"unknown semantic trace op: {op}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--python-core", type=Path, help="Path containing the extracted gwent Python package")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--starting-player", type=int, default=0)
    parser.add_argument("--script", type=Path, help="Semantic .trace or legacy option-index replay script")
    parser.add_argument("--scenario", default="deck_a_semantic_replay")
    parser.add_argument("--deck", choices=["deck-a"], default="deck-a", help="Python exporter currently supports the extracted Deck A core")
    parser.add_argument("--effects", choices=["deck-a"], default="deck-a", help="Reserved for CLI parity with C++ trace runner")
    parser.add_argument("--random-mulligan", action="store_true")
    parser.add_argument("--no-auto-end-turn", action="store_true", help="Do not auto-select post_play end_turn after a semantic external action")
    parser.add_argument("--manual-pending", action="store_true", help="Leave pending effect choices unresolved until an explicit choose_target line")
    parser.add_argument("--include-legal-surface", action="store_true", help="Include normalized legal-action surface at every trace step")
    args = parser.parse_args()

    add_python_core(args.python_core)

    from gwent.env import GwentEnv
    from gwent.game.demo_deck import load_deck_a
    from gwent.game.setup import build_standard_match

    prepared = build_standard_match(
        load_deck_a(),
        load_deck_a(),
        seed=args.seed,
        starting_player_id=args.starting_player,
        mode="release",
        require_implemented=False,
        random_mulligan=args.random_mulligan,
    )
    env = GwentEnv(prepared.kernel)
    doc: dict[str, Any] = {
        "schema_version": "gwent-golden-trace-v1",
        "engine": "python",
        "scenario": args.scenario,
        "seed": args.seed,
        "starting_player_id": args.starting_player,
        "steps": [],
    }
    append_step(doc, "initial", env.kernel, include_legal_surface=args.include_legal_surface)

    if args.script:
        for raw in args.script.read_text(encoding="utf-8").splitlines():
            stripped = strip_comment(raw)
            if not stripped:
                continue
            result = replay_semantic_action(env, stripped, auto_end_turn=not args.no_auto_end_turn, manual_pending=args.manual_pending)
            append_step(doc, stripped, env.kernel, action={"script": stripped}, result=result, include_legal_surface=args.include_legal_surface)
            if env.terminated:
                break

    print(json.dumps(doc, ensure_ascii=False, indent=2, sort_keys=False))
    return 0


if __name__ == "__main__":
    # Some embedded Python-core runs can leave non-daemon cleanup state alive
    # after the trace JSON has already been written. Flush and exit directly so
    # CTest/audit subprocesses observe completion immediately.
    import os

    code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(code)
