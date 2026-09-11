# 本地 CPU Docker 单机对战

本目录提供的是产品运行时，而不是训练环境：它只加载 `models/v3/` 中的最终 V3 `policy.pt`。PPO、
Collector、CUDA、`runs/` 和服务器训练资产均不会进入 Docker 镜像或挂载目录。

本地产品镜像与服务器 `crk` 环境按 Python 3.10、Torch 2.6.0、NumPy 1.26.4 和 Web 依赖版本对齐；
CPU Docker 使用 `torch==2.6.0+cpu`，不会安装服务器的 CUDA 12.4 wheel。官方基础镜像使用
`python:3.10-slim-bookworm`，因此对齐的是 Python 3.10 主次版本，补丁版本由该基础镜像当前发布内容决定，
不是强行声称精确为 3.10.8。

训练环境不属于本 Compose。训练 Docker、CPU smoke 和服务器 GPU/512 并行训练见
[训练 Docker 操作手册](TRAINING_DOCKER.md)。

此前旧版镜像的 Docker 构建、启动和健康检查已在 Docker Desktop Linux Engine 上验证通过；本次按锁定
环境重建的新版镜像已完成构建，首次构建会下载 Python、PyTorch CPU wheel 和 C++ 编译依赖。新版容器
替换与启动的复核见安装复盘文档。

本次安装中遇到的网络、基础镜像、依赖版本和权限问题，以及可复用的 PowerShell 成功流程，见
[本地 Docker 安装复盘](LOCAL_DOCKER_INSTALL_NOTES.md)。

## 结构

```text
浏览器 :8080
    │ 同源 /api
    ▼
web (Nginx) ──> bff (FastAPI) ──> core (C++ + CPU policy)
                                  │
                                  └──> teacher（可选，只读解释）
```

宿主机只发布 `127.0.0.1:8080`。Core、BFF 和 Teacher 仅使用 Compose 内部网络；浏览器不会直接访问
shared library、checkpoint 或 Core HTTP 服务。

## 启动前条件

1. 安装 Docker Desktop，并切换到 Linux containers / WSL2 后端。
2. 将从服务器下载的最优模型放入 `models/v3/policy.pt`。这一个文件是单机推理的必需模型资产；不需要
   下载任何中间 checkpoint、`runs/` 或 `artifacts/`。

3. 推荐（但不是启动前置条件）使用生产安装入口对模型做本地校验并记录来源：

   ```powershell
   python scripts/install_model.py C:\path\to\best.pt
   ```

4. 可选的 `models/v3/installed.json` 由安装入口生成，用于记录哈希与 contract 元数据。它缺失时 Docker
   会给出警告，但仍会加载非空的 `policy.pt`。Compose 始终以只读方式挂载模型目录。

5. 复制部署配置模板（首次使用即可）：

   ```powershell
   Copy-Item deploy/docker/.env.example deploy/docker/.env
   ```

## 启动命令

在仓库根目录执行：

```powershell
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml build
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml up -d
```

完成后浏览器打开 `http://127.0.0.1:8080`。默认 Core 以 `human_vs_ai` 启动：P0 为人类，P1 为 CPU
checkpoint。Web 页面仍保留 `manual_test`，但它是显式的本地规则调试模式，不应作为正常单机对战入口。

## 测试容器（按需启动）

测试环境与产品运行环境分开。`test` 是 Compose 的可选 profile，不会随 `core`、`bff`、`teacher`、`web`
一起常驻运行，也不承担训练任务。

测试镜像固定 Python 3.10、CPU Torch、pytest、pytest-asyncio、httpx 及项目测试依赖；测试容器通过
bind mount 读取当前仓库，因此修改 Python 代码后不需要重新构建镜像。

首次构建（只在测试依赖或基础镜像变化时重新下载）：

```powershell
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml --profile test build test
```

运行默认的适配器、Teacher 和 BFF 单元/contract 测试：

```powershell
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml --profile test run --rm test
```

只运行 BFF 测试：

```powershell
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml --profile test run --rm test python -m pytest -q apps/web/backend/tests
```

第一次 `build` 会下载依赖并生成 `gwent-local-test` 镜像；之后 `run` 直接复用镜像，不会重新安装
pytest。`--rm` 只删除本次测试容器，不删除测试镜像、项目文件或模型文件。

Teacher 是可选服务。要启用解释面板，使用：

```powershell
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml --profile teacher up -d --build
```

未启用或不可用时，Teacher API 会返回可展示的错误；游戏创建和出牌不受影响。

## 常用操作

```powershell
# 查看各服务状态与健康检查
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml ps

# 查看 Core 推理启动日志
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml logs --tail=100 core

# 替换模型后，只重启 Core；无需重新构建镜像
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml restart core

# 停止并移除容器/网络，不会删除宿主机模型
docker compose --env-file deploy/docker/.env -f deploy/docker/compose.cpu.yml down
```

## 可配置项

`deploy/docker/.env` 管理部署值，Core 再读取 `GWENT_SERVER_*` 环境变量（兼容旧的 `SERVER_*` 名称）。
`.env.example` 提供默认值：

| 部署变量 | 默认值 | 作用 |
|---|---|---|
| `GWENT_DOCKER_MODEL_DIR` | `../../models/v3` | 包含最终 `policy.pt` 的宿主机目录 |
| `GWENT_DOCKER_WEB_PORT` | `8080` | 唯一发布到宿主机的端口 |
| `GWENT_DOCKER_MODE` | `human_vs_ai` | 正常对战或 `manual_test` 调试模式 |
| `GWENT_DOCKER_TORCH_INDEX_URL` | PyTorch CPU index | Core 镜像的 CPU torch 来源 |
| `GWENT_DOCKER_PYTHON_BASE_IMAGE` | `python:3.10-slim-bookworm` | Python 3.10 基础镜像 |
| `GWENT_DOCKER_TORCH_VERSION` | `2.6.0+cpu` | CPU 推理 Torch 版本 |

Compose 传给 Core 的运行时变量如下：

| 变量 | 值 | 作用 |
|---|---|---|
| `GWENT_SERVER_MODE` | `human_vs_ai` | 正常单机对战模式 |
| `GWENT_SERVER_CHECKPOINT` | `/app/models/v3/policy.pt` | 只读模型路径 |
| `GWENT_SERVER_LIBRARY` | `/opt/gwent/lib/libgwent_core.so` | 镜像内 C++ Core 动态库 |
| `GWENT_SERVER_DEVICE` | `cpu` | 禁止依赖 CUDA |
| `GWENT_SERVER_HOST` | `0.0.0.0` | 仅供 Compose 内部访问 |

本地 IDE 直接运行 `tools/server/human_vs_ai.py` 时仍使用原来的默认值：`manual_test`、CPU、
`127.0.0.1:8008`。

## 首次验收清单

首次实际运行时，按以下顺序验证：

1. `core` 健康检查通过，且启动日志显示正确的 `policy.pt`、CPU device 和 schema；
2. `bff` 健康接口的 `core.ok` 为 `true`；
3. 页面能创建 `human_vs_ai` 对局，AI 可完成首个决策；
4. 人类仅能提交 Core 最新 state 返回的 `option_index`，包括动态插入位置；
5. 浏览器和 Teacher 均看不到 AI 手牌或未执行候选；
6. 分别关闭 Teacher 和切换 `manual_test`，确认前者不阻断游戏、后者才显示双方手牌。

相关边界见 [Core HTTP Contract](../../apps/web/docs/CORE_API_CONTRACT.md)、
[本地 Docker 方案](LOCAL_DOCKER_PLAN.md) 和 [V3 模型槽说明](../../models/v3/README.md)。
