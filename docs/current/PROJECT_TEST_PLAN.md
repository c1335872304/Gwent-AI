# Gwent 项目完整测试规划

## 1. 目标与当前结论

本计划用于验收以下完整链路：

```text
C++ Core / C ABI
       ↓
Python RL / Collector / checkpoint inference
       ↓
Core HTTP adapter
       ↓
FastAPI BFF
       ↓
React Web / Teacher panel
       ↓
CPU Docker Compose runtime
```

目标不是重新训练模型，而是证明：

1. Core 规则、合法动作、C ABI 和 RL schema 没有回归；
2. 已下载的最优模型可以在 CPU Docker 中加载并完成推理；
3. BFF 严格转发 Core contract，前端只提交最新的 `option_index` 及同一状态的 `match_id/revision`；
4. Teacher 只解释已执行的真实动作或 Core clone 中已执行的单个根行动及其必要选择，不泄露 AI 隐藏手牌，也不阻断游戏；
5. Docker 中的 Core → BFF → Web → 浏览器访问链路可以稳定工作。

当前状态是“非训练产品链路基本通过，浏览器手动验收待执行”：

- Core 非训练 CTest：**36/36 通过**；另行执行了小规模训练 smoke；
- Core card data/schema 检查：通过；Teacher 静态检查：通过；
- Docker 内 BFF 测试：**17 passed**；Core HTTP adapter 测试：**12 passed**；Teacher 测试：**7 passed**；
- 前端 `npm run build`：通过；
- 当前源码重建的 Core/BFF/Web Compose：healthy；CPU `human_vs_ai` 推理、BFF HTTP、`ability_text`、隐藏手牌边界、版本化预演和 `409 stale_state`：通过；
- Teacher profile health 和 `/api/teacher/explain`：通过；
- 本地训练 smoke：collector 使用 1 个环境、2 个回合通过；PPO/replay smoke 使用 4 个环境、1 次 update 通过，`illegal=0`、`mismatches=0`；
- 浏览器实际点击验收尚未执行；正式训练和训练全量测试按要求暂缓。

M4 自动验收记录（2026-09-11）：

- 连续 Teacher 预演、真实《盖尔》及其排/位置/牌选择、领袖目标选择均通过；
- 旧 revision 返回 `409 stale_state`，新局返回新 `match_id/revision=0`；
- 停止 Teacher 后真实 `/step` 仍成功；
- human-vs-AI 隐藏手牌边界和卡牌 `ability_text` 通过；
- 仅剩浏览器手工点击与视觉确认，不能由 API smoke 代替。

测试环境已固定为按需构建的 Docker `test` profile：依赖写入
`deploy/docker/requirements.test.txt`，镜像由 `deploy/docker/Dockerfile.test` 构建，测试时挂载当前工作树。
生产镜像仍刻意不包含 pytest；测试容器退出后可以删除，但测试依赖镜像会保留并由 Docker layer cache 复用。

本轮范围明确排除正式训练：不执行 `python/tests` 全量、训练任务验证、长时间 PPO 训练和服务器训练。只执行本地 1 环境 collector smoke 及 4 环境/1 次 update 的最小 PPO/replay smoke；其余训练相关项目统一标记为“暂缓”。

本计划遵循四个边界：Core 是规则和合法动作事实来源，Trainer 只负责训练/模型兼容，Product 只负责转发和展示，Teacher 只解释结构化 evidence。

## 2. 测试原则

### 2.1 分层执行，逐级放行

```text
T0 环境与资产检查
    ↓
T1 静态、schema、配置和 contract 检查
    ↓
T2 Core / C ABI / golden 回归
    ↓
T3 Python RL / Collector / 模型推理（本轮暂缓）
    ↓
T4 Teacher 与 BFF 单元测试
    ↓
T5 前端构建与 HTTP 产品链路
    ↓
T6 Docker Compose 集成
    ↓
T7 浏览器功能验收
```

