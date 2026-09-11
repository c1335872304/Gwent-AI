# Gwent V3 当前项目架构

> 本文是当前源码、运行时、Docker、模型和 contract 的架构说明。它描述已经落地的边界，不把历史计划或旧实验任务当作当前运行依赖。

## 1. 一句话概览

项目由两条相互隔离的运行链组成：

```text
产品运行链：浏览器 → Web → BFF → Core HTTP → C++ Core + 产品模型
训练运行链：Training Task → Trainer → Collector → C++ Core 动态库 → PPO/checkpoint
```

产品运行链负责“人类与 AI 对战”；训练运行链负责离线采样、训练和评估。训练服务器不是产品 Web runtime 的依赖。

## 2. 仓库分层

```text
src/                         C++ 游戏规则与环境实现
include/                     C++ public headers / C ABI headers
cmake/                       C++ 构建配置

python/src/gwent_rl/         RL observation、collector、policy、PPO、评估、Training CLI
python/tests/                Python/C ABI/RL 回归测试

tools/server/human_vs_ai.py  产品使用的 Strategy/Core HTTP adapter
apps/web/backend/            FastAPI BFF、严格 HTTP models、Core/Teacher clients
apps/web/frontend/           React 页面、状态渲染和动作交互
services/teacher/             独立 Teacher Runtime 和 grounded explanation

configs/training/            算法与实验 config
training/tasks/               正式 Training Task
runs/                        一次训练的日志、checkpoint、eval
artifacts/                   长期 pinned 训练资产
models/v3/                   产品最终推理模型

deploy/docker/               产品 Docker、测试 Docker、训练 Docker
docs/current/                当前架构、contract、运行和测试说明
```

## 3. 开发责任边界

```text
                         修改请求
                             │
       ┌─────────────────────┼─────────────────────┐
       ▼                     ▼                     ▼
   Core Agent            Trainer Agent         Product Agent
   C++规则/contract      PPO/collector/task     React/BFF/Core HTTP
       │                     │                     │
       └────────────── contract / tests ──────────┘
                             │
                             ▼
                       Teacher Agent
                    explanation/evidence/privacy
```

| 层 | 权威职责 | 不应承担的职责 |
|---|---|---|
| C++ Core | 状态、卡牌规则、pending choice、legal actions、动作执行、C ABI | 不依赖 Python/BFF/React 重写规则 |
| Strategy/Core adapter | 用 policy/value 给 Core 合法候选打分，执行 AI 动作，形成 action history/evidence | 不自行制造合法动作 |
| Trainer | Collector、PPO、评估、checkpoint、warm-start/resume 判定 | 不修改规则、不定义 Core schema |
| BFF | Core/Teacher 转发、Pydantic 校验、缓存和错误隔离 | 不解析 label 推断 target/row/position |
| Teacher | 将结构化已执行证据翻译为解释，提供 deterministic fallback | 不选动作、不 step Core、不进入 reward |
| React | 渲染最新 state/actions，提交 `option_index` 和版本字段 | 不自行生成 legal action |

## 4. 产品运行链

### 4.1 Docker 拓扑

```text
浏览器 127.0.0.1:8080
          │
          ▼
web / Nginx
          │ 同源 /api
          ▼
bff / FastAPI :8010
     ├─ /api/game/* ───────► core / FastAPI :8008
     │                         ├─ C++ Core
     │                         ├─ C ABI observation/actions
     │                         └─ policy.pt + CPU inference
     │
     └─ /api/teacher/* ────► teacher / FastAPI :8020（可选）
                              └─ grounded explanation
```

现有产品 Compose 是 `deploy/docker/compose.cpu.yml`：

- `core`、`bff`、`web` 是产品基本链路；
- `teacher` 通过 Compose profile 可选启用；
- `test` 是按需启动的测试容器，不属于常驻产品链；
- 宿主机只发布 Web 端口，Core/BFF/Teacher 通过 Compose 内部网络通信；
- Core 只读挂载 `models/v3/policy.pt`，不挂载训练 checkpoint。

### 4.2 一次真实人机对战

```text
浏览器请求 /api/game/new 或 /api/game/step
        ↓
BFF 校验请求并转发 option_index + match_id + expected_revision
        ↓
Core 在锁内校验当前 state 和合法 action
        ↓
Core 执行人类动作
        ↓ human_vs_ai 模式
Strategy 使用当前 observation 的合法候选选择 AI 动作
        ↓
Core 执行 AI 动作，直到控制权回到人类/对局结束
        ↓
返回最新 GameState
```

`tools/server/human_vs_ai.py` 同时承担产品策略适配和 Core HTTP 入口：它加载最终模型，直接通过 C ABI 读取 observation、使用 policy 选择 `option_index`，再把动作交回 Core 执行。

## 5. GameState 与动作 contract

当前产品 API contract 与 RL contract 独立：

```text
Product Core API v1
  ├─ match_id
  ├─ revision
  ├─ summary / objects / row_effects
  ├─ actions
  └─ last_human_action / last_ai_actions

RL contract
  ├─ Observation schema
  ├─ Action Grammar
  └─ Reward ABI
```

