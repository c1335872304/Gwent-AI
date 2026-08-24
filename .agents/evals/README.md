# Agent Evals

这里保存 **Coding Agent 工程能力测试**。它们不测游戏胜率，而是测 Agent 是否能找到正确事实来源、遵守 Skill、控制修改范围并给出验证证据。

当前任务覆盖：

1. `001_repo_orientation`：定位 Core → C ABI → Policy 的多阶段动作链；
2. `002_schema_change_plan`：规划 Observation / Action contract 变更；
3. `003_regression_triage`：训练回归的分层排查；
4. `004_trainer_mulligan_capability`：识别“规则能力存在但训练不可达”的 capability gap；
5. `005_product_insert_position`：动态插入位置的 Product 集成；
6. `006_teacher_grounding`：Teacher grounding 与隐藏信息边界。

评估重点：

- 是否定位正确 owner；
- 是否读取对应 Skill/reference；
- 是否避免跨层复制逻辑；
- 是否主动运行 deterministic verification；
- 是否识别 ABI/schema/HTTP/privacy 风险；
- 是否在需要时正确 handoff。
