---
name: product-integration
description: 维护 Gwent React/FastAPI 产品层时使用；覆盖 Core HTTP/JSON contract、动态 legal action UI、BFF strict models、TypeScript types、Teacher panel 集成、错误降级以及 Core→Product breaking-change 同步。
---

# Product Integration

把 Product 保持成“结构化 contract 的消费者”，而不是第二套游戏引擎。React/FastAPI 负责展示、转发、错误处理和可选 Teacher 编排；合法性始终来自 Core。

## Authoritative sources

先读 `references/CORE_HTTP_CONTRACT.md`，再定位：

- Core HTTP adapter：`tools/server/human_vs_ai.py`
- BFF typed contract：`apps/web/backend/app/models/core_contract.py`
- frontend types：`apps/web/frontend/src/types/game.ts`
- UI interaction：`apps/web/frontend/src/`

如果这些层缺少展示所需结构化字段，先提出 contract gap，不解析自然语言 label/source/target 猜规则。

## Workflow

1. **Confirm upstream contract**：检查 `summary.decision`、`actions`、`api_version` 以及当前结构化 metadata。
2. **Keep actions authoritative**：UI 只渲染 Core 返回的合法 options，并原样提交 `option_index`；每次 step 后废弃旧 index。
3. **Propagate types end-to-end**：Core adapter → BFF strict Pydantic model → contract doc → TypeScript type → component。
4. **Render, do not re-derive**：例如 `choose_insert_position` 只按 Core 返回的 `insert_position` 渲染插槽，不用 `cards.length + 1` 自己生成合法动作。
5. **Degrade optional dependencies**：Teacher timeout/failure 只影响解释 panel，不阻断 gameplay step。
6. **Verify**：运行 `python .agents/skills/product-integration/scripts/verify.py`；改 React 时再追加 `--frontend`。

## Decision rules

- 新字段是 additive 且旧客户端可忽略：通常 non-breaking；同步 types/tests 即可。
- 删除/重命名/改变语义或合法 action 表达：按 breaking change 处理并显式 bump Product `api_version`。
- UI 想要规则相关信息但 contract 没有：向 Core 请求结构化字段；禁止解析显示文本。
- 需要 PPO/checkpoint/runtime 推理：不在 Product 层实现；继续通过 Core/Strategy HTTP adapter 消费结果。

## Invariants

- Product 不 import `gwent_rl`、不加载 C++ shared library、不直接加载 checkpoint。
- Core `actions` 是唯一合法性来源；BFF/React 不重算 legal action。
- 只消费结构化 target/source/row/position metadata，不从文本反推规则。
- stale `option_index` 不跨 step 复用。
- Teacher 不产生或覆盖 gameplay action。

## Verification

后端 + contract：

```bash
python .agents/skills/product-integration/scripts/verify.py
```

React 改动：

```bash
python .agents/skills/product-integration/scripts/verify.py --frontend
```

最终回复必须给出：上游 contract、同步了哪些消费者、是否 breaking、验证结果以及是否需要 Core/Teacher handoff。

## Handoff

- 缺少规则/合法性字段：交给 Core 定义 authoritative HTTP field。
- Teacher evidence/output contract：交给 Teacher；Product 只负责展示和 loading/error UX。
- checkpoint、训练服务器、模型 promotion：交给 Trainer。
- 仅 React 布局/交互且不改变 contract：Product 可独立完成。
