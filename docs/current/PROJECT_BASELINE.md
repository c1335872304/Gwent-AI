# 项目现状与架构基线

> 本文是本地 CPU 产品运行时与当前源码边界的基线。它记录已落地的能力、稳定 contract、验证入口和已知维护事项；计划类文档只保留设计与历史决策，不替代本文。

## 1. 当前目标与非目标

项目提供本地 Docker 单机的“人类 vs AI”昆特牌对战：C++ Core 是规则唯一事实来源，产品只加载一份已封盘的 V3 模型进行 CPU 推理。

- 本地运行需要 `models/v3/policy.pt`，不需要服务器的 `runs/`、中间 checkpoint 或 optimizer state；
- 本地 Docker 不承担训练，不启动 512 环境并行；
- 当前产品包含“AI 决策过程”和“AI 教师”，两者是不同数据流；
- 本文不把训练服务器的 Python/CUDA 版本当成本地 CPU 镜像的运行前置条件。

## 2. 运行时架构

```text
Browser (127.0.0.1:8080)
  │
  ▼
web / Nginx
  │ same-origin /api
  ▼
BFF / FastAPI (8010)
  ├── /game/* ────────────────► Core HTTP (8008)
  │                                ├── C++ rules / legal actions
  │                                ├── C ABI observation
  │                                └── V3 policy.pt CPU inference
  │
  └── /teacher/* ─────────────► Teacher (8020, optional)
                                   └── grounded explanation only
```

Compose 只向宿主机发布 Web 的 `127.0.0.1:8080`；Core、BFF 和 Teacher 位于 Compose 内部网络。模型目录以只读卷挂载到 Core。

## 3. 权威边界

| 层 | 权威职责 | 禁止事项 |
|---|---|---|
| C++ Core | 卡牌规则、状态、pending choice、legal actions、动作执行 | 由 Python、BFF 或 React 重写规则 |
| Strategy | 对 Core 当前候选动作打分，输出概率/价值并选择 `option_index` | 自行判断动作是否合法 |
| BFF | 严格校验、转发、错误隔离 | 解析动作 label 推断规则 |
| Teacher | 将已执行真实动作或 Core 分支轨迹翻译为 grounded explanation | 产生动作、改写 `option_index`、泄露隐藏信息 |
| React | 呈现最新状态和 Core 已给出的合法动作 | 自己生成目标、排或插入位置 |
| Trainer | 训练、评估、warm-start / resume policy | 修改游戏规则或让服务器训练目录成为产品依赖 |

## 4. 两个 AI 面板

### AI 决策过程

展示真实对局中 AI 玩家已经执行的最近一次动作及该次控制权内的内部决策。数据来自 Core service 的 `last_ai_actions`，因此属于真实历史。

### AI 教师

回答“若 AI 接管当前人类回合会怎么走”。流程是：

```text
当前真实人类状态
  → gwent_rl_env_clone
  → clone 内正式 policy 执行一个根行动及其强制选择
  → counterfactual-action-chain-v2 trace
  → Teacher 解释该分支中已执行的步骤
  → 丢弃 clone
```

Teacher 不直接选择动作。分支动作由同一 Core 合法动作面与同一正式模型生成；它在根行动的 pending resolution 完成、控制权离开人类、对局结束或达到安全步数时停止，不会继续选择第二个自愿动作。

### 局面版本与过期操作

每份 `GameState` 带 Core 生成的 `match_id` 和 `revision`：新局生成新的
`match_id` 且 revision 为 0；每次成功真实 `/step` 递增一次 revision。浏览器
提交动作和预演请求时同时带回这两个值。Core 在同一锁内检查它们：旧局面返回
`409 stale_state`，同一局面的非法 option 返回 `422 invalid_action`。BFF 以
`(match_id, revision, trace schema, level)` 管理预演/Teacher 缓存与 single-flight；
React 丢弃不匹配当前版本的教师响应。

### 分支隔离不变量

`PendingChoice` 内部使用 `ResolutionFrame` 保存任务尾部、预算和 decision prefix。环境 clone 必须深拷贝该可变 frame 与回滚快照；否则 Teacher 在 clone 中选择卡牌/排/位置会污染真实局面，表现为 `PREFIX_OVERFLOW`。该问题已由 C API 回归覆盖。

## 5. 当前版本与兼容性

