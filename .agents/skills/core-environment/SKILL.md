---
name: core-environment
description: 维护 Gwent C/C++ 游戏规则、卡牌、legal action、pending decision、C ABI 与 RL environment contract 时使用；也用于 Observation / Action Grammar / runtime card catalog 的 breaking change、兼容性检查与 Core→Training/Product handoff。
---

# Core Environment

把 C/C++ Core 当作游戏规则与环境 contract 的唯一事实来源。这个 Skill 不保存 Core 百科，而是给 Coding Agent 一条可重复的修改路径：先定位 authoritative source，再判断 contract 影响，做最小修改，最后用确定性检查证明没有把规则或 ABI 悄悄复制到其他层。

## Authoritative sources

按任务读取最小必要材料：

- 新增/扩展卡牌：`references/CARD_EXTENSION.md`
- runtime spawn / transform / helper definition：`references/RUNTIME_CARD_CATALOG.md`
- legal action / pending choice / C ABI option：`references/ACTION_CONTRACT.md`
- Observation / Action Grammar / Reward ABI：`references/SCHEMA_CONTRACT.md`

版本数字的唯一人工来源是 `config/rl_contract.json`，生成的 C/Python mirror 不手改。

## Workflow

1. **Classify**：判断任务属于 card behavior、runtime catalog、action contract 或 schema contract；不要因为文件语言不同就换 owner。
2. **Locate**：从对应 Reference 找到事实源和已有相似实现；新卡优先复用已有 primitive/effect。
3. **Assess contract impact**：明确是否改变 legal surface、C ABI、Observation、Action Grammar、Reward ABI 或玩家可知信息。
4. **Make the smallest change**：只修改事实源与必要消费者；禁止在 Python/TypeScript 补一份规则。
5. **Verify locally**：运行 `python .agents/skills/core-environment/scripts/verify.py`；需要 C/Python focused regression 时追加 `--tests`。
6. **Handoff**：若 contract 已改变，向 Trainer/Product 提供结构化字段、版本和验证结果，由消费者处理各自兼容逻辑。

## Decision rules

### Card extension

先运行只读定位工具：

```bash
python .agents/skills/core-environment/scripts/card_extension_stub.py --card "<name-or-id>"
```

随后找最接近的现有卡和测试。只有现有 primitive/effect 无法表达行为时才扩展能力。涉及 runtime create/transform/helper definition 时必须同时检查 catalog dependency closure 和真实 `api::Game::create()` 路径。

### Action / decision change

语义不同的候选动作必须从 Core 一直保留到 C/Python collector；不能为了简化 policy input 在 RL 层提前合并。若 Product 需要新展示字段，优先扩展结构化 contract，不让 UI 解析 label/source/target 文本。

### Schema change

先运行：

```bash
python .agents/skills/core-environment/scripts/check_schema.py
```

只 bump 真正 breaking 的 contract。Observation 信息语义改变时同时检查 information boundary，并要求 Trainer 对旧 checkpoint 做显式 resume / warm-start / incompatible 判断。

## Invariants

- C/C++ Core 是游戏规则、legal action 和 environment contract 的事实源。
- Python/RL、FastAPI、React 不复制卡牌规则或合法性。
- `GameState` 可以持有私有状态，但 fair Observation 不暴露对手真实手牌、牌库顺序或其他隐藏分配结果。
- runtime card dependency 不能只靠起始牌组推断；真实 `Game::create()` 必须可解析运行时会出现的 definition。
- 不因为一个版本变化顺手 bump 其他 contract；`TASK_SCHEMA_VERSION` 属于 Training。
- checkpoint migration policy 属于 Trainer，不能反向定义当前 Core schema。

## Verification

最小确定性证据：

```bash
python .agents/skills/core-environment/scripts/verify.py
```

涉及 legal action / C ABI / collector 的修改，再运行：

```bash
python .agents/skills/core-environment/scripts/verify.py --tests
```

最终回复必须说明：修改的 authoritative source、contract 是否 breaking、运行了哪些检查、是否需要跨 Agent 同步。

## Handoff

- Observation / Action Grammar breaking change：Core 定义新 contract；Trainer 决定 checkpoint migration。
- HTTP 需要新结构化 action/state 字段：Core 定义字段；Product 同步 BFF/types/UI。
- Teacher 缺 authoritative game fact：Core/Strategy 暴露结构化 evidence；Teacher 不从文本反推。
- 纯训练预算、reward/GAE、run orchestration：停止修改 Core，交给 Trainer。
