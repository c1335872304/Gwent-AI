# 训练 Docker 规划

> 状态：Docker 文件已落地。本机 CPU 镜像、任务 `plan` 和 16 局 smoke 已验证；GPU 与 512 并行只能在训练服务器完成。

本文描述如何把当前服务器训练环境容器化，并同时支持本地 CPU 小规模验证。它不改变本地人机对战 Docker 的职责，也不把训练容器加入正常的对战 Compose。

## 1. 目标与边界

训练 Docker 的目标是固定训练运行时，减少服务器上的 Python、PyTorch、C++ Core 和训练代码版本漂移。

训练容器负责：

- 读取 Training Task 和算法配置；
- 直接加载 C++ Core 动态库；
- 运行 Collector、PPO、评估和 checkpoint 保存；
- 把 `runs/`、`artifacts/` 作为训练输出交给宿主机保存。

训练容器不负责：

- 提供浏览器对战页面；
- 提供 Core HTTP、BFF 或 Teacher 服务；
- 直接覆盖产品模型 `models/v3/policy.pt`；
- 在容器内部运行 SSH 服务。

本地对战仍使用 [本地 CPU Docker](LOCAL_DOCKER.md)。训练 Docker 是另一套镜像和 Compose 配置。

## 0. 本机快速开始

首次使用时复制模板（本仓库本机已创建默认的 `deploy/docker/.env.train`）：

```powershell
Copy-Item deploy/docker/.env.train.example deploy/docker/.env.train
```

构建 CPU Trainer 镜像：

```powershell
docker compose --env-file deploy/docker/.env.train -f deploy/docker/compose.train.yml -f deploy/docker/compose.train.cpu.yml build trainer
```

默认命令只做低成本 task 预检，不会开始训练：

```powershell
docker compose --env-file deploy/docker/.env.train -f deploy/docker/compose.train.yml -f deploy/docker/compose.train.cpu.yml run --rm trainer
```

实际运行 16 局 CPU smoke 的命令见第 10.2 节。该任务的 `resume: never` 会拒绝覆盖已有 checkpoint；需要重复实验时应创建新的 task/run_dir，而不是删除已有结果。

## 2. 当前项目事实

当前训练入口不是一个新的训练脚本，而是已有的任务编排器：

```text
Training Task
  -> gwent_rl.training.cli
  -> TrainingOrchestrator
  -> gwent_rl.train_ppo
  -> RlCollector
  -> libgwent_core.so
```

相关事实来源：

- `configs/training/*.yaml` 是算法和实验参数；
- `training/tasks/*.yaml` 是一次正式运行的预算、初始化、runtime、checkpoint 和 resume policy；
- `python/src/gwent_rl/training/cli.py` 提供 `plan`、`run`、`status`；
- `python/src/gwent_rl/collector.py` 直接加载 `libgwent_core.so`；
- `training/tasks/smoke.yaml` 是低成本端到端验证任务；
- [训练与 V3 模型说明](TRAINING_AND_MODEL.md) 定义训练、评估和产品模型的边界。

### 2.1 512 的真实含义

正式任务中的：

```yaml
runtime:
  num_envs: 512
```

表示一个 Trainer 进程内部创建 512 个 Core 游戏环境，不表示创建 512 个 Docker 容器。

部分任务还存在：

```yaml
evaluation:
  games: 512
  num_envs: 128
```

这里的 `games: 512` 是评估完整对局总数，`num_envs: 128` 是评估时的并行环境数，不能与训练阶段的 `runtime.num_envs` 混为一谈。

## 3. 推荐运行拓扑

### 3.1 训练服务器

```text
训练服务器主机
  └─ trainer 容器
      ├─ Python 3.10（生产时固定已验证的基础镜像 digest）
      ├─ PyTorch 2.6.0 + CUDA 12.4（GPU 版本）
      ├─ gwent_rl 训练代码
      ├─ TrainingOrchestrator / PPO
      ├─ Collector
      ├─ 512 个 Core 环境句柄
      └─ libgwent_core.so
```

训练时不建议额外运行 Core HTTP 容器。Collector 直接调用动态库，避免 512 个环境通过 HTTP 往返造成性能和故障面扩大。

### 3.2 本地训练

```text
本地 Windows + Docker Desktop
  └─ trainer 容器（CPU）
      ├─ PyTorch 2.6.0 + CPU
      ├─ 少量并行环境
      └─ smoke / debug / 小规模训练
```