Core 返回的 `actions` 是唯一合法动作来源。每个 action 除展示文本外，还包含结构化字段：

```text
source_object_index
target_object_index
target_side
target_zone
target_row
insert_position
stable_hash
```

前端始终提交 Core 返回的 `option_index`，并携带产生该动作的 `match_id`、`revision`。前端不能通过 `label`、`source` 或 `target` 文本反推规则。

## 6. 局面版本和并发保护

```text
新局：match_id = 新值，revision = 0
成功真实 step：revision + 1
旧 match_id/revision：409 stale_state
当前局面的非法 option：422 invalid_action
```

Core 在同一锁内检查版本并执行真实动作。BFF 在 `/new` 和 `/step` 后失效旧的 Teacher trace/cache；React 收到不匹配当前版本的响应时丢弃它并刷新最新 state。

Teacher 预演缓存至少以以下信息区分：

```text
(match_id, revision, trace schema, explanation level, top_k)
```

这样慢请求不能把旧局面结果写回当前页面。

## 7. 两个 AI 面板必须分开理解

### 7.1 AI 决策过程

这是实际对战历史：

```text
人类真实动作
   ↓
Core 把控制权交给 AI
   ↓
Strategy/Core 真实执行 AI 的连续决策
   ↓
last_ai_actions
   ↓
前端按一次 AI 控制权分组展示
```

它展示的是 AI 玩家已经在真实对局中执行的最近一次动作和该次控制权内的内部决策，不是教师推演，也不是候选动作列表。

### 7.2 AI 教师

这是独立的只读反事实指导：

```text
当前真实人类局面
   ↓
Core 校验 match_id/revision
   ↓
gwent_rl_env_clone
   ↓
同一正式 policy 在 clone 中选择一个 root_action
   ↓
Core 自动完成 0~N 个必要的 pending choices
   ↓
counterfactual-action-chain-v2
   ↓
BFF 调 Teacher /v1/explain-turn
   ↓
React 展示指导动作和必要选择
   ↓
丢弃 clone，真实对局不变
```

这里的边界是“一个根行动 + Core 为完成该行动实际要求的零个或多个选择”，不是固定的“根行动 + 两个选择”，也不是继续模拟下一个自由行动。

例如：

- 普通出牌可能只有 `root_action`；
- 需要选排的部署可能是根行动加一次选排；
- 需要选目标和位置的动作可能是根行动加多次必要选择；
- 不需要目标的部署不能凭空添加选择。

Teacher 不产生 action，也不修改真实对局；Core/Strategy 已经在 clone 中产生并执行的结构化步骤才是 Teacher 的输入。

## 8. Teacher contract 与隐私边界

Teacher 有两条输入路径：

```text
真实 AI 动作：last_ai_actions → decision-packet-v0 → /v1/explain
当前人类指导：counterfactual-action-chain-v2 → /v1/explain-turn
```

Teacher Runtime 的处理顺序是：

```text
结构化 packet/trace
  → evidence builder
  → privacy filter
  → deterministic renderer 或 provider
  → Teacher response
```

必须保持：

- 公开卡牌文本、公开战场、比分和已执行动作可以解释；
- AI 隐藏手牌、未公开候选和隐藏牌库顺序不返回前端；
- 无外部 provider 时 deterministic explanation 仍可用；
- Teacher 不能声称解释神经网络隐藏思维过程；
- Teacher 失败只能让面板降级，不能让 `/api/game/step` 失败。

## 9. Clone 隔离与动作链停止条件

预演使用 C API clone，不使用真实 handle step。clone 必须深拷贝所有可变 pending 状态，特别是：

- `ResolutionFrame`；
- task queue 尾部；
- decision prefix；
- budget/rollback snapshot；
- 当前 pending choice 及其可变引用。

动作链停止条件是：

1. 根行动完成且 Core 回到自由 turn；
2. 根行动导致控制权离开当前人类；
3. 对局在分支中结束；
4. 达到安全步数上限。

不得用扩大 prefix 上限、截断 trace 或 Teacher 绕开 Core 来掩盖 clone 污染。此前出现“打出盖尔后 `PREFIX_OVERFLOW`”的根因属于 clone/pending 状态隔离问题，已由回归测试覆盖；当前保持 prefix contract 不变。

## 10. 训练运行链

### 10.1 训练容器拓扑

```text
训练服务器或本地 Docker
  └─ trainer 容器
      ├─ Training CLI / Orchestrator
      ├─ gwent_rl.train_ppo
      ├─ RlCollector
      ├─ 直接加载 libgwent_core.so
      └─ PPO / evaluation / checkpoint
```

训练时不需要独立的 Core HTTP 容器。Collector 直接加载动态库；`runtime.num_envs: 512` 表示一个 Trainer 进程内部创建 512 个环境，不表示 512 个 Docker 容器。

训练 Docker 的文件位于 `deploy/docker/`：

