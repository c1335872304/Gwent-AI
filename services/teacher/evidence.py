from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Mapping, Sequence

from .knowledge import CardKnowledgeBase


ROW_NAMES = {0: "近战排", 1: "远程排"}
SIDE_NAMES = {0: "P0", 1: "P1"}


@dataclass(frozen=True)
class TeacherEvidence:
    decision_serial: int | None
    decision_kind: str
    actor_id: int | None
    state_value: float | None
    chosen: dict[str, Any]
    alternatives: tuple[dict[str, Any], ...]
    action_label: str
    card_fact: str | None
    glossary_facts: tuple[str, ...]
    public_facts: tuple[str, ...]


def _num(value: Any) -> float | None:
    try:
        if value is None:
            return None
        return float(value)
    except (TypeError, ValueError):
        return None


def _candidate_list(packet: Mapping[str, Any]) -> list[dict[str, Any]]:
    raw: Sequence[Mapping[str, Any]] = packet.get("candidates") or packet.get("legal_options") or ()
    return [dict(item) for item in raw]


def _chosen_index(packet: Mapping[str, Any]) -> int | None:
    value = packet.get("recommended_option_index", packet.get("chosen_option_index"))
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _card_name(candidate: Mapping[str, Any], kb: CardKnowledgeBase) -> str | None:
    if candidate.get("card_name"):
        return str(candidate["card_name"])
    try:
        card_id = int(candidate.get("card_id", 0))
    except (TypeError, ValueError):
        return None
    card = kb.card(card_id)
    return str(card.get("name")) if card else None


def _card_name_from_field(candidate: Mapping[str, Any], field: str, kb: CardKnowledgeBase) -> str | None:
    try:
        card_id = int(candidate.get(field, -1))
    except (TypeError, ValueError):
        return None
    card = kb.card(card_id)
    return str(card.get("name")) if card else None


def action_label(candidate: Mapping[str, Any], kb: CardKnowledgeBase) -> str:
    kind = str(candidate.get("option_kind") or "action")
    name = _card_name(candidate, kb)
    row = candidate.get("target_row_id", candidate.get("target_row", -1))
    pos = candidate.get("insert_position", -1)

    if kind == "pass":
        return "PASS（结束当前行动）"
    if kind in {"play_card", "play"} and name:
        return f"打出《{name}》"
    if kind == "use_leader" and name:
        return f"使用领袖《{name}》"
    if kind == "end_turn":
        return "结束回合"
    if kind == "choose_card":
        source_name = _card_name_from_field(candidate, "source_card_id", kb)
        target_name = _card_name_from_field(candidate, "target_card_id", kb) or name
        try:
            actor_id = int(candidate.get("actor_id", -1))
            target_side = int(candidate.get("target_side_id", candidate.get("target_side", -1)))
        except (TypeError, ValueError):
            actor_id, target_side = -1, -1
        side = "敌方" if actor_id in (0, 1) and target_side in (0, 1) and actor_id != target_side else ""
        if source_name and target_name:
            source_zone = candidate.get("source_zone", -1)
            try:
                source_prefix = "使用领袖" if int(source_zone) == 7 else "使用"
            except (TypeError, ValueError):
                source_prefix = "使用"
            return f"{source_prefix}《{source_name}》选择{side}《{target_name}》"
        if target_name:
            return f"选择{side}《{target_name}》"
        return "选择卡牌目标"
    if kind == "choose_row":
        try:
            return f"选择{ROW_NAMES.get(int(row), f'第 {int(row)} 排')}"
        except (TypeError, ValueError):
            return "选择目标排"
    if kind == "choose_insert_position":
        try:
            row_text = ROW_NAMES.get(int(row), f"第 {int(row)} 排")
            return f"在{row_text}选择第 {int(pos) + 1} 个插入位置"
        except (TypeError, ValueError):
            return "选择排内插入位置"
    if name:
        return f"{kind}：《{name}》"
    return kind


def _public_facts(public_state: Mapping[str, Any], actor_id: int | None) -> tuple[str, ...]:
    summary = public_state.get("summary") if isinstance(public_state, Mapping) else None
    if not isinstance(summary, Mapping):
        return ()
    facts: list[str] = []
    round_no = summary.get("round")
    if round_no is not None:
        facts.append(f"当前第 {round_no} 小局。")
    p0 = summary.get("p0")
    p1 = summary.get("p1")
    if isinstance(p0, Mapping) and isinstance(p1, Mapping):
        s0, s1 = p0.get("score"), p1.get("score")
        if isinstance(s0, (int, float)) and isinstance(s1, (int, float)):
            facts.append(f"公开比分为 P0 {int(s0)} : {int(s1)} P1。")
            if actor_id in (0, 1):
                own, opp = (s0, s1) if actor_id == 0 else (s1, s0)
                delta = int(own - opp)
                if delta > 0:
                    facts.append(f"当前行动方领先 {delta} 点。")
                elif delta < 0:
                    facts.append(f"当前行动方落后 {-delta} 点。")
                else:
                    facts.append("当前双方同分。")
    return tuple(facts)


def build_evidence(
    packet: Mapping[str, Any],
    public_state: Mapping[str, Any],
    kb: CardKnowledgeBase,
    *,
    top_k: int = 3,
) -> TeacherEvidence:
    candidates = _candidate_list(packet)
    if not candidates:
        raise ValueError("decision packet contains no candidates/legal_options")
    chosen_idx = _chosen_index(packet)
    chosen = next((c for c in candidates if int(c.get("option_index", -1)) == chosen_idx), None)
    if chosen is None:
        raise ValueError(f"recommended/chosen option {chosen_idx!r} not found in candidates")

    ranked = sorted(
        candidates,
        key=lambda c: _num(c.get("probability")) if _num(c.get("probability")) is not None else -1.0,
        reverse=True,
    )
    alternatives = tuple(c for c in ranked if c is not chosen)[: max(0, int(top_k) - 1)]

    card = kb.card(chosen.get("card_id"))
    card_fact = None
    glossary: list[str] = []
    if card:
        description = str(card.get("description") or "").strip()
        if description:
            card_fact = f"《{card.get('name')}》卡牌文本：{description}"
            glossary.extend(kb.glossary_hits(description))

    actor_id = packet.get("actor_id")
    try:
        actor_id = int(actor_id) if actor_id is not None else None
    except (TypeError, ValueError):
        actor_id = None

    return TeacherEvidence(
        decision_serial=int(packet["decision_serial"]) if packet.get("decision_serial") is not None else None,
        decision_kind=str(packet.get("decision_kind") or "unknown"),
        actor_id=actor_id,
        state_value=_num(packet.get("state_value")),
        chosen=chosen,
        alternatives=alternatives,
        action_label=action_label(chosen, kb),
        card_fact=card_fact,
        glossary_facts=tuple(dict.fromkeys(glossary)),
        public_facts=_public_facts(public_state, actor_id),
    )