本地训练容器与本地对战的 `core` 容器是两个不同用途的运行时。前者加载 Core 动态库做批量采样，后者提供对战 HTTP API。

## 4. 已增加的 Docker 文件

```text
deploy/docker/
├─ Dockerfile.train
├─ compose.train.yml
├─ compose.train.cpu.yml
├─ compose.train.gpu.yml
├─ requirements.train.txt
└─ .env.train.example
```

使用一个 `Dockerfile.train`，通过构建参数切换 CPU/GPU Torch；使用 CPU 和 GPU 两个 Compose 覆盖文件，避免本地命令误拉取 CUDA 运行时。

## 5. Dockerfile.train 设计

### 5.1 Core 编译阶段

镜像的前置 build stage 编译 C++ Core：

```text
Core source
  -> cmake / compiler / ninja
  -> libgwent_core.so
```

运行时阶段只复制编译结果，不保留编译器和源码构建缓存。

训练镜像必须把 Core 编译工具链作为明确的版本 contract 管理。当前服务器的 `gwent-build` 环境与本地产品 Docker 的 Debian 编译环境并不自动等价，尤其要检查：

- GCC / G++ 版本；
- libstdc++、libgomp 和 glibc 兼容性；
- CMake、Ninja 版本；
- C ABI 和 RL schema/action grammar。

优先方案是使用固定的 Linux builder 镜像从当前仓库源码编译；备选方案是使用同一 builder 预先生成并校验过的 `libgwent_core.so`。不建议把服务器某次手工编译的 `.so` 静默挂载进镜像而不记录来源。

### 5.2 Trainer 运行阶段

运行阶段包含：

- Python 3.10；
- PyTorch 2.6.0；
- NumPy 1.26.4 及训练实际依赖；
- `python/src/gwent_rl`；
- `training/`、`configs/training/` 和必要的 `scripts/`；
- 镜像内的 `/opt/gwent/lib/libgwent_core.so`。

GPU 构建使用 PyTorch `2.6.0+cu124`，并由 GPU Compose 请求宿主机 GPU。CPU 构建使用 `2.6.0+cpu`，不安装 CUDA 依赖。基础镜像默认是 `python:3.10-slim-bookworm`；服务器需要可复现时应固定为经过验证的不可变镜像 digest，而不是假设滚动标签永远是某个 Python 补丁版本。

镜像不安装 `openssh-server`，也不复制 SSH 私钥、公钥或服务器凭据。

## 6. Compose 设计

### 6.1 公共 trainer 服务

公共配置负责：

- 工作目录，例如 `/workspace`；
- `PYTHONPATH`；
- Core 动态库路径；
- task/config 路径；
- `runs/` 和 `artifacts/` 输出目录；
- 日志输出和停止策略；
- 共享内存大小；
- 非 root 用户运行。

### 6.2 CPU Compose

CPU Compose 只设置：

```text
device=cpu
torch=2.6.0+cpu
num_envs=smoke 任务默认值
```

它用于本地 smoke、collector 验证和小规模训练，不设置 `gpus: all`。

### 6.3 GPU Compose

GPU Compose 设置：

```text
device=cuda
torch=2.6.0+cu124
gpus=all
```

GPU 是否可用由服务器主机的 NVIDIA 驱动和 Docker GPU runtime 决定。镜像内 CUDA wheel 版本与宿主机驱动兼容，不等于容器自己拥有 GPU 硬件。

### 6.4 不默认常驻

训练服务不应该随本地对战命令自动启动。正式训练应使用一次性容器：

```text
docker compose ... run --rm trainer ...
```

这样训练完成后容器退出，但 `runs/` 和 `artifacts/` 仍保留在宿主机。长时间训练也可以使用后台运行和日志查看，但必须明确对应的 task 和 run_dir。

## 7. 目录挂载与资产所有权

推荐挂载关系：

```text
只读：
├─ configs/
├─ training/tasks/
├─ data/
└─ models/v3/                 # warm-start 或 frozen opponent 输入

可写：
├─ runs/
└─ artifacts/
```

三类模型/训练资产必须保持分离：

