from __future__ import annotations

from pathlib import Path
from typing import Any

from .evidence import TeacherEvidence, action_label, build_evidence
from .knowledge import CardKnowledgeBase
from .models import TeacherAlternative, TeacherRequest, TeacherResponse
from .prompt_builder import build_grounded_prompt
from .providers import TeacherTextProvider

TEACHER_RESPONSE_SCHEMA_VERSION = "gwent-teacher-response-v1"


class TeacherAgent:
    """Grounded, read-only explanation layer for Strategy Core decisions."""

    def __init__(
        self,
        *,
        project_root: str | Path | None = None,
        provider: TeacherTextProvider | None = None,
    ) -> None:
        self.kb = CardKnowledgeBase(project_root)
        self.provider = provider

    def explain(self, request: TeacherRequest) -> TeacherResponse:
        if request.level not in {"beginner", "intermediate", "advanced"}:
            raise ValueError(f"unsupported teacher level: {request.level}")
        evidence = build_evidence(
            request.decision_packet,
            request.public_state,
            self.kb,
            top_k=request.top_k,
        )
        prompt = build_grounded_prompt(request, evidence)
        explanation = (
            self.provider.generate(prompt).strip()
            if self.provider is not None
            else self._deterministic_explanation(request, evidence)
        )
        chosen_prob = self._prob(evidence.chosen.get("probability"))
        alternatives = tuple(
            TeacherAlternative(
                option_index=int(item.get("option_index", -1)),
                label=action_label(item, self.kb),
                probability=self._prob(item.get("probability")),
            )
            for item in evidence.alternatives
        )
        grounded = list(evidence.public_facts)
        if evidence.card_fact:
            grounded.append(evidence.card_fact)
        grounded.extend(evidence.glossary_facts)

        caveats = ["这是基于策略输出与公开规则证据的教学解释，不是神经网络内部思维过程。"]
        packet_evidence = request.decision_packet.get("evidence")
        if isinstance(packet_evidence, dict) and packet_evidence.get("hidden_candidates_redacted"):
            caveats.append("实时对局会隐藏 AI 未公开的手牌候选；教师只解释已经执行的动作，不泄露隐藏信息。")
        if chosen_prob is None:
            caveats.append("当前输入没有提供候选概率，因此不评价模型对该动作的概率偏好强弱。")
        if not evidence.card_fact:
            caveats.append("当前候选没有可匹配的本地卡牌文本，解释仅使用动作与局面字段。")

        return TeacherResponse(
            schema_version=TEACHER_RESPONSE_SCHEMA_VERSION,
            decision_serial=evidence.decision_serial,
            level=request.level,
            headline=f"推荐：{evidence.action_label}",
            explanation=explanation,
            action_label=evidence.action_label,
            policy_probability=chosen_prob,
            state_value=evidence.state_value,
            alternatives=alternatives,
            grounded_facts=tuple(grounded),
            caveats=tuple(caveats),
            prompt=prompt if request.level == "advanced" else None,
        )

    @staticmethod
    def _prob(value: Any) -> float | None:
        try:
            return float(value) if value is not None else None
        except (TypeError, ValueError):
            return None

    def _deterministic_explanation(self, request: TeacherRequest, e: TeacherEvidence) -> str:
        prob = self._prob(e.chosen.get("probability"))
        prob_text = f"策略在合法候选中给它约 {prob * 100:.1f}% 的概率。" if prob is not None else ""
        state_text = ""
        if e.state_value is not None:
            if e.state_value >= 0.25:
                state_text = f"当前 state value 为 {e.state_value:.2f}，模型对当前局面评价偏正面。"
            elif e.state_value <= -0.25:
                state_text = f"当前 state value 为 {e.state_value:.2f}，模型认为当前局面偏困难。"
            else:
                state_text = f"当前 state value 为 {e.state_value:.2f}，模型对局面的评价较接近中性。"

        if request.level == "beginner":
            pieces = [f"这一步选择“{e.action_label}”。", prob_text]
            if e.card_fact:
                pieces.append("能确认的依据是这张牌的公开卡牌效果与当前合法动作；教师不会替模型编造额外理由。")
            if e.public_facts:
                pieces.append(e.public_facts[-1])
            return "".join(x for x in pieces if x)

        alt_text = ""
        if e.alternatives:
            alt = e.alternatives[0]
            alt_prob = self._prob(alt.get("probability"))
            alt_label = action_label(alt, self.kb)
            if alt_prob is not None:
                alt_text = f"最接近的备选是“{alt_label}”（约 {alt_prob * 100:.1f}%）。"
            else:
                alt_text = f"一个主要备选是“{alt_label}”。"

        if request.level == "intermediate":
            return "".join(
                x
                for x in [
                    f"模型选择“{e.action_label}”。",
                    prob_text,
                    state_text,
                    alt_text,
                    "这里的解释只引用可验证的策略分数、公开局面和卡牌文本。",
                ]
                if x
            )

        facts = "；".join(e.public_facts)
        return "".join(
            x
            for x in [
                f"模型选择“{e.action_label}”。",
                prob_text,
                state_text,
                alt_text,
                f"公开局面证据：{facts}。" if facts else "",
                f"卡牌证据：{e.card_fact}" if e.card_fact else "",
                "高级模式同时返回 grounded_facts、候选列表和 provider-neutral prompt，便于审计或接入外部 LLM。",
            ]
            if x
        )
