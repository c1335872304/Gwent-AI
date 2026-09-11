# 本地 CPU Docker 单机对战方案与实施记录

> **状态：已实现并完成 Core/BFF/Teacher/Web 本地运行态审计。** 当前启动、模型和实际版本以
> [`LOCAL_DOCKER.md`](LOCAL_DOCKER.md) 与 [`PROJECT_BASELINE.md`](PROJECT_BASELINE.md) 为准。
> 本文保留部署设计和实施历史；后文“建议新增”“尚未执行”等措辞描述的是实施前状态。

> M4 自动验收已补充到 [`LOGIC_OPTIMIZATION_PLAN.md`](LOGIC_OPTIMIZATION_PLAN.md) 和
> [`PROJECT_TEST_PLAN.md`](PROJECT_TEST_PLAN.md)。本文件不再作为 Docker 启动命令的
> 唯一入口；日常操作请直接阅读 `LOCAL_DOCKER.md`。

## 1. 目标与边界

目标是在 Windows Docker Desktop（WSL2 / Linux containers）上提供一个可重复启动的本地单机产品：

    浏览器中的人类玩家（P0） -> 本地 CPU AI（P1） -> C++ Core

本地 Docker 只负责对局推理、Web 交互和可选 Teacher 解释；训练、Collector、PPO、CUDA、训练运行目录和服务器运维都继续留在训练服务器。

不改变以下边界：

- C++ Core 仍是卡牌规则、状态推进和 legal actions 的唯一事实来源；
- Web 只能显示 Core 返回的结构化 state/actions，并原样提交最新的 option_index；
- Teacher 只解释已执行真实动作，或 Core clone 中已执行的当前人类回合 branch trace；失败不能阻断对局；
- Docker 化属于运行时包装，不修改 RL observation、Action Grammar、Reward ABI 或 Product API 的语义。

当前版本的唯一 contract 来源是 `config/rl_contract.json`；不要以旧部署手册中的历史 schema 版本或旧 portfolio 标识作为当前事实。当前产品模型槽是 `models/v3/policy.pt`，安装入口是 `scripts/install_model.py`。

## 2. 推荐拓扑

将本地产品作为一个 Docker Compose 应用运行，而不是把四个独立职责塞进单个进程：

    Browser
      |
      v
    web (React build + Nginx) :8080
      |
      v
    bff (FastAPI) :8010
      |                    |
      v                    v
    core (C++ + CPU policy) teacher (optional) :8020
    :8008

| 服务 | 使用现有代码 | 职责 | 模型 |
|---|---|---|---|
| core | tools/server/human_vs_ai.py | 加载 libgwent_core.so 与 policy，执行 AI 回合，输出权威 Core HTTP contract | 读取 models/v3 |
| bff | apps/web/backend | 校验 Core contract、转发游戏请求、编排可选 Teacher | 不加载 |
| teacher | services/teacher | 根据 live-safe evidence 输出解释 | 不加载 |
| web | apps/web/frontend | React 编译产物与浏览器入口 | 不加载 |

该拆分复用现有服务边界和端口：Core 8008、BFF 8010、Teacher 8020。用户视角仍是一次 docker compose up。

## 3. 已纳入第一版的运行时修复

Docker 不应封装当前已知的运行时问题。先完成下面四项并加入回归测试：

1. V3 policy 路由：`human_vs_ai.py` 现在按当前 actor 从 `player0_deck_id` / `player1_deck_id` 构造 `actor_deck_ids`。V3 shared/private policy 没有该字段不能推理。
2. 信息边界：`human_vs_ai` 使用 `include_private_info=0`；只有 `manual_test` 使用 oracle/debug 观察面。模式切换会重建环境，避免将 manual 的私有信息面带入真实对局。
3. ABI 单一镜像：单环境 `GwentRlConfig`、`GwentRlStepResult`、`GwentRlObservation` 已移入 `python/src/gwent_rl/_ctypes.py`，Adapter 不再维护重复 struct。
4. 容器配置：Core server 支持 `GWENT_SERVER_*` 环境变量，并兼容旧 `SERVER_*` 名称；直接本地运行仍保留 `manual_test` 默认值。

