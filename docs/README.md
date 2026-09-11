# Documentation

本文档目录按“事实来源”和“使用场景”组织。当前源码的结论以基线和
contract 为准；方案/实施记录只用于解释为什么这样设计，不能覆盖当前代码。

## 当前事实来源

1. [`current/PROJECT_BASELINE.md`](current/PROJECT_BASELINE.md) — 当前架构、运行版本、模型资产和已验证链路；
2. [`current/ARCHITECTURE.md`](current/ARCHITECTURE.md) — Development Plane / Runtime Plane 和职责边界；
3. [`../apps/web/docs/CORE_API_CONTRACT.md`](../apps/web/docs/CORE_API_CONTRACT.md) — Core HTTP、`match_id/revision`、step、preview 和错误 contract；
4. [`current/CORE_CONTRACTS.md`](current/CORE_CONTRACTS.md) — C++ Core、legal action、RL 与跨层 contract 原则；
5. [`current/TEACHER_AND_WEB.md`](current/TEACHER_AND_WEB.md) — AI 决策过程、AI 教师、隐私和故障隔离；
6. [`current/TRAINING_AND_MODEL.md`](current/TRAINING_AND_MODEL.md) — 训练、模型槽位、checkpoint 和 promotion 边界。

## 日常操作

- [`current/LOCAL_DOCKER.md`](current/LOCAL_DOCKER.md) — 本地 Docker 启动、停止、配置和浏览器入口；
- [`current/DEVELOPMENT.md`](current/DEVELOPMENT.md) — C++、Python、Web、Teacher 的开发命令；
- [`current/PROJECT_TEST_PLAN.md`](current/PROJECT_TEST_PLAN.md) — 测试分层、当前结果和未覆盖项目；
- [`../apps/web/README.md`](../apps/web/README.md) — Web 产品边界和模块入口。

## 设计与实施记录

- [`current/LOGIC_OPTIMIZATION_PLAN.md`](current/LOGIC_OPTIMIZATION_PLAN.md) — M0–M4 状态边界、revision、缓存和验收记录；
- [`current/AI_DECISION_AND_TEACHER_TURN_PLAN.md`](current/AI_DECISION_AND_TEACHER_TURN_PLAN.md) — AI 决策过程与教师行动链的设计演进；
- [`current/LOCAL_DOCKER_PLAN.md`](current/LOCAL_DOCKER_PLAN.md) — Docker 架构与实施记录；
- [`current/LOCAL_DOCKER_INSTALL_NOTES.md`](current/LOCAL_DOCKER_INSTALL_NOTES.md) — Windows/Docker Desktop 安装坑和复现流程。

上述记录中的“建议”“规划中”只表示历史阶段；若与基线或源码不一致，以基线、
详细 contract 和实际验证为准。更细的工程规则放在 `.agents/skills/*/`，协议级
细节放在对应模块的 contract 文件中。
