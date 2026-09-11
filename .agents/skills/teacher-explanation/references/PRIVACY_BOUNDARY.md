# Teacher Privacy Boundary

## Human vs AI

允许进入 Teacher / Frontend：

- AI 在真实对局中已执行的动作，或 Core clone 中已执行的当前人类回合 branch action；
- 已公开的场面、比分、墓地、状态；
- 动作的结构化 target/row/position；
- 不泄露隐藏信息的 policy/value metadata。

禁止进入浏览器解释：

- AI 当前隐藏手牌列表；
- AI 尚未执行的具体隐藏卡牌候选；
- 对手真实隐藏手牌；
- 牌库真实顺序；
- oracle/debug-only observation。

## Alternative actions

只有候选本身属于玩家已经可知的信息时才能展示。否则实时 Teacher 只解释真实或 branch 中已经 executed 的 action。