| 目录 | 用途 | Docker 权限 |
|---|---|---|
| `runs/` | 一次训练的日志、checkpoint、评估结果 | 可写 |
| `artifacts/` | 长期固定引用的训练资产 | 按任务需要可写 |
| `models/v3/` | 产品最终推理模型 | 训练容器只读 |

训练完成后，经过评估和 promotion，才使用 `scripts/install_model.py` 更新产品模型。训练过程不能直接把 `latest.pt` 覆盖成产品 `policy.pt`。

## 8. 路径和配置改造要求

当前部分任务文件仍包含服务器绝对路径，例如 `/home/...`。这些路径进入容器后会失效，必须在训练 Docker 实施前处理。

统一规则：

1. 训练任务中的路径使用项目根目录相对路径；
2. 容器内固定项目根目录，例如 `/workspace`；
3. Core 动态库允许通过 `GWENT_CORE_LIBRARY` 覆盖；
4. checkpoint 通过 task、warm-start 参数或容器内路径传入；
5. 禁止把宿主机用户目录写死在可提交的 task 文件中。

目标状态示例：

```text
项目根目录：/workspace
Core 库：/opt/gwent/lib/libgwent_core.so
模型输入：/workspace/models/v3/policy.pt
训练输出：/workspace/runs/...
长期资产：/workspace/artifacts/...
```

## 9. SSH 边界

SSH 只用于登录训练服务器主机：

```text
本地电脑
  └─ SSH → 训练服务器主机
             └─ docker compose → trainer 容器
```

不采用：

```text
服务器主机 → SSH → trainer 容器
```

原因：

- `docker exec` 已经可以进入运行中的容器；
- 单机 512 环境不需要容器间 SSH；
- 容器内 SSH 会增加端口、密钥和安全维护成本；
- 容器之间的服务通信应使用 Docker 网络或训练框架的 rendezvous 机制。

只有未来做多机分布式训练，且所选框架明确要求 SSH 时，才单独设计 SSH 或调度系统接入。本阶段不提前加入。

## 10. 训练命令设计

训练 Docker 使用现有 `training.cli`，而不是新增一套绕过 Task 的脚本。

### 10.1 先查看计划

```powershell
docker compose --env-file deploy/docker/.env.train -f deploy/docker/compose.train.yml -f deploy/docker/compose.train.cpu.yml run --rm trainer `
  python -m gwent_rl.training.cli plan `
  --task /workspace/training/tasks/smoke.yaml `
  --project-root /workspace `
  --library /opt/gwent/lib/libgwent_core.so
```

### 10.2 本地 smoke

```powershell
docker compose --env-file deploy/docker/.env.train -f deploy/docker/compose.train.yml -f deploy/docker/compose.train.cpu.yml run --rm trainer `
  python -m gwent_rl.training.cli run `
  --task /workspace/training/tasks/smoke.yaml `
  --project-root /workspace `
  --library /opt/gwent/lib/libgwent_core.so
```

### 10.3 服务器正式任务

服务器正式任务仍从 `training/tasks/*.yaml` 选择，例如 warm-start 或正式 250k 任务。执行前必须先 `plan`，检查：

- 任务和算法 config 存在；
- Core 动态库路径正确；
- schema/action grammar 兼容；
- warm-start 和 resume 语义没有混用；
- run_dir 不会覆盖其他实验；
- device 和 `num_envs` 符合服务器资源。

正式训练不在本地 Windows 上用 512 并行做验收。

服务器的 GPU 构建与运行使用同一个 Trainer Dockerfile，只额外叠加 GPU Compose：

```bash
ssh user@training-server
cd /path/to/gwent_v3
cp deploy/docker/.env.train.example deploy/docker/.env.train
# 根据服务器实际目录、UID/GID 修改 .env.train 后执行：
docker compose --env-file deploy/docker/.env.train -f deploy/docker/compose.train.yml -f deploy/docker/compose.train.gpu.yml build trainer
docker compose --env-file deploy/docker/.env.train -f deploy/docker/compose.train.yml -f deploy/docker/compose.train.gpu.yml run --rm trainer python -m gwent_rl.training.cli plan --task /workspace/training/tasks/<task>.yaml --project-root /workspace --library /opt/gwent/lib/libgwent_core.so
```

确认 `plan`、GPU 和小规模任务都通过后，才将正式 task 的 `runtime.num_envs` 设为或保留为 512。

## 11. 本地训练能力

