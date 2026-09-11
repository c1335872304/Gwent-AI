# Skill-driven Agent System

## 1. 为什么把 Skill 放在第一层

项目跨越规则引擎、RL、Web 和解释 Agent。若让一个 Coding Agent 自由修改整个仓库，很容易出现：

- Python/前端复制 C++ 规则；
- 训练异常被错误归因成超参数问题；
- HTTP contract 被文本解析替代；
- Teacher 为了“解释得像”而编造模型理由或泄露隐藏信息。

因此项目把工程经验固化为 Skill，并按事实来源划分 Agent ownership。

## 2. 四个 Agent / 四个 Skill

| Agent | Skill | 事实来源 | 主要输出 |
|---|---|---|---|
| Core | `$core-environment` | C++ Core / C ABI / RL contract | 规则、legal action、schema、测试 |
| Trainer | `$training-config` | Training config / task / checkpoint metadata | PPO 任务、迁移、评估、server run |
| Product | `$product-integration` | Core HTTP contract | BFF、React、交互 contract |
| Teacher | `$teacher-explanation` | executed action / executed Core branch trace + public evidence | grounded explanation、privacy filter |

Agent 不按语言机械切分，而按**责任边界**切分。例如 Python golden trace 工具属于 Core；BFF 属于 Product；Teacher Provider 属于 Teacher。

## 3. Skill 结构

每个 Skill 至少包含：

```text
SKILL.md
references/
  └─ authoritative contracts / edge cases
scripts/
  └─ deterministic checks
```

工作流统一遵循：

```text
Trigger
  -> Locate authoritative source
  -> Check invariant
  -> Minimal change
  -> Deterministic verification
  -> Cross-boundary handoff
```

## 4. Contract 优先

Skill 不允许通过“猜测其他层怎么工作”完成任务。

典型边界：

- Core → RL：Observation / Action Grammar / C ABI；
- Core → Product：结构化 HTTP action/state contract；
- Strategy → Teacher：已执行真实动作或 clone 中已执行的 branch trace、概率、value、公开 evidence；
- Training → Runtime：checkpoint metadata 与兼容性。

Breaking change 必须先修改事实来源，再同步消费者。

## 5. Agent Evals

`.agents/evals/tasks/` 保存代表性的工程任务，用于验证 Agent 是否：

- 能定位正确层；
- 能区分 schema / action grammar；
- 不用 reward 或 learning rate 掩盖 Core bug；
- 不在前端复制合法性；
- 不泄露 AI 隐藏手牌；
- 能完成跨层 handoff。

这些 Eval 测的是“工程行为”，不是游戏强度。

## 6. Handoff 原则

跨层任务由最接近事实来源的 Agent 主导：

- 新 action：Core 定义 → Product/RL 消费；
- checkpoint migration：Core 定义新 contract → Trainer 决定迁移策略；
- Teacher 缺 evidence：Teacher 提需求 → Core/Strategy 暴露结构化字段；
- Web 需要新交互：Product 提 contract 需求 → Core 决定合法性表达。

项目不设置额外 manager Agent；Contract 与 verification 就是协作边界。
