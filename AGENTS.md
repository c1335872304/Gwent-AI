# AGENTS.md

本仓库使用 **四个专用 Coding Agent + 四个可复用 Skill** 维护不同工程边界。Agent 本身只负责路由和职责；真正稳定、可复用的工程知识写在 `.agents/skills/`。

```text
                         change request
                              |
        +---------------------+---------------------+
        |                     |                     |
        v                     v                     v
   Core Agent            Trainer Agent         Product Agent
$core-environment      $training-config     $product-integration
        |                     |                     |
        +----------- contracts / tests ------------+
                              |
                              v
                        Teacher Agent
                   $teacher-explanation
```

不要新增 manager Agent。跨边界任务由最接近事实来源的 Agent 主导，通过 contract 与验证结果 handoff。

## 路由

| 任务 | Coding Agent | Skill |
|---|---|---|
| 卡牌、规则、legal action、环境行为 | `core` | `$core-environment` |
| Observation / Action Grammar / C ABI contract | `core` | `$core-environment` |
| PPO / collector / reward / Training Task / server training | `trainer` | `$training-config` |
| React / FastAPI BFF / Core HTTP contract / UX | `product` | `$product-integration` |
| Teacher evidence / explanation / privacy / Teacher API | `teacher` | `$teacher-explanation` |
| 旧 checkpoint → 新 schema | Core 定义 contract；Trainer 决定 migration | Core + Trainer |
| Core HTTP breaking change | Core 提供 authoritative fields；Product 同步 contract | Core + Product |
| Teacher 所需新 evidence | Strategy/Core 暴露结构化字段；Teacher 不反向解析文本 | Core/Product + Teacher |

不要按语言机械切分：Core 的 Python golden/trace 工具仍属于 Core；`tools/server/` 中训练/评测通常属于 Trainer；`human_vs_ai.py` 是 Product 所依赖的 Strategy/Core HTTP adapter，contract 变更需 Core/Product 协作。

## Skill contract

一个 Skill 至少应该包含：

1. **Trigger**：什么任务必须读取它；
2. **Workflow**：处理顺序；
3. **Invariant**：绝不能破坏的工程边界；
4. **References / scripts**：事实来源和自动检查；
5. **Verification**：完成任务前必须给出的证据；
6. **Handoff**：什么时候停止修改并交给另一个 Agent。

Skill 不写项目百科，也不为一次性任务临时新增。重复出现的工程判断才进入 Skill。

## Core Agent

- C/C++ 规则和 environment contract 是事实来源；不要在 Python/TypeScript 复制游戏规则。
- 新卡先找相似行为，优先复用 primitive/effect，再做最小扩展和测试。
- runtime spawn/transform/helper definition 必须检查完整 card catalog dependency closure。
- Schema 先判断属于 Observation、Action Grammar 还是其他 contract，只 bump breaking 的那一个。
- 新卡、动作和 schema 流程统一读取 `.agents/skills/core-environment/SKILL.md`。

## Trainer Agent

- `configs/training/*.yaml` = 算法/实验参数；`training/tasks/*.yaml` = 正式运行任务。
- warm-start source schema/grammar 必须显式保存；resume / warm-start / incompatible 由 Trainer 判断。
- `runs/` 是训练输出，`artifacts/` 是长期 pinned 资产；服务器不是产品 runtime 依赖。
- 训练异常先区分 environment bug、collector bug、reward/GAE bug 和优化问题，不先盲调 learning rate。
- 训练工作流统一读取 `.agents/skills/training-config/SKILL.md`。

## Product Agent

- 维护 `apps/web/` 的 React + FastAPI BFF 与 Core HTTP/JSON contract。
- 不 import `gwent_rl`、不加载 C++ shared library、不直接加载 checkpoint。
- Core `actions` 是唯一合法性来源；前端只渲染合法 action 并原样提交 `option_index`。
- 结构化 metadata 不足时请求 contract 变更，禁止解析 label/source/target 文本反推规则。
- Product 工作流统一读取 `.agents/skills/product-integration/SKILL.md`。

## Teacher Coding Agent

- 维护 `services/teacher/`、Teacher BFF 接口和前端解释数据 contract。
- Teacher 只能解释 Strategy 已执行的动作；不重算 legal action、不改 `option_index`、不参与 reward shaping。
- 解释必须来自结构化 evidence；不把“语言流畅”当作“解释有证据”。
- human-vs-AI 实时解释不得泄露 AI 隐藏手牌或未公开候选动作。
- Provider 可替换，但 evidence builder、privacy filter 和 output schema 是稳定边界。
- Teacher 工作流统一读取 `.agents/skills/teacher-explanation/SKILL.md`。

## Runtime Teacher 与 Coding Agent 的区别

`teacher` Coding Agent 是**开发角色**；`services/teacher/` 是**运行时组件**：

```text
Coding Agent + Skill  --maintains-->  services/teacher/
Strategy Core         --evidence-->   Teacher Runtime --text--> React panel
```

两者不要混成一个自治决策 Agent。

## Agent Evals

`.agents/evals/` 不测试游戏胜率，而测试 Coding Agent 是否：

- 找到正确事实来源；
- 遵守 Skill/invariant；
- 没有误改其他层；
- 识别 schema / ABI / HTTP / privacy 风险；
- 主动运行对应验证；
- 能在跨边界任务中正确 handoff。

## 验证

```bash
python scripts/check.py quick
python scripts/check.py test
python scripts/check.py full
python scripts/check.py train

PYTHONPATH=. pytest -q services/teacher/tests
PYTHONPATH=apps/web/backend pytest -q apps/web/backend/tests
cd apps/web/frontend && npm ci && npm run build
```

最终说明应包含：改了什么、为什么、读取了哪个 Skill/contract、验证结果、是否跨越 Agent 边界。
