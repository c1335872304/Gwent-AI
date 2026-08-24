from __future__ import annotations

import json
from pathlib import Path
from typing import Any


GLOSSARY: dict[str, str] = {
    "重伤": "重伤会在回合推进时持续造成伤害并减少剩余持续回合。",
    "流血": "流血是持续伤害状态；本项目卡牌文本中也会使用“重伤”描述该机制。",
    "遮蔽": "遮蔽会阻止单位获得新的状态，例如重伤；净化后才能重新施加这些状态。",
    "霜冻": "霜冻是排天气效果，会持续影响对应排上的单位。",
    "赤诚": "赤诚要求牌组满足对应阵营条件，满足时会启用额外效果。",
    "部署": "部署效果在单位从手牌打出并进入战场时触发。",
    "指令": "指令是单位或战术提供的主动能力，需要在合法时机使用。",
    "净化": "净化会移除单位上的状态。",
    "增益": "增益会提高单位当前战力。",
}


class CardKnowledgeBase:
    """Read-only local card/rule lookup used to ground explanations."""

    def __init__(self, root: str | Path | None = None) -> None:
        if root is None:
            root = Path(__file__).resolve().parents[2]
        self.root = Path(root)
        path = self.root / "data" / "cards" / "reference" / "all_cards.json"
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
        self.cards: dict[int, dict[str, Any]] = {
            int(card["id"]): dict(card) for card in payload.get("cards", [])
        }

    def card(self, card_id: int | None) -> dict[str, Any] | None:
        if card_id in (None, 0, -1):
            return None
        return self.cards.get(int(card_id))

    def glossary_hits(self, text: str) -> list[str]:
        hits: list[str] = []
        for keyword, explanation in GLOSSARY.items():
            if keyword in text:
                hits.append(f"{keyword}：{explanation}")
        return hits
