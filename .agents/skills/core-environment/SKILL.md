---
name: core-environment
description: 维护 Gwent C/C++ 游戏规则与环境 contract 时使用；覆盖新卡、legal action、顺序决策，以及 RL observation/action schema 和跨 Training 兼容边界。
---

# Core Environment

这个 Skill 只维护三个会反复出现的 Core 工作流：

1. **新增/扩展卡牌**：怎样安全地把一张卡加入 C/C++ 环境。
2. **动作/规则 Contract**：怎样修改 legal action、pending choice 和执行语义而不丢失边界信息。
3. **Schema Contract**：环境给训练层暴露什么，以及什么时候需要升级版本。
4. **Runtime card/catalog 完整性**：运行时生成、变形、派生选择或 helper definition 不能只靠起始牌组装载。

不要把它扩展成 Core 百科全书。

## A. 新卡扩展

1. 运行只读占位脚本：
   `python .agents/skills/core-environment/scripts/card_extension_stub.py --card "<name-or-id>"`
2. 阅读 `references/CARD_EXTENSION.md`。
3. 找行为最接近的现有卡和测试。
4. 优先复用已有 primitive/effect；只扩展最小必要能力。
5. 添加最接近层级的测试；涉及动作/决策/状态序列时再用 golden/trace。

## B. Runtime card / catalog 完整性

只要卡牌效果会出现以下任一情况，就必须阅读
`references/RUNTIME_CARD_CATALOG.md`：

- 运行时 `spawn/create` 一张不一定在起始牌组中的卡；
- `transform` 到另一张定义；
- 通过 card-definition choice / helper definition 表达“择一”；
- 从牌组外创建 token、特殊牌、派生牌；
- effect handler 通过 card id 再查另一张 definition。

硬性规则：

1. **C/C++ Core 仍是唯一规则事实来源**；Python/RL 不补卡牌规则。
2. 真实 `api::Game::create()` 后的 `CardCatalog` 必须覆盖所有 supported runtime dependencies，而不是只覆盖两个 deck spec。
3. 新增 runtime dependency 时，除了卡牌级 unit test，还必须有至少一个走真实 `Game::create()` 的 integration/regression test。
4. 如果 RL trace 中某个应有的 pending choice 完全没有出现，先检查 Core catalog / continuation / C API 路径，禁止直接归因于“模型不会玩”。
5. 测试不能只用手工安装完整 catalog 的 fixture 来证明真实环境可用。

## C. 动作/规则 Contract

涉及 Action、legal action、pending choice、C ABI 或 RL option 时，先完整阅读
`references/ACTION_CONTRACT.md`。玩家动作信息必须从 Core 一直保留到 C/Python collector；
不要在 RL 层合并语义不同的候选项。

## D. Schema Contract

Schema 的 owner 是 **Core Agent**。Training Agent 只消费、校验和处理 checkpoint 兼容性。

先阅读 `references/SCHEMA_CONTRACT.md`，然后运行：

`python .agents/skills/core-environment/scripts/check_schema.py`

核心规则只有三条：

- 当前 RL Observation / Action Grammar / Reward ABI 版本的唯一人工来源是 `config/rl_contract.json`。
- 版本升级使用 `python scripts/bump_rl_contract.py --schema/--grammar/--reward ...`；生成的 C/Python mirror 不手改。
- `TASK_SCHEMA_VERSION`：Training Task YAML 格式版本；属于 Training，不随环境 schema 自动升级。

**不要因为其中一个版本变化，就顺手 bump 另外几个 contract。**

当 observation 的字段、维度或语义发生破坏性变化时，Core 才升级 observation schema；当前文档描述语义但不复制可变版本数字。随后由 Training Agent 单独判断旧 checkpoint 是 resume、warm-start 还是不兼容。

## 边界

- 不在 Python 复制游戏规则。
- 不为了单张卡创建不必要的新抽象。
- card scaffolder 未来只生成 boilerplate，不生成复杂规则语义。
- Core 定义 environment contract；Training 不自行发明当前环境 schema 版本。
- checkpoint migration policy 可以属于 Training，但它不能反向定义 Core schema。


## Information-boundary regression rule

任何 Observation / C API / Collector 改动都必须同时检查“规则事实”和“玩家可知信息”：

1. C++ `GameState` 可以持有完整私有状态，但 RL Observation 不得直接序列化对手真实手牌或牌库顺序。
2. 公共牌表必须作为 definition-only knowledge 表达，不得借用物理 Hand/Deck zone 暴露分配结果。
3. fair mode 是训练/评估默认；oracle/private mode 只能用于 debug/ablation。
4. Observation 信息语义改变必须 bump schema，并提供显式 warm-start migration；旧 checkpoint 不允许 silent resume。
5. 至少保留一个 C API regression：own hand visible、opponent hand hidden、opponent hand count visible、both public decklists visible。
