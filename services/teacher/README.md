# Teacher Runtime

`services/teacher/` 是只读解释服务。它把 Strategy 已执行动作和公开 evidence 转换成教学文本，不参与动作选择、legal action 或 PPO。

能力：

- DecisionPacket / TraceDecision 输入；
- Beginner / Intermediate / Advanced 三档解释；
- probability / value 与本地 card/rule evidence；
- deterministic fallback；
- 可替换语言 Provider；
- HTTP API；
- 人机对局隐藏信息保护。

详细边界见 [`../../docs/current/TEACHER_AND_WEB.md`](../../docs/current/TEACHER_AND_WEB.md)。