这四项涉及 Core/Product contract 的消费方式，但不应触发 schema、grammar 或 api_version bump。

## 4. 已纳入部署的文件

    deploy/docker/
      Dockerfile.core
      Dockerfile.bff
      Dockerfile.teacher
      Dockerfile.web
      compose.cpu.yml
      requirements.core.cpu.txt
      nginx.conf
      entrypoint-core.sh
    .dockerignore
    docs/current/LOCAL_DOCKER_PLAN.md
    docs/current/LOCAL_DOCKER.md

`apps/web/backend/requirements.txt` 与 `services/teacher/requirements.txt` 继续是各自服务的唯一依赖来源；
Core 的 CPU 推理依赖单独放在部署目录。使用独立 Dockerfile 可以避免把 PyTorch、C++ 构建工具和 Node
依赖复制到每个镜像。若后续希望减少文件数量，可改为一个多 target Dockerfile，但 Compose 中的职责边界保持不变。

## 5. 镜像与模型策略

### Core 镜像

采用 multi-stage build：

1. Builder：Linux、CMake、Ninja、GCC/G++，构建 shared libgwent_core.so。
2. Runtime：Python slim、CPU PyTorch、NumPy、FastAPI、Uvicorn、libgomp/libstdc++，只复制动态库、Core server、Python inference 代码、data/cards 和 data/embeddings。

CPU PyTorch 版本必须以最终 checkpoint 的实际 metadata 与 scripts/install_model.py 的生产 loader 验证为准；不能只照搬历史服务器手册的版本号。

### 模型

模型不进入镜像层，Compose 以只读 volume 挂载：

    ./models/v3 -> /app/models/v3:ro

运行前必需：

    models/v3/policy.pt

`models/v3/installed.json` 是推荐的 provenance 记录，不是推理的硬依赖。它由 `scripts/install_model.py`
生成并记录哈希及 contract 元数据；只有从服务器下载的最优 `policy.pt` 时，本地 Docker 仍可启动并在日志中
明确警告。服务器的中间 checkpoint、`runs/` 和 `artifacts/` 不需要下载。

新模型的更新流程固定为：

    server best.pt
      -> scripts/install_model.py
      -> models/v3/policy.pt + installed.json（推荐）
      -> docker compose restart core

这样更新模型不需要重建镜像，也不会把 checkpoint 历史、optimizer state 或训练输出带入本地产品。

## 6. Compose 运行约定

默认配置：

- core 只在 Compose 内暴露 8008，不映射到局域网；
- teacher 只在 Compose 内暴露 8020；
- bff 只在 Compose 内暴露 8010；
- web 映射宿主机 127.0.0.1:8080；
- 模型 volume 为只读；
- 不挂载 runs、artifacts/checkpoints 或服务器训练目录；
- `deploy/docker/.env` 集中模型目录、Web 端口、运行模式和 CPU PyTorch index；
- core 设置 GWENT_SERVER_MODE=human_vs_ai、GWENT_SERVER_DEVICE=cpu；
- bff 设置 GWENT_APP_CORE_API_URL=http://core:8008 和 GWENT_APP_TEACHER_API_URL=http://teacher:8020。

每个服务应提供 healthcheck。BFF 可以在 Teacher 不可用时继续提供游戏；Core 不可用则 BFF 健康接口必须明确报告上游失败。

## 7. 分阶段实施

### Phase 0：运行时可用性（已实现，并已完成契约审计）

- 修复 V3 actor_deck_ids、fair observation 和 ctypes 重复定义；
- 为 Core adapter 增加环境变量配置；
- 增加 AI 首步推理、人类隐藏信息和 Deck A/B 路由 regression；
- 责任边界：Core 主导，Product 同步 HTTP 契约测试。