上层失败时不继续把问题归因于模型效果。例如 legal action 缺失时，先查 Core catalog、pending choice、C API 和 adapter，不直接调整 learning rate。

### 2.2 不重新训练作为默认策略

本次单机产品验收只使用：

```text
models/v3/policy.pt
```

不要求下载历史 checkpoint、`runs/` 或 `artifacts/`。训练层 smoke、训练任务和 checkpoint compatibility 本轮暂缓；只有用户明确要求时才恢复训练测试或启动正式训练。

### 2.3 不接管桌面也能完成大部分测试

T0–T6 使用 PowerShell、Docker CLI、HTTP 请求和测试脚本即可完成，不需要控制浏览器窗口。T7 浏览器验收可以由用户手动点击；如果没有浏览器自动化工具，只记录为“手动验收未执行”，不能冒充已通过。

## 3. 测试环境和前置条件

### 3.1 必需资产

- Docker Desktop Linux Engine 正常运行；
- `models/v3/policy.pt` 存在且为从服务器下载的最终模型；
- `deploy/docker/.env` 已按 `.env.example` 创建；
- Docker 可使用 CPU，不依赖 CUDA；
- 宿主端口 `8080` 未被其他服务占用；
- 若执行 Core 本地构建，需要 CMake、Ninja、C/C++ 编译器；
- 若宿主没有 Python，优先使用项目 Docker 镜像中的 Python 3.10，不要因为宿主缺少 `python.exe` 就跳过测试。

### 3.2 Windows/Docker 检查

```powershell
$Docker = "C:\Users\Ruikai Chen\AppData\Local\Programs\DockerDesktop\resources\bin\docker.exe"
& $Docker version
& $Docker info
& $Docker image ls
Test-Path -LiteralPath "models\v3\policy.pt"
```

如果 `docker` 已加入 PATH，可以把 `$Docker` 替换为 `docker`。只读检查失败时先处理 Docker Desktop/权限问题，不修改项目代码。

### 3.3 固定测试容器

测试容器是按需启动的独立服务，不是日常运行的第四/第五个产品服务。它使用与本地产品相同的
Python 3.10 基础镜像和 CPU Torch 版本，但额外安装 pytest、pytest-asyncio、httpx 及测试依赖。
项目代码通过 Compose bind mount 进入 `/workspace`，因此 Python 代码修改后不需要重新构建测试镜像。

首次构建测试镜像（会下载基础镜像和测试依赖，后续复用缓存）：

```powershell
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml --profile test build test
```

运行 Python 适配器、Teacher 和 BFF 单元/contract 测试：

```powershell
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml --profile test run --rm test
```

`test` 容器默认不依赖 `core`、`bff`、`teacher` 或 `web`；需要 HTTP 集成测试时，再显式启动对应的
Compose 服务并为测试增加集成命令。该容器不加载训练任务、不启动 PPO，也不替代学校服务器训练环境。

## 4. 测试矩阵

| 阶段 | 范围 | 入口 | 通过标准 | 证据 |
|---|---|---|---|---|
| T0 | 环境、模型、端口 | Docker/PowerShell 检查 | Engine、模型、端口和配置可用 | 版本输出、模型大小、Compose config |
| T1 | 文档、schema、卡牌数据、Product contract | Core/Product 独立检查 | 非训练检查全部 PASS | quick 子集日志 |
| T2 | C++ Core、C ABI、legal action | CMake、`ctest`、golden | Core/CTest 全部通过 | CTest/golden 报告 |
| T3 | Collector、PPO、训练任务、checkpoint 兼容性 | 本地最小 smoke；正式训练暂缓 | smoke 通过；不执行 512 并行和长跑 | smoke 日志 |
| T4 | Teacher、BFF、隐私 contract | pytest、Teacher check | Teacher 可解释且失败不阻断游戏 | pytest、privacy 检查 |
| T5 | React、BFF HTTP、Core HTTP | `npm run build`、PowerShell HTTP | 构建成功，state/step contract 完整 | build 输出、JSON 响应 |
| T6 | 新 Docker 镜像和 Compose | `compose build/up` | 三服务 healthy，CPU 推理和能力字段可见 | `ps`、日志、API 响应 |
| T7 | 浏览器 UI | 手动点击 | 对局、详情、Teacher、错误态可操作 | 截图/录屏/验收表 |

