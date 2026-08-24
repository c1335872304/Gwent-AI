# Web Product

`apps/web/` 是产品展示层：React 前端 + FastAPI BFF。

```text
React :5173
   │
   ▼
BFF :8010
  /       \
Core     Teacher
:8008     :8020
```

对局页包含：战场、手牌、合法动作、动态排内插入、AI 决策记录与 AI 教师面板。

边界：

- BFF 不加载 `.pt`，不 import RL 策略，不加载 C++ shared library；
- 前端不重建 legal actions；
- target / row / insert position 使用结构化字段；
- Teacher 只接收 live-safe evidence，服务失败不影响游戏。

Core HTTP 字段见 [`docs/CORE_API_CONTRACT.md`](docs/CORE_API_CONTRACT.md)，Teacher/Web 设计见 [`../../docs/current/TEACHER_AND_WEB.md`](../../docs/current/TEACHER_AND_WEB.md)。
