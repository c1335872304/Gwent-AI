# Gwent AI

Gwent AI 是一个 **Skill-driven vibe coding** 游戏 AI 工程。项目的重点不是单独展示某个模型，而是展示如何用 **专用 Coding Agent + Skill + Contract + Eval** 驱动一个跨 C++ 规则引擎、强化学习、Teacher Agent 和 Web 产品的复杂系统持续演进。

## 1. 项目核心：Skill-driven 开发系统

```text
需求
  │
  ▼
Agent 路由
  │
  ├─ Core Agent     ── $core-environment
  ├─ Trainer Agent  ── $training-config
  ├─ Product Agent  ── $product-integration
  └─ Teacher Agent  ── $teacher-explanation
  │
  ▼
Contract / Invariant / Verification
  │
  ▼
Validated Change
```

四个 Skill 都保存在 `.agents/skills/`，不是一次性 Prompt，而是可复用的工程工作流。每个 Skill 明确：

- **Trigger**：什么时候应该由这个 Skill 接管；
- **Workflow**：先查什么、后改什么；
- **Invariant**：哪些系统边界不能破坏；
- **Reference / Script**：事实来源与自动检查；
- **Verification**：修改完成后必须提供什么证据；
- **Handoff**：什么时候应该停止扩散修改并交给另一个 Agent。

`.agents/evals/` 则验证 Coding Agent 是否遵守这些工程规则。单元测试回答“代码是否正确”，Agent Eval 回答“Agent 是否按正确边界解决问题”。

详见 [Skill 与 Agent 架构](docs/current/SKILL_SYSTEM.md)。

## 2. 两层架构

项目明确区分开发平面和运行时平面。

### Development Plane

```text
Coding Agents
     │
     ▼
Skills + Contracts + Evals
     │
     ▼
源码 / 配置 / 测试 / 产品集成
```

### Runtime Plane

```text
C++ Game Core
     │  legal actions / public state
     ▼
V3 Strategy Core
     │  executed action + evidence
     ├──────────────────────┐
     ▼                      ▼
FastAPI / React        Teacher Runtime
     │                      │
     └────────── UI ◄───────┘
```

核心边界：

- **C++ Game Core**：规则、卡牌效果、状态推进和合法动作的唯一事实来源；
- **Strategy Core**：只在 Core 给出的合法动作中选择；
- **Teacher Runtime**：只解释已经执行的动作，不改变动作、不参与 PPO、不泄露隐藏信息；
- **Web Product**：消费结构化 HTTP contract，不复制规则、不直接加载训练逻辑。

详见 [系统架构](docs/current/ARCHITECTURE.md)。

## 3. C++ Game Core

Core 负责：

- 卡组、手牌、战场、墓地和轮次状态；
- 卡牌效果、状态、天气与触发顺序；
- legal action 与多阶段 decision；
- 动态排内插入位置；
- C ABI / RL observation；
- golden trace、规则回归与 fuzz/stress 测试。

规则层不把合法性复制到 Python 或 TypeScript。跨层只传递结构化 action / state contract。

当前关键规则包括：

- 卡牌进入墓地时恢复当前基础战力；
- 暗影长者先尝试给予 2 点 Bleeding，再结算额外 Bleeding tick；
- Veil 阻止 Bleeding；若 Bleeding 被 Veil 阻止，该目标不会凭空受到后续 Bleeding 伤害。

详见 [Core 与跨层 Contract](docs/current/CORE_CONTRACTS.md)。

## 4. V3 双卡组策略模型

V3 使用一个 checkpoint 同时服务 Deck A 与 Deck B：

```text
                 Shared Representation
                     ~49.6%
                  /           \
                 /             \
        Deck A Private     Deck B Private
            ~50.4%             ~50.4%
              │                  │
         A decisions         B decisions
```

这种结构同时保留通用局面表示和卡组私有策略。运行时根据 deck id 选择对应 private branch，因此产品只需要加载一个 V3 checkpoint。

当前封盘模型来自修正 Veil / Unseen Elder 交互后的 50,000 局 Joint 训练：

| 指标 | 结果 |
|---|---:|
| Best update | u20 |
| Joint score | 55.2% |
| A score vs previous best | 50.9% |
| B score vs previous best | 59.5% |
| Illegal results | 0 |

模型槽位：`models/v3/policy.pt`。

详见 [训练与 V3 模型](docs/current/TRAINING_AND_MODEL.md)。

## 5. Teacher Agent

Teacher 有两层含义：

- **Teacher Coding Agent**：通过 `$teacher-explanation` Skill 维护解释 contract、隐私边界和产品集成；
- **Teacher Runtime**：`services/teacher/`，把 Strategy 的结构化 evidence 转换成 Beginner / Intermediate / Advanced 三档解释。

Teacher Runtime 的原则：

```text
executed action + public evidence -> explanation
hidden hand / hidden alternatives -X-> frontend
Teacher                            -X-> option_index / legal action / reward
```

无外部 LLM 时可使用 deterministic grounded fallback；替换语言 Provider 不改变 evidence 和安全边界。

详见 [Teacher 与 Web](docs/current/TEACHER_AND_WEB.md)。

## 6. Web Product

```text
React Frontend :5173
       │
       ▼
FastAPI BFF :8010
    /          \
   ▼            ▼
Core :8008   Teacher :8020
```

对局页包含战场、手牌、合法动作、动态插入位置、AI 决策记录和 AI 教师面板。Teacher 是可选旁路：解释服务失败不会影响游戏继续。

## 7. 仓库结构

```text
.agents/            Skills、references、scripts、Agent Evals
.codex/agents/      Coding Agent 定义
src/ include/       C++ 权威规则核心
python/             RL collector、policy、PPO、evaluation
configs/            算法与训练配置
training/           可执行 Training Task
services/teacher/   Teacher Runtime
apps/web/           React + FastAPI BFF
contracts/          跨层 contract 入口
models/v3/          最终 V3 模型槽位
tests/              C++ regression tests
python/tests/       Python / RL tests
tools/              golden trace、server adapter、profiling 等工程工具
docs/current/       当前系统文档（不保留研发历史）
```

## 8. 验证入口

快速检查：

```bash
python scripts/check.py quick
```

完整工程测试：

```bash
python scripts/check.py test
```

更完整的构建、训练、Teacher 和 Web 启动方式见 [开发与运行](docs/current/DEVELOPMENT.md)。