## 5. T0：环境、模型和配置检查

### 5.1 Compose 配置静态检查

```powershell
$Compose = @(
  "--env-file", "deploy/docker/.env",
  "-f", "deploy/docker/compose.cpu.yml"
)
& $Docker compose @Compose config --quiet
```

重点确认：

- Core 使用 `python:3.10-slim-bookworm`；
- CPU Torch 使用 `2.6.0+cpu`；
- 模型目录以只读方式挂载到 `/app/models/v3`；
- Core、BFF、Teacher 不直接暴露宿主端口；
- Web 唯一发布 `127.0.0.1:8080`；
- Teacher 通过 profile 可选，不是游戏启动硬依赖。

### 5.2 模型资产检查

```powershell
Get-Item -LiteralPath "models\v3\policy.pt" | Select-Object FullName,Length,LastWriteTime
Get-Content -LiteralPath "models\v3\README.md"
```

通过标准：模型文件非空，路径与 Compose 的 `GWENT_SERVER_CHECKPOINT` 一致，启动日志不会回退到不存在的 checkpoint。

若存在安装元数据，还要核对：

- checkpoint hash；
- RL schema/action grammar/reward contract；
- deck/model family；
- device compatibility。

不允许用 `strict=False`、静默裁剪或伪造 metadata 绕过不兼容。

## 6. T1：静态、schema、卡牌数据和配置检查（不含训练）

### 6.1 非训练 quick 子集

本轮不直接执行 `python scripts/check.py quick`，因为该入口还会调用 training task validation。先执行不涉及训练的检查：

```powershell
python tools/codegen/generate_card_data.py --check
python tools/codegen/validate_card_data.py
python .agents/skills/core-environment/scripts/check_schema.py
python .agents/skills/teacher-explanation/scripts/check_teacher.py
```

宿主无 Python 时，在包含项目 Python 依赖的 Docker 镜像中执行等价命令：

```powershell
& $Docker run --rm `
  --mount "type=bind,source=$((Get-Location).Path),target=/workspace" `
  -w /workspace `
  -e PYTHONPATH=/workspace/python/src `
  gwent-local-core:latest `
  sh -c "python tools/codegen/generate_card_data.py --check && python tools/codegen/validate_card_data.py && python .agents/skills/core-environment/scripts/check_schema.py && python .agents/skills/teacher-explanation/scripts/check_teacher.py"
```

本轮非训练检查必须覆盖并通过：

- Agent/Skill 包完整性；
- Markdown 本地链接；
- Python 工具语法；
- Product contract 硬化检查；
- 本地模型槽检查；
- 卡牌 codegen 一致性；
- `supported_cards.json` 校验；
- RL schema 校验；
- Teacher runtime/product integration 文件检查。

以下项目本轮不执行，留到训练测试恢复时：

- `validate_training.py --all`；
- training task YAML 完整校验；
- checkpoint warm-start/resume/migration 验证。

### 6.2 本次卡牌能力字段的专门检查

检查以下链路不能断：

```text
supported_cards.json.description
    → CardTextRecord.description
    → human_vs_ai.py objects[].ability_text
    → BFF GameObject.ability_text
    → frontend GameObject.ability_text
    → CardDetailModal 卡牌能力