| Contract / runtime | 当前值 | 事实来源 |
|---|---:|---|
| RL contract（Observation / Action / Reward） | 读取 `config/rl_contract.json` | manifest、C mirror、Python mirror |
| Product Core API | 1 | Core HTTP、BFF Pydantic、前端 TypeScript |
| Counterfactual trace | `counterfactual-action-chain-v2` | Core → BFF |
| Teacher action response | `gwent-teacher-response-v1` | Teacher service |
| Teacher turn response | `gwent-teacher-turn-response-v1` | Teacher service |
| Local Python base | `python:3.10-slim-bookworm` | Dockerfiles |
| Local inference | Torch `2.6.0+cpu` | `Dockerfile.core` / Compose |

2026-09-11 的运行态审计确认：四个 Compose 服务 healthy；Core 实际运行 Python 3.10.21、Torch 2.6.0+cpu；已加载模型 contract metadata 与 manifest 一致，模型 Update 为 20。模型 loader 会 fail-closed 校验 schema、grammar 和 prefix semantics，因此不兼容 checkpoint 不会静默进入推理。

Python 3.10 的补丁版本与服务器环境可以不同；本地是 CPU 推理，服务器可使用 CUDA。二者只有在 checkpoint contract、模型结构或模型语义不一致时才构成产品兼容性问题。

## 6. 模型资产与训练边界

```text
Training server
  configs/training + training/tasks
      → runs/ / artifacts/
      → promotion
      → models/v3/policy.pt
      → local Docker inference
```

- `models/v3/policy.pt` 是唯一必需的本地模型资产；
- 产品模型同时含 shared、Deck A private、Deck B private 分支；
- 模型替换使用 `scripts/install_model.py`，它会校验 contract、原子替换模型，并生成可选的 `models/v3/installed.json`；
- `installed.json` 缺失不阻止推理，但会失去来源文件名、hash 和 metadata 的本地审计记录；
- 历史 task 可以保留旧 schema/grammar 以支持迁移研究，但不得直接在当前 runtime 上启动。`rulefix_v7_250k` 就是此类历史任务，验证器会显式警告。

## 7. 已验证的关键链路

| 验证 | 当前证据 |
|---|---|
| Core clone 隔离 | `gwent_rl_c_api_tests` 通过；连续 10 次分支预演后，真实 pending choice 仍可完成 |
| Contract 镜像 | schema 检查通过：manifest、C header、Python mirror 与维度一致 |
| Training 定义 | 23 个 config/task 通过校验；仅历史 `rulefix_v7_250k` 发出预期 warning |
| 模型兼容 | Core 成功加载 Update 20，metadata 与当前 schema/grammar/reward 一致 |
| 产品链路 | BFF → Core → Teacher 的只读预演返回成功；结果携带匹配的 match/revision，预演前后真实状态不变 |
| 过期保护 | 重用旧 revision 返回结构化 409 `stale_state`；前端刷新合法动作而非报 Core 崩溃 |
| 服务状态 | Web、BFF、Core、Teacher health check 全部正常 |

这些证据不等于完成正式训练评估，也不替代浏览器手工交互、长期性能或全量 CTest。

## 8. 文档与维护入口

| 需求 | 首先阅读 |
|---|---|
| 当前架构、版本与运行边界 | 本文 |
| Core / C ABI / schema | `CORE_CONTRACTS.md`、`$core-environment` |
| 本地产品 Docker 操作 | `LOCAL_DOCKER.md` |
| 训练 Docker、CPU smoke、服务器 GPU 训练 | `TRAINING_DOCKER.md` |
| AI 决策过程与教师设计历程 | `AI_DECISION_AND_TEACHER_TURN_PLAN.md` |
| 运行时状态、教师推演与故障逻辑优化 | `LOGIC_OPTIMIZATION_PLAN.md` |
| 测试矩阵与未执行项 | `PROJECT_TEST_PLAN.md` |
| 训练、warm-start、promotion | `TRAINING_AND_MODEL.md`、`$training-config` |

计划完成后必须把最终事实回填到本文或对应 contract，而不是让“建议”“规划中”文档继续充当当前说明。

## 9. 已知维护事项

1. 为当前 `policy.pt` 补充 `installed.json`，记录来源和 hash；
2. 将旧 contract training task 明确标为历史/迁移用途，避免误启动；
3. 若需要字节级可复现 Docker 构建，固定 Python、Node、Nginx 基础镜像 digest；当前浮动 tag 只保证主版本线；
4. `scripts/check.py quick` 是源码卫生检查，默认要求仓库没有产品模型二进制。已安装 `models/v3/policy.pt` 的本地产品目录，应改用本文第 7 节的 contract、模型和 Docker 验收组合，或在后续单独调整该检查策略。