### Phase 1：容器构建（已实现，并已完成构建验证）

- 新建 Core/BFF/Teacher/Web Dockerfile；
- 添加根目录 .dockerignore，排除 runs、artifacts、build、node_modules、模型二进制；
- Dockerfile 定义 Core shared library 的编译、动态依赖和 CPU-only PyTorch 安装；
- 责任边界：Core 构建与 inference runtime；Product 负责 BFF/Web 镜像。

### Phase 2：Compose 集成（已实现，并已完成服务与 HTTP 验证）

- 新建 `compose.cpu.yml`；
- 将 BFF 的 loopback 默认地址替换为 Compose 服务名环境变量；
- 配置 web 到 BFF 的同源反向代理，避免浏览器直接访问 Core；
- 使用只读模型挂载和 healthcheck 管理启动顺序；Teacher 通过 Compose profile 保持可选；
- 责任边界：Product 主导，不更改游戏规则。

### Phase 3：产品验收（已完成基础验收；浏览器回归持续按测试计划执行）

- 验证 manual_test 与 human_vs_ai 两种模式；
- 验证动态 choose_insert_position 仍仅使用 Core actions；
- 验证 AI 手牌、未执行候选和 oracle observation 不会返回浏览器；
- 验证 Teacher 超时/关闭时，游戏 step 仍可继续；
- 本地 Docker 操作手册已写入 `docs/current/LOCAL_DOCKER.md`。

## 8. 验收标准

| 类别 | 必须证明的结果 |
|---|---|
| 构建 | Core shared library 可被 Python ctypes 加载；CPU PyTorch 可加载最终 policy |
| 服务 | core、bff、teacher healthcheck 均可用；Teacher 失效不阻断 BFF 游戏流 |
| 对局 | human_vs_ai 能创建对局、AI 完成首个决策、人类可提交最新 option_index |
| 规则 | 卡牌 target、row、insert position 只来自 Core 返回的合法 actions |
| 隐私 | AI 看不到人类隐藏手牌；浏览器看不到 AI 隐藏手牌和未执行候选 |
| 模型 | `policy.pt` 存在且非空；若存在 `installed.json`，其记录的 contract 与当前 runtime 匹配 |
| 前端 | npm ci && npm run build 通过，浏览器可访问本地页面 |
| 回归 | C++/C API、Python collector、BFF、Teacher 的既有测试通过 |

## 9. 明确不做的事

- 不在 Docker 内运行 PPO 或长期训练；
- 不把 CUDA、tmux、服务器 Conda 环境复制到本地；
- 不让 Web 直接加载 .pt 或 shared library；
- 不在 React/Python 重新实现卡牌规则或 legal action；
- 不因为 Docker 化而升级 RL schema、Action Grammar 或 reward ABI；
- 不把模型二进制提交为源码的一部分。

## 10. 后续交付顺序

建议按以下顺序实施并分别验证：

1. 修复 Core adapter 的 V3 路由与信息边界；
2. 添加 Core runtime Dockerfile 和推理 smoke；
3. 添加 BFF/Teacher/Web 镜像及 Compose；（已完成）
4. 添加模型安装、健康检查与本地操作手册；（已完成）
5. 按 [项目测试计划](PROJECT_TEST_PLAN.md) 执行浏览器交互、异常恢复与回归验收；当前自动 M4 已通过，仅剩浏览器手工点击确认。

关联文档：

- [系统架构](ARCHITECTURE.md)
- [Core 与跨层 Contract](CORE_CONTRACTS.md)
- [Teacher 与 Web 产品](TEACHER_AND_WEB.md)
- [Core HTTP API Contract](../../apps/web/docs/CORE_API_CONTRACT.md)
- [V3 模型槽说明](../../models/v3/README.md)
