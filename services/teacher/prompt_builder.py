from __future__ import annotations

import json

from .evidence import TeacherEvidence
from .models import TeacherRequest


def build_grounded_prompt(request: TeacherRequest, evidence: TeacherEvidence) -> str:
    """Build a provider-neutral prompt without invoking any external model."""

    facts = {
        "decision_serial": evidence.decision_serial,
        "decision_kind": evidence.decision_kind,
        "actor_id": evidence.actor_id,
        "state_value": evidence.state_value,
        "chosen": evidence.chosen,
        "alternatives": list(evidence.alternatives),
        "card_fact": evidence.card_fact,
        "glossary_facts": list(evidence.glossary_facts),
        "public_facts": list(evidence.public_facts),
    }
    return (
        "你是 Gwent 教学解释器。只解释 Strategy Core 已经选择的动作，不重新选择动作。\n"
        "规则：\n"
        "1. 只使用 FACTS 中的事实；没有证据就明确说不确定。\n"
        "2. 不声称知道神经网络的内部思维过程。\n"
        "3. 不创造合法动作、卡牌效果、隐藏手牌或未提供的局面信息。\n"
        "4. 输出面向用户的教学解释，重点说明选择本身、可观察证据和备选动作。\n"
        f"5. 教学层级：{request.level}；语言：{request.language}。\n\n"
        "FACTS:\n"
        + json.dumps(facts, ensure_ascii=False, indent=2)
    )