```

通过标准：

- 普通支持卡牌的 `ability_text` 非空；
- token/未登记能力的卡牌返回空字符串而不是伪造文本；
- BFF 缺少 `ability_text` 时严格报 contract 错误；
- React 不从 `status`、`label`、`source` 或 `target` 推断能力。

## 7. T2：Core、C ABI、legal action 和 golden 回归

### 7.1 Core/C ABI 聚焦测试

```powershell
cmake -S . -B .build\core-test -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_SHARED_LIBS=ON `
  -DGWENT_BUILD_TESTS=ON `
  -DGWENT_BUILD_TRACE_TOOLS=OFF
cmake --build .build\core-test --parallel `
  --target gwent_core_model_tests gwent_legal_actions_tests gwent_c_api_smoke_tests gwent_rl_c_api_tests gwent_rl_collector_c_api_tests
ctest --test-dir .build\core-test --output-on-failure -R "gwent_core_model_tests|gwent_legal_actions_tests|gwent_c_api_smoke_tests|gwent_rl_c_api_tests|gwent_rl_collector_c_api_tests"
```

本轮只执行 Core/C ABI/C++ tests，不执行 `scripts/check.py test`，因为该统一入口还会继续运行 Python training tests。需要覆盖：

- `gwent_core_model_tests`；
- `gwent_legal_actions_tests`；
- `gwent_c_api_smoke_tests`；
- `gwent_rl_c_api_tests`；
- `gwent_rl_collector_c_api_tests`；
- C++ Core model；
- legal actions；
- C API smoke；
- RL C API；
- RL collector C API。

### 7.2 Core 全量 CTest

```powershell
cmake -S . -B .build\core-full -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_SHARED_LIBS=ON `
  -DGWENT_BUILD_TESTS=ON `
  -DGWENT_BUILD_TRACE_TOOLS=ON
cmake --build .build\core-full --parallel
ctest --test-dir .build\core-full --output-on-failure
```

通过标准：Core CMake 构建和 CTest 全部退出码为 0。`python/tests`、PPO、Collector 和训练初始化不在本轮执行。

### 7.3 C API 信息边界

至少确认以下 regression 不变：

- 自己手牌可见；
- 对手手牌身份不可见；
- 对手手牌数量可见；
- 公共牌组信息按 contract 暴露；
- `human_vs_ai` 不把 AI 隐藏手牌送给 HTTP；
- `manual_test` 才允许显式调试双方手牌。

### 7.4 Golden trace 和 legal surface

Core 构建 trace runner 后执行：

```powershell
cmake -S . -B .build\golden -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_SHARED_LIBS=ON `
  -DGWENT_BUILD_TESTS=ON `
  -DGWENT_BUILD_TRACE_TOOLS=ON
cmake --build .build\golden --parallel

& .build\golden\gwent_trace_runner.exe `
  --scenario trace_smoke `
  --script tools\golden\cases\trace_smoke.trace `
  --seed 0 `
  --starting-player 0 `
  --deck trace `
  --effects none `
  --include-legal-surface `
  > .build\golden\trace_smoke.surface.json
```

再使用以下检查：

- Python/C++ semantic pair；
- `trace_diff.py`；
- `legal_surface_diff.py`；
- `seed_sweep.py --repeat-check`；
- insert position、pending target、row choice 和 pass 场景。

通过标准：同 seed、同动作脚本下，状态、决策顺序、合法动作集合和关键 reward 一致；若 entity id 仍有已知分配差异，必须显式记录临时忽略项，不能默认忽略。

## 8. T3：训练层、checkpoint 和训练 smoke

本轮只执行本地最小 smoke，不启动 512 环境并行、不执行正式训练、不修改产品模型槽。训练服务器和长时间训练仍然暂缓。

### 8.1 本地最小 smoke

使用当前 CPU Docker Core 镜像和镜像内 `/opt/gwent/lib/libgwent_core.so`：

```powershell
docker run --rm `
  --mount "type=bind,source=$((Get-Location).Path),target=/workspace" `
  -w /workspace `
  -e PYTHONPATH=/workspace/python/src `
  gwent-local-core:latest `
  python python/tests/smoke_ctypes_collector.py `
    --num-envs 1 --rounds 2 --seed 46 `
    --library /opt/gwent/lib/libgwent_core.so