本地可以训练，但定位是验证和小实验，不是替代服务器正式训练。

### 11.1 推荐本地规模

```yaml
runtime:
  num_envs: 4 或 8
  collector_threads: 2 或 4
  device: cpu
```

当前已有的 `training/tasks/smoke.yaml` 使用 8 个环境、2 个 collector 线程和 CPU，适合作为第一条 Docker 训练验收链。

按此前 Docker Desktop 约 12 个 CPU、7.44GB 容器内存的资源配置，本地可以做：

- Core 动态库加载；
- Collector 小批量采样；
- smoke PPO；
- 小规模 checkpoint 生成；
- warm-start 加载验证；
- 小规模评估。

不建议本地做：

- 512 环境长时间运行；
- 250k 正式预算；
- 服务器吞吐量结论；
- 依赖 CUDA 的正式实验。

如果本机有 NVIDIA GPU，并且 Docker Desktop、WSL2、驱动和 GPU runtime 均可用，理论上可以使用 GPU Compose；但仍需先做 GPU smoke，不能仅凭镜像构建成功判断 GPU 训练可用。

## 12. checkpoint 语义

只有最优模型时，本地可以把它作为 warm-start 输入：

```text
models/v3/policy.pt
  -> 兼容性检查
  -> warm-start 新 run
  -> 新的 runs/tasks/... 输出
```

它不能自动等价于 resume，因为 resume 还需要同一运行的 optimizer、计数器和训练状态。使用外部 checkpoint 前，应先运行项目已有的 warm-start 校验工具，并确认 source schema/action grammar。

## 13. 分阶段实施

### M0：训练 Docker contract

- 确认 CPU/GPU 镜像版本；
- 确认 Core builder 工具链；
- 确认容器内项目根目录、动态库路径和输出目录；
- 确认历史 retired task 的宿主机绝对路径不作为 Docker 任务运行；
- 确认不加入 SSH。

### M1：训练镜像

- 编写 `Dockerfile.train`；
- 编译并复制 `libgwent_core.so`；
- 安装锁定的 Python、PyTorch 和训练依赖；
- 设置非 root、工作目录和 `PYTHONPATH`。

### M2：CPU Compose 与 smoke

- 编写 CPU Compose；
- 用 `training/tasks/smoke.yaml` 执行 `plan`；
- 执行小规模 smoke；
- 检查 checkpoint、日志、评估和 `runs/` 输出。

当前 M0、M1、M2 已在本机完成。`training/tasks/smoke.yaml` 已经通过 task 校验、容器 `plan` 和 16 局 CPU smoke；输出位于 `runs/tasks/trainer_smoke/`，没有写入 `models/v3/`。

### M3：GPU Compose 与服务器验证

- 编写 GPU Compose；
- 验证 `torch.cuda.is_available()`；
- 验证 C++ Core 动态库和 Collector；
- 先用小规模任务验证，再切换正式任务。

### M4：正式训练操作

- 服务器上执行正式 task；
- 512 并行只在服务器资源确认后启用；
- 记录镜像 tag、源码版本、Core ABI、schema、grammar 和 task；
- 训练结果经过 evaluation/promotion 后再安装到 `models/v3/`。

## 14. 验收标准

训练 Docker 正式完成前，至少需要有以下证据：

1. CPU 镜像可以加载 `libgwent_core.so`；
2. `training.cli plan` 能解析 smoke task；
3. smoke 训练能产生预期 checkpoint 和 task state；
4. `runs/`、`artifacts/`、`models/v3/` 的读写边界符合设计；
5. GPU 镜像能确认 CUDA 和 PyTorch 版本；
6. 服务器小规模训练成功后，才允许启用正式 512 并行；
7. 训练容器不依赖 SSH，不启动 BFF、Teacher 或 Web；
8. 产品 Docker 仍能独立加载最终 `models/v3/policy.pt`。

## 15. 与现有 Skill 的关系

本方案遵循 `.agents/skills/training-config/SKILL.md` 和其 Training Configuration Reference：

- Task 是正式运行边界，不把算法逻辑复制进 Docker 配置；
- `runs/`、`artifacts/`、`models/v3/` 三者不混用；
- warm-start、resume 和 migration 分开处理；
- 训练容器不拥有游戏规则，规则异常应回到 Core contract 排查；
- 正式训练和产品 runtime 保持隔离。
