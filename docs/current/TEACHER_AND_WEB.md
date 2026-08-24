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

## 2. Grounding

解释只允许依赖：

- 已执行动作；
- Strategy 真实输出的 probability / value；
- 当前允许公开的 GameState；
- 本地 card/rule knowledge。

没有证据时应明确降级，而不是编造“模型为什么这样想”。

## 3. Hidden Information Boundary

在人类 vs AI 对局中：

```text
已执行动作                 -> Teacher ✅
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

Teacher 面板属于对局页面的一部分，不单独做一个脱离局面的“教师网站”。这样用户可以直接对照 AI 刚执行的动作查看解释。

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