docker run --rm `
  --mount "type=bind,source=$((Get-Location).Path),target=/workspace" `
  -w /workspace `
  -e PYTHONPATH=/workspace/python/src `
  gwent-local-core:latest `
  python python/tests/smoke_training_loop.py `
    --library /opt/gwent/lib/libgwent_core.so
```

第二个 smoke 当前脚本内部固定 4 个环境、1 次 update 和短 replay；如果脚本参数发生变化，必须先确认并发量仍然小于本地限制。

### 8.2 后续恢复正式训练时执行

```powershell
python scripts/check.py train
```

恢复训练测试后，只执行最短 collector/PPO smoke，不启动正式长跑。需要验证：

- observation/action/reward contract 与 Core 版本一致；
- collector 能创建环境、采样、结束 episode；
- GAE、rollout buffer、PPO update 不报 shape/device 错误；
- checkpoint save/load metadata 完整；
- CPU 路径不意外调用 CUDA。

### 8.3 后续恢复测试时执行

正式训练测试恢复后，再做 checkpoint compatibility/inference 验证。当前 Docker 产品测试仍然可以验证 CPU 推理，但不代表训练已完成。

## 8.4 本轮保留的产品推理检查

本轮仍需使用已下载的最优模型验证产品是否能够启动并推理；这不是训练测试。使用 `models/v3/policy.pt` 启动 Docker Core，确认：

- `/health` 返回 `ok=true`；
- `checkpoint` 指向 `/app/models/v3/policy.pt`；
- `device=cpu`；
- `schema_version` 与模型 metadata 匹配；
- `human_vs_ai` 新局后 `last_ai_actions` 能产生动作；
- AI 连续动作不会卡在非法 option 或异常 pending choice。

训练 server 和产品 runtime 必须分离：产品测试不要求服务器 checkpoint 目录、历史 runs 或训练进程存在。

## 9. T4：Teacher、BFF 和隐私测试

### 9.1 Teacher 静态和单元测试

```powershell
python .agents/skills/teacher-explanation/scripts/check_teacher.py
PYTHONPATH=. pytest -q services/teacher/tests
PYTHONPATH=apps/web/backend pytest -q apps/web/backend/tests/test_teacher_api.py
```

通过标准：

- deterministic fallback 可用；
- explanation 只基于 executed action、public state 和公开 card/rule knowledge；
- Teacher 不修改 `option_index`；
- Teacher 不重新计算 legal actions；
- Teacher 不生成隐藏候选动作；
- Teacher 失败时游戏仍可继续。

### 9.2 BFF contract 测试

使用固定测试镜像，不在每次运行时临时安装 pytest：

```powershell
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml --profile test run --rm test python -m pytest -q apps/web/backend/tests
```

覆盖：

- Core API version；
- strict Pydantic extra fields；
- uint64 `stable_hash`；
- structured target metadata；
- `ability_text`；
- BFF 不加载 checkpoint、不 import `gwent_rl`、不加载 C++ shared library；
- Teacher optional dependency 行为。

## 10. T5：前端构建和 HTTP 产品链路

### 10.1 前端静态构建

```powershell
cd apps/web/frontend
npm ci
npm run typecheck
npm run build
cd ../..
```

通过标准：TypeScript 没有 contract 类型错误，Vite 构建成功，生成 `dist/index.html` 和静态资源。

### 10.2 Core HTTP smoke

Core 运行后用 PowerShell 检查：

```powershell
Invoke-RestMethod http://127.0.0.1:8008/health
Invoke-RestMethod http://127.0.0.1:8008/state
```

然后依次验证：

1. `POST /new` 的 `manual_test`；
2. `POST /new` 的 `human_vs_ai`；
3. 读取最新 `actions`；
4. 只提交最新 `actions[0].index`；
5. `choose_row` 和 `choose_insert_position` 连续决策；
6. 非法/过期 option 被 Core 拒绝；
7. state 中 `objects[].ability_text` 与卡牌清单一致。

### 10.3 BFF/Web same-origin smoke

```powershell
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8080/healthz
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8080/
Invoke-RestMethod http://127.0.0.1:8080/api/health
```

使用 `Invoke-RestMethod` 完成一局最小链路：

```powershell
$newBody = @{ seed = 123; mode = "manual_test"; player0_deck_id = 0; player1_deck_id = 0 } | ConvertTo-Json
$state = Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:8080/api/game/new `
  -ContentType "application/json" `
  -Body $newBody

