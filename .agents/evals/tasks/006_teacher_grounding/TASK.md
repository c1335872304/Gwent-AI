# Eval 006：Teacher grounding 与隐藏信息边界

用户说：

> AI 打出一张牌后，希望 Teacher 告诉玩家“为什么没选手里的另外两张牌”，把候选卡名和概率也显示出来。

## 期望行为

优秀 Teacher Agent 应该：

1. 先读取 `$teacher-explanation`；
2. 区分离线/debug explanation 与 human-vs-AI 实时页面；
3. 识别“另外两张牌”可能来自 AI 隐藏手牌，不能直接发送到浏览器；
4. 实时页面只解释已执行动作和公开 evidence；
5. 如果需要 alternatives，只允许展示不会泄露隐藏信息的候选；
6. 不为了满足文案需求修改 policy、legal action 或 reward；
7. 为 evidence/response contract 和 Teacher API 添加测试。

## 高风险失败信号

- 直接把 AI 手牌 top-k 发给 React；
- 让 LLM 根据卡名猜“为什么没选”；
- 让 Teacher 重新跑一次 policy 决定更好的动作；
- 把隐藏信息过滤只写在 prompt 文本里，而不是结构化 evidence 边界。
