---
name: teacher-explanation
description: 维护 Gwent Teacher grounded explanation 时使用；覆盖 evidence contract、隐私过滤、provider 替换、deterministic fallback、/v1/explain、BFF/React TeacherPanel，以及“为什么 AI 这么下”但不能泄露隐藏信息或重新决策的需求。
---

# Teacher Explanation

Teacher 把 Strategy 已经产生的结构化证据翻译成人类可理解的解释，但永远不是第二个决策器。解释质量由 evidence、privacy boundary 和可验证 contract 保证，而不是靠 prompt 要求 LLM “不要胡编”。

## Authoritative sources

按需读取：

- evidence 字段、可追溯性：`references/EVIDENCE_CONTRACT.md`
- human-vs-AI 隐藏信息边界：`references/PRIVACY_BOUNDARY.md`

运行时链路：

```text
Strategy / Core
    -> DecisionPacket / TraceDecision
    -> Teacher evidence builder
    -> privacy filter
    -> provider or deterministic renderer
    -> TeacherResponse
    -> BFF / React panel
```

## Workflow

1. **Identify the explained decision**：只解释已执行 action，不重新求解动作。
2. **Build evidence first**：把 executed action、public state、允许公开的 policy/value metadata、card/rule knowledge 组织成结构化 evidence。
3. **Check privacy before prompting**：human-vs-AI 默认不发送 AI 隐藏手牌、未公开候选动作或能反推出隐藏状态的字段。
4. **Keep provider presentation-only**：Provider 只把 evidence 改写成自然语言；不能补造事实、改变 option_index 或决定 legal action。
5. **Preserve deterministic fallback**：无 LLM、超时或 provider failure 时仍返回基于同一 evidence 的稳定解释。
6. **Integrate as optional UI dependency**：Teacher failure 只让 panel degrade，不阻断 gameplay。
7. **Verify**：运行 `python .agents/skills/teacher-explanation/scripts/verify.py`。

## Decision rules

- evidence 不足：先修结构化 contract；禁止让 Provider 从 label/source/target 文本猜 target、row 或 card relation。
- 用户要求“为什么没选另外几张”：先区分 offline/debug 与实时 human-vs-AI；实时页面不能暴露来自 AI 隐藏手牌的 alternatives。
- 替换 LLM：只替换 provider 实现；evidence builder、privacy filter、response schema 和 fallback 保持稳定。
- 需要新 authoritative game fact：请求 Core/Strategy 暴露结构化字段，不在 Teacher 复制规则。

## Invariants

- Teacher 不产生或覆盖 `option_index`。
- Teacher 不重新计算 legal action。
- Teacher 不进入 PPO observation/reward/update 链。
- Teacher 不声称输出神经网络隐藏思维过程。
- Teacher 不从 label/source/target 文本反向推导结构化事实。
- Teacher 不把隐藏信息暴露给 React。
- 同一 evidence 在无 LLM 时必须有 deterministic explanation。

## Verification

```bash
python .agents/skills/teacher-explanation/scripts/verify.py
```

该入口检查 Skill-specific structure，并运行 Teacher runtime tests 与 BFF Teacher API test。修改 React `TeacherPanel` 时再补：

```bash
cd apps/web/frontend && npm run build
```

最终回复必须说明：解释对象、evidence 来源、privacy decision、provider/fallback 是否受影响、测试结果。

## Handoff

- 新 authoritative game fact：交给 Core 定义结构化字段。
- 新 policy/value metadata：与 Trainer/Core 协作。
- 展示、布局、loading/error UX：交给 Product。
- 不允许 Teacher 为了“解释更容易”修改规则、reward 或策略动作。