$state.objects | ForEach-Object {
  if (-not ($null -ne $_.ability_text)) { throw "missing ability_text" }
}

if ($state.actions.Count -gt 0) {
  $stepBody = @{ option_index = $state.actions[0].index } | ConvertTo-Json
  $nextState = Invoke-RestMethod `
    -Method Post `
    -Uri http://127.0.0.1:8080/api/game/step `
    -ContentType "application/json" `
    -Body $stepBody
}
```

## 11. T6：Docker Compose 集成测试

### 11.1 重建并启动新镜像

当前运行中的旧容器不能代表本次源码。能力字段、Python 3.10 和新依赖必须通过重新构建进入镜像：

```powershell
$Compose = @(
  "--env-file", "deploy/docker/.env",
  "-f", "deploy/docker/compose.cpu.yml"
)

& $Docker compose @Compose build core bff web
& $Docker compose @Compose up -d
& $Docker compose @Compose ps
```

Teacher 集成单独验证：

```powershell
& $Docker compose @Compose --profile teacher up -d --build
```

### 11.2 服务健康和版本检查

```powershell
& $Docker compose @Compose ps
& $Docker compose @Compose logs --tail=100 core
& $Docker compose @Compose logs --tail=100 bff
```

必须确认：

- core、bff、web healthy；启用 profile 时 teacher healthy；
- Core 日志显示 CPU device 和正确模型路径；
- BFF `/api/health` 的 `core.ok=true`；
- Web `/healthz` 返回 200；
- 镜像内部 Python 为 3.10 系列；
- Torch/NumPy/FastAPI/Pydantic 版本与部署计划一致；
- BFF 未意外携带训练服务器资产；
- checkpoint 是只读挂载，不被容器覆盖。

### 11.3 Docker 游戏回归

按以下顺序执行：

1. Web 页面可以创建 `human_vs_ai`；
2. AI 自动完成首个决策；
3. P1 隐藏手牌不出现在 `/api/game/state` 或 Teacher evidence；
4. 人类提交最新合法 `option_index` 后 state 更新；
5. 动态排内插入位置正常；
6. 点击卡牌“详情”可以看到能力文本、实时战力、护甲和状态；
7. 切换 `manual_test` 后双方手牌可见；
8. Teacher 关闭或异常时，游戏仍能创建和出牌；
9. Teacher profile 启用时，`/api/teacher/health` 和解释接口正常。

## 12. T7：浏览器手动验收清单

浏览器验收只验证 UI 呈现和交互，不把浏览器视为规则事实来源。

### 12.1 基础页面

- [ ] `http://127.0.0.1:8080` 可以打开；
- [ ] 页面没有白屏或 JavaScript 启动错误；
- [ ] 对局状态、回合、双方分数正确显示；
- [ ] 页面刷新后错误态明确，不显示伪造对局。

### 12.2 卡牌详情

- [ ] 手牌卡牌可以打开详情；
- [ ] 战场卡牌可以打开详情；
- [ ] 详情显示卡牌名称、类型、Card ID；
- [ ] 详情显示“卡牌能力”；
- [ ] 多行能力文本换行正常；
- [ ] 当前战力、护甲和持续状态与 API 实时值一致；
- [ ] 无能力文本的 token 显示空态，不出现 `undefined`；
- [ ] 关闭弹窗后不改变对局或选中动作。

### 12.3 动作和隐私

- [ ] 只渲染 Core 返回的合法动作；
- [ ] 目标选择、排选择、插入位置选择可以完成；
- [ ] 不通过 label 文本解析目标；
- [ ] AI 隐藏手牌不在 UI 出现；
- [ ] AI 决策过程只展示已执行 AI 动作；AI 教师只解释 Core clone 已执行的当前人类回合分支动作；
- [ ] Teacher 失败只影响解释面板，不阻断游戏。

## 13. 失败定位顺序

| 现象 | 第一检查点 | 不要先做的事 |
|---|---|---|
| Core 无法启动 | 动态库、模型路径、catalog、Core 日志 | 不先调模型参数 |
| legal action 缺失 | Core pending choice、C API、trace、catalog | 不在前端补动作 |
| BFF 422/协议错误 | Core JSON 与 Pydantic contract | 不放宽 `extra=forbid` |
| AI 推理失败 | checkpoint metadata、schema、device、模型 loader | 不静默 `strict=False` |
| Teacher 无解释 | evidence、privacy filter、fallback/provider | 不把 label 当事实来源 |
| 卡牌能力为空 | `supported_cards.json`、card id、Core adapter | 不在 React 写卡牌规则表 |
| 页面白屏 | `npm run build`、静态资源、浏览器 console | 不直接修改 API contract |
| Docker 健康失败 | Compose 依赖顺序、healthcheck、服务日志 | 不删除模型或盲目重装 Docker |

## 14. 本轮非训练验收命令顺序

### 14.1 不启动 Docker 的 Core/Product/Teacher 验收

```powershell
python tools/codegen/generate_card_data.py --check
python tools/codegen/validate_card_data.py
python .agents/skills/core-environment/scripts/check_schema.py
python .agents/skills/teacher-explanation/scripts/check_teacher.py

PYTHONPATH=. pytest -q services/teacher/tests
PYTHONPATH=apps/web/backend pytest -q apps/web/backend/tests

cd apps/web/frontend
npm ci
npm run typecheck
npm run build
cd ../..
```

### 14.2 Docker/产品验收

```powershell
& $Docker compose @Compose build
& $Docker compose @Compose up -d
& $Docker compose @Compose ps
Invoke-RestMethod http://127.0.0.1:8080/api/health
Invoke-RestMethod http://127.0.0.1:8080/api/game/state
```

最后再执行第 10、11、12 节的 API、Docker 和浏览器清单。

## 15. 本轮完成定义和交付证据

只有同时满足以下条件，才可以写“非训练项目链路验收通过”：

- [ ] T0 环境和模型资产通过；
- [ ] T1 非训练静态/schema/card/Product 检查通过；
- [ ] T2 C++/C ABI/CTest/golden 通过；
- [ ] T3 训练相关项目已明确标记为暂缓，不作为本轮通过条件；
- [ ] 产品 Docker CPU 推理通过；
- [ ] T4 Teacher、BFF、隐私测试通过；
- [ ] T5 前端构建和 HTTP contract smoke 通过；
- [ ] T6 新镜像 Compose 健康、游戏、模型推理和能力字段通过；
- [ ] T7 浏览器手动验收通过，或明确标记为未执行；
- [ ] 所有失败项都有 issue、日志或复现命令；
- [ ] 测试使用的镜像 tag、模型 hash、contract 版本和 git 工作区状态已记录。

建议保存以下证据：

```text
test-evidence/
├── environment.txt
├── quick.log
├── ctest.log
├── python-pytest.log
├── teacher-pytest.log
├── frontend-build.log
├── docker-ps.txt
├── core.log
├── bff.log
├── api-health.json
├── game-state-manual-test.json
├── game-state-human-vs-ai.json
└── golden/
```

测试完成后，在此文档顶部更新当前状态，并注明：执行日期、Docker 镜像版本、模型 hash、通过/失败数量，以及尚未覆盖的浏览器或性能项目。
