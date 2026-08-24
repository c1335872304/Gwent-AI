# Cross-layer Contracts

该目录记录跨层稳定边界。原则是：消费者依赖结构化 contract，而不是复制规则或解析展示文本。

主要 contract：

- RL contract manifest：`config/rl_contract.json`；
- Core → Web：[`../apps/web/docs/CORE_API_CONTRACT.md`](../apps/web/docs/CORE_API_CONTRACT.md)；
- Strategy → Teacher：`python/src/gwent_rl/contracts/decision_packet.py`；
- Skill 内部约束：`.agents/skills/*/references/`。

各 contract 独立演进：Product API version 不等于 Observation Schema，也不等于 Action Grammar。
