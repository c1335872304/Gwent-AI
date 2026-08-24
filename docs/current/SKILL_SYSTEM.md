# Skill-driven Agent System

## 1. 目标

本项目不是把 Skill 当成一组长 Prompt，而是把它作为 Coding Agent 的**项目级控制面**：用很少但稳定的指令告诉 Agent 该去哪里找事实、哪些边界不能跨、什么操作应交给确定性工具，以及完成后如何给出验证证据。

真正的执行能力仍来自仓库代码、测试、CLI 和 build/training toolchain。Skill 的价值不在行数，而在于减少 Agent 每次都从整个仓库重新猜测 ownership、contract 和验证方式。

```text
user change request
        |
        v
  agent ownership
        |
        v
     SKILL.md  -----> references/   (authoritative context, on demand)
        |
        +-----------> scripts/      (deterministic checks / wrappers)
        |
        v
 repository code / tests / tools
        |
        v
 verification evidence + handoff
```

## 2. 四个 Agent / 四个 Skill

| Agent | Skill | 事实来源 | 主要输出 |
|---|---|---|---|
| Core | `$core-environment` | C++ Core / C ABI / RL contract | 规则、legal action、schema、测试 |
| Trainer | `$training-config` | Training config / task / checkpoint metadata | PPO 任务、迁移、评估、promotion |
| Product | `$product-integration` | Core HTTP contract | BFF、React、交互 contract |
| Teacher | `$teacher-explanation` | executed action + public evidence | grounded explanation、privacy filter |

Agent 按**责任边界和 authoritative source**切分，而不是按语言机械切分。例如 Python golden trace 工具仍属于 Core，BFF 属于 Product，Teacher provider 属于 Teacher。

## 3. Skill package

每个 Skill 的最小形态是：

```text
<skill>/
├── SKILL.md       # metadata + operational instructions
├── references/   # optional: schemas, contracts, domain rules
└── scripts/       # optional in general; this repo provides verify.py for every Skill
```

本仓库额外约定每个 Skill 都提供 `scripts/verify.py` 作为统一的 deterministic verification entry point。它可以直接做检查，也可以组合已有 repo-level tests/tools；不复制已经存在的实现。

`SKILL.md` 负责：

- 说明 authoritative source 和任务处理顺序；
- 固化最重要的 invariants / failure modes；
- 指向需要按需加载的 References；
- 指定完成任务必须运行的 Verification；
- 标明跨 owner 时的 Handoff。

## 4. Progressive disclosure

Skill 不把整个仓库文档塞进上下文。Agent 先通过 metadata/ownership 选择 Skill，再由 Skill 根据任务类型读取最小 Reference：

```text
metadata routing
  -> SKILL.md
      -> only relevant reference
      -> deterministic script/tool
```

例如修改 Observation 时读 `SCHEMA_CONTRACT.md`，新增卡牌时读 `CARD_EXTENSION.md`；Product 不需要因此加载 PPO 文档，Teacher 也不需要加载完整 C++ 规则实现。

## 5. Contract-first execution

Skill 不允许通过“猜测其他层怎么工作”完成任务。主要边界包括：

- Core → RL：Observation / Action Grammar / C ABI；
- Core → Product：结构化 HTTP action/state contract；
- Strategy → Teacher：已执行动作、允许公开的 policy/value metadata 和 public evidence；
- Training → Runtime：checkpoint metadata 与兼容性。

Breaking change 先修改事实来源，再同步消费者。跨层任务由最接近事实源的 Agent 主导，不新增 manager Agent。

## 6. Deterministic verification

每个 Skill 都有一个稳定入口：

```bash
python .agents/skills/core-environment/scripts/verify.py
python .agents/skills/training-config/scripts/verify.py
python .agents/skills/product-integration/scripts/verify.py
python .agents/skills/teacher-explanation/scripts/verify.py
```

项目自身的 Skill package 也可验证：

```bash
python scripts/check_skill_system.py
```

它检查 4 Agent→4 Skill 映射、Skill metadata、资源引用以及统一 verify entrypoint，防止 Skill 目录在项目演进中变成只存在于 README 的装饰。

## 7. Agent Evals

`.agents/evals/tasks/` 保存代表性工程任务，用来观察 Agent 是否：

- 定位正确 owner 和 authoritative source；
- 主动读取对应 Skill/reference；
- 避免跨层复制逻辑；
- 识别 ABI/schema/HTTP/privacy 风险；
- 运行 deterministic verification；
- 在跨边界任务中正确 handoff。

这些 Eval 测“工程行为”，不是游戏胜率。

## 8. 为什么 Skill 可以很小

Skill 不重写项目实现。一个几十行的 Skill 只要能稳定表达“在哪里找事实、按什么顺序改、哪些事情禁止做、如何验证”，就可以约束几千行甚至更多代码的修改。详细知识留在 Reference，可执行正确性留给 Script/测试，代码本身仍是最终实现。
