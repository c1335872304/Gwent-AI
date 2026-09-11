---
name: product-integration
description: 维护 Gwent React/FastAPI 产品层、Core HTTP contract、动态合法动作交互和 Teacher 展示集成时使用。
---

# Product Integration Skill

用于 `apps/web/` 的 React/FastAPI 产品集成任务。

## 边界

- `tools/server/human_vs_ai.py` / Core HTTP adapter 输出权威状态与合法 actions。
- FastAPI BFF 只做产品级转发、错误处理和未来 Teacher 编排。
- React 只呈现状态并提交最新 `option_index`。
- 不在 Product 层复制卡牌规则、legal action、PPO 推理或 checkpoint loader。训练服务器不是 Product runtime 依赖；`human_vs_ai.py` 在本地加载 promotion 后的 `models/v3/policy.pt`，Product 仍只消费 HTTP。

## 交互工作流

1. 先确认 Core 当前 `summary.decision` 与 `actions` contract。
2. 只使用结构化字段：`source_object_index`、`target_object_index`、`target_side`、`target_zone`、`target_row`、`insert_position`；禁止解析 `source` / `target` / `label` 文本反推规则数据。
3. `choose_insert_position` 必须按 Core actions 动态渲染 `0..N` 中实际返回的合法位置；UI 不自行根据牌数生成合法动作。
4. 每次 step 后丢弃旧 action index，完全使用新 state 返回的 actions；提交时
   同时带该 state 的 `match_id` 与 `revision`，收到 `stale_state` 时刷新 state，
   不把它显示成规则引擎故障。
5. 修改 contract 时同步更新：Core HTTP adapter → BFF strict Pydantic models → `apps/web/docs/CORE_API_CONTRACT.md` → frontend TypeScript types → UI，并按 breaking/non-breaking 判断是否 bump Product `api_version`。

## 最小验证

```bash
PYTHONPATH=apps/web/backend pytest -q apps/web/backend/tests
python -m compileall -q apps/web/backend/app
cd apps/web/frontend && npm ci && npm run build
```

若本机无法安装前端依赖，至少完成 TypeScript contract review，并在有 Node 环境时补跑 build。

## Teacher 原则

Teacher 输入应是 Strategy Core 的结构化 decision trace，输出面向新手的解释。Teacher 不产生或覆盖 `option_index`，也不进入训练 reward/observation 链。

当前人类回合的 Teacher 使用独立的 `counterfactual-action-chain-v2` trace：BFF 调用 Core 只读预演，Core clone
在分支内执行一个根行动及其 Core-required choice，Teacher 只解释已经存在的 branch steps。`root_action` 是给人类的指导动作，`required_choice` 必须以结构化 parent serial 关联 root；不能按 label 猜关系或继续执行第二个自由动作。该预演失败只能降级教师面板，不能让
`/game/step` 失败。不要把 branch steps 写入 `last_ai_actions`，也不要在 BFF 或 React 复刻分支逻辑。

修改此链路时，验证一次预演前后 `/api/game/state` 的 summary、objects、actions 和 checkpoint update 均不变。
若 clone 中的 staged choice 污染真实 state，交由 Core 按 `$core-environment` 的 clone 不变量修复。
预演/Teacher 缓存只能以 Core 的 `(match_id, revision, trace schema, level)` 为键；
任何真实 `/new` 或 `/step` 必须使旧结果失效，React 只能渲染与当前 revision 相同的结果。