- `Dockerfile.train`：在 build stage 编译 Core，再构建 Trainer runtime；
- `compose.train.cpu.yml`：本地 CPU；
- `compose.train.gpu.yml`：服务器 CUDA/GPU；
- `requirements.train.txt`：训练运行时依赖；
- `TRAINING_DOCKER.md`：训练容器操作和服务器部署说明。

### 10.2 本地和服务器的用途

```text
本地 CPU：smoke、collector、PPO 小实验、warm-start 验证
服务器 GPU：正式任务、长预算、512 并行、完整评估
```

本地不使用 512 并行做正式验收。当前本机 CPU Trainer 已完成 8 环境、2 collector 线程、16 局 smoke；服务器 GPU runtime 和正式 512 训练仍需在服务器上单独验收。

训练容器不安装 SSH。SSH 只登录服务器主机，然后由主机启动 Docker。未来多机训练若需要 rendezvous 或调度器，再单独设计分布式边界。

### 10.3 训练资产流转

```text
configs/training + training/tasks
          ↓
trainer / collector / PPO
          ↓
runs/（本次运行）
          ↓ evaluation / promotion
artifacts/（长期 pinned 资产）
          ↓ scripts/install_model.py
models/v3/policy.pt（产品最终模型）
```

三者不能混用：

- `runs/` 保存日志、task state、latest/best/checkpoint；
- `artifacts/` 保存需要长期固定引用的 checkpoint 和 registry；
- `models/v3/` 只放产品推理模型，产品 Core 只读加载。

只有同一 contract、同一 run 才能 resume。跨 contract 或新实验使用 warm-start/migration，并且必须保留 source schema、grammar、hash 和 metadata。

## 11. 测试运行链

测试分为三类，不要求所有容器同时联动：

```text
Core/C++/C ABI tests
  └─ 验证规则、legal action、clone、schema 和 ABI

Python/Teacher/BFF tests
  └─ 使用按需 test 容器或对应 Python 环境

Product integration
  └─ core + bff + teacher + web 联动，验证 HTTP/前端构建/真实页面
```

`deploy/docker/compose.cpu.yml` 的 `test` profile 是临时测试容器：

- 它挂载当前源码；
- 固定 Python、CPU Torch、pytest 和测试依赖；
- `run --rm` 后删除测试容器，但保留测试镜像；
- 它不是 Trainer，也不需要和产品四服务一起常驻。

## 12. 当前版本与兼容性原则

版本事实来源必须分开：

| Contract | 事实来源 |
|---|---|
| RL Observation / Action Grammar / Reward ABI | `config/rl_contract.json`、Core C mirror、Python mirror |
| Product Core API | Core HTTP、BFF Pydantic、前端 TypeScript |
| Counterfactual trace | Core `counterfactual-action-chain-v2` |
| Teacher response | Teacher `gwent-teacher-response-v1` / `gwent-teacher-turn-response-v1` |
| Training Task YAML | `training/task.schema.json` 和 `training/task.py` |
| Docker runtime | 对应 Dockerfile、Compose 和锁定依赖 |

不要因为 Observation 变化就顺手修改 Product API；不要因为 Task YAML 变化就修改 RL schema；不要在 Python、BFF 或 React 复制 C++ 规则。

## 13. 当前已验证与未完成项

已验证：

- Core clone/pending choice 隔离回归；
- Core → BFF → Teacher 的只读动作链；
- `match_id/revision` stale-state 保护；
- AI 实际动作与 AI 教师数据流分离；
- 本地产品 Docker 的 Core/BFF/Teacher/Web 链；
- 本地 CPU Trainer 镜像、task 校验、8 环境 16 局 smoke；
- smoke 训练 `illegal=0`，checkpoint 和 task state 正常生成。

仍需在后续服务器阶段完成：

- NVIDIA Docker runtime 与 `torch.cuda.is_available()` 验证；
- 服务器 GPU 小规模 Trainer smoke；
- 服务器原 `gwent-build` GCC 11.2 与当前 Docker builder GCC 12.2 的兼容确认；
- 正式 512 并行训练及完整评估；
- promotion 后产品模型的最终人工验收。

## 14. 文档入口

| 需求 | 首先阅读 |
|---|---|
| 当前整体架构 | 本文 |
| 当前源码/模型/运行基线 | `PROJECT_BASELINE.md` |
| Core、C ABI、schema、legal action | `CORE_CONTRACTS.md`、`$core-environment` |
| 本地人机对战 Docker | `LOCAL_DOCKER.md` |
| 训练 Docker | `TRAINING_DOCKER.md`、`TRAINING_AND_MODEL.md`、`$training-config` |
| Teacher evidence、隐私、动作链 | `TEACHER_AND_WEB.md`、`$teacher-explanation` |
| 前端/BFF/Core HTTP | `apps/web/docs/CORE_API_CONTRACT.md`、`$product-integration` |
| 测试范围与未执行项 | `PROJECT_TEST_PLAN.md` |
| 历史设计和优化过程 | `LOGIC_OPTIMIZATION_PLAN.md`、`AI_DECISION_AND_TEACHER_TURN_PLAN.md` |

