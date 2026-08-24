# Core and Cross-layer Contracts

## 1. C++ Core 是唯一规则来源

卡牌效果、状态变化、随机目标、合法动作和多阶段 decision 都由 C++ Core 决定。Python、Teacher 和 React 只能消费结构化结果。

## 2. Legal Action

策略与产品都使用 Core 返回的 candidate list：

```text
state
  -> Core enumerates legal actions
  -> consumer selects option_index
  -> Core executes option_index
```

禁止消费者：

- 自行重建 legal action；
- 根据 card text 猜 target；
- 根据 label/source/target 字符串反向解析规则；
- 硬编码动态插入位置数量。

## 3. 多阶段 Action Grammar

复杂动作按 decision 链展开，例如：

```text
play card
  -> choose row
  -> choose insert position
  -> choose target
```

每一阶段都由 Core 暴露合法候选，policy 只对当前候选打分。

## 4. Observation Contract

当前 Observation / Action Grammar / Reward Config 的版本号只以 `config/rl_contract.json` 为准。维护文档描述语义，不重复钉死可变版本号。

Observation 由 Core/C ABI 生成，Python schema mirror 必须通过 Skill 检查脚本与 C 定义保持一致。

## 5. Schema Change

变更前先判断属于：

- Observation Schema；
- Action Grammar；
- Reward Config；
- Product HTTP API。

只 bump 真正发生 breaking change 的 contract。checkpoint compatibility 由 Trainer 根据 source/target metadata 决定，不在 Core 中静默迁移。

## 6. Core → Product

HTTP action/state contract 的协议级字段见 [`../../apps/web/docs/CORE_API_CONTRACT.md`](../../apps/web/docs/CORE_API_CONTRACT.md)。Product API version 与 RL schema 独立演进。

## 7. Strategy → Teacher

Teacher 只接收结构化 evidence：

- 已执行动作；
- policy probability / value（若真实存在）；
- 公开状态；
- card/rule knowledge。

Teacher 不把生成文本当作模型的真实 chain-of-thought。
