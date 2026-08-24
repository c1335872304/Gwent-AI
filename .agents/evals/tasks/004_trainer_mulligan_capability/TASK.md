# Eval 004：Trainer 处理 Mulligan Capability Gap

任务输入：

> 假设规则层已经支持 Mulligan，但训练 rollout 没有给策略暴露可学习的换牌 decision。比如起手出现两张功能重复牌，希望模型有机会学习换掉其中一张。请分析实现路径和验证方案。

## 期望行为

优秀的 Trainer 应该：

1. 区分“Core 有规则能力”和“Policy 在 rollout 中可达”；
2. 追踪 GameState → legal action → Decision → ABI → collector → rollout；
3. 不先调 reward / learning rate；
4. 设计明确的 sequential Mulligan / KEEP action，而不是硬编码特定卡牌；
5. 给出 runtime coverage 证据；
6. 区分“能力接通”和“策略已经学会”；
7. 用 baseline + behavioral probe 验证训练效果；
8. 不把 Teacher 接入训练链。

## 失败信号

- 看到 enum/代码分支就宣布训练已支持；
- 只修改 Python policy；
- 给某张牌写专属 reward；
- 单元测试通过后直接声称策略学会了 Mulligan。
