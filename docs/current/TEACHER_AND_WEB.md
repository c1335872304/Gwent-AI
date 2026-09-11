# Teacher and Web Product

## 1. Teacher Runtime

`services/teacher/` 是 Strategy 之后的只读解释层：

```text
executed action + evidence
          │
          ▼
     Teacher Runtime
          │
          ▼
      explanation
```

它支持 Beginner / Intermediate / Advanced 三档解释，并把“事实证据”和“自然语言表达”分离。

页面中的两个模块不共享语义：

- **AI 决策过程**：真实 AI 玩家已经执行的最近一次动作及历史；
- **AI 教师**：Core 从当前人类状态 clone 出只读分支后，模拟一个 AI 选择的根行动及其 Core-required target/row/position；Teacher 只解释这条已经由 Strategy/Core 生成的行动链。

## 2. Grounding

解释只允许依赖：

- 已执行动作；
- Strategy 真实输出的 probability / value；
- 当前允许公开的 GameState；
- 本地 card/rule knowledge。

教师“指导行动链”的每一步都是 clone 环境中已应用的 branch action，不是 Teacher 临时提出的新动作。它在一个根行动及其 pending resolution 完成时停止，不进入第二个自由动作、P1 下一回合、下一小局或整场比赛。

没有证据时应明确降级，而不是编造“模型为什么这样想”。

## 3. Hidden Information Boundary

在人类 vs AI 对局中：

```text
真实 AI 已执行动作           -> AI 决策过程 ✅
Core branch 已执行动作       -> Teacher ✅
公开状态                   -> Teacher ✅
真实动作概率 / value        -> Teacher ✅
AI 隐藏手牌                 -> 前端 ❌
未公开候选动作              -> 前端 ❌
```

BFF 应构造 live-safe packet，而不是把完整内部候选集合交给浏览器。

## 4. Web Topology

```text
React :5173
   │
   ▼
FastAPI BFF :8010
  /           \
 ▼             ▼
Core :8008  Teacher :8020
```

Teacher 面板属于对局页面的一部分，不单独做一个脱离局面的“教师网站”。它展示当前局面的只读 AI 指导行动链，并明确标记为“未执行”；真实 AI 行为则保留在 AI 决策过程面板。

每个真实局面由 Core 的 `match_id + revision` 标识。BFF 以该标识缓存
Core branch trace 和不同解释级别的 Teacher 文本；任何真实 `/new` 或
`/step` 会失效旧缓存。React 只渲染 `base_match_id/base_revision` 与当前
局面相同的返回，慢响应不会覆盖新局面的指导。

## 5. Failure Isolation

Teacher 是旁路组件：

```text
Teacher unavailable
  -> explanation unavailable
  -> game / strategy / legal actions continue normally
```

因此解释服务不会成为规则正确性或对局可用性的依赖。

## 6. Provider Boundary

LLM Provider 只负责语言生成。稳定边界是：

- evidence builder；
- privacy filter；
- prompt contract；
- output schema。

替换本地 LLM 或 API Provider 不应影响 Strategy Core。
