# 本地 Gwent Docker 安装复盘与成功流程

本文记录本次在 Windows + Docker Desktop + WSL2 环境中部署本地 CPU 单机版 Gwent 的实际过程。
它是故障排查和复现记录；日常启动参数仍以 [LOCAL_DOCKER.md](LOCAL_DOCKER.md) 为准。

## 一、最终目标

本地产品运行时只需要：

```text
浏览器 -> Web(Nginx) -> BFF(FastAPI) -> Core HTTP(C++ Core + CPU policy)
```

训练仍在服务器进行。本地 Docker 不需要下载：

- 中间 checkpoint；
- `runs/`；
- 训练 collector/PPO 依赖；
- CUDA 运行时。

本地推理只挂载：

```text
models/v3/policy.pt
```

## 二、本次遇到的坑

### 1. Docker Desktop 已安装，但 `docker` 不在 PowerShell PATH

截图中的 Docker Desktop Engine 是运行的，但普通 PowerShell 中执行 `docker` 找不到命令。
实际可用的 Docker CLI 位于：

```powershell
$dockerCli = 'C:\Users\Ruikai Chen\AppData\Local\Programs\DockerDesktop\resources\bin\docker.exe'
& $dockerCli version
```

长期解决方式是把该目录加入用户 PATH；临时解决方式是在当前 PowerShell 中使用上面的完整路径。

### 2. 不能从 `docker-desktop` 内部 WSL 发行版调用 Docker CLI

`docker-desktop` 是 Docker Desktop 的内部发行版，不是用户工作环境。
从里面执行 Docker CLI 会得到不支持或连接异常。应当在 Windows PowerShell 中调用 Docker Desktop 自带的
`docker.exe`，并让它连接 `desktop-linux` context。

检查：

```powershell
wsl.exe -l -v
& $dockerCli context show
& $dockerCli version
```

### 3. Docker Hub 认证服务多次超时或 EOF

构建初期出现过：

```text
failed to fetch oauth token
auth.docker.io ... connect timeout
auth.docker.io ... EOF
```

这不是项目代码错误，而是 Docker Hub registry 认证链路不稳定。处理顺序：

1. 确认 Docker Desktop Engine 正常运行；
2. 重试 `docker pull`；
3. 让已下载的 layer 复用缓存；
4. 如果仍失败，检查 Docker Desktop 代理、网络或 Docker Hub 登录状态。

本次最终成功下载了：

```text
python:3.10-slim-bookworm
Digest: sha256:68d914ec641a0b69267ce65184d000a2bc3a9ee2590ab702b82250ab2385735a
```

### 4. 初始 Dockerfile 使用了 Python 3.11，和服务器环境不一致

之前为了复用本机缓存，Dockerfile 临时使用了 `python:3.11-slim`，实际运行版本为：

```text
Python 3.11.16
Torch 2.14.0+cpu
NumPy 2.4.6
```

这不符合服务器 `crk` 环境给出的基线。之后已改成：

```text
Python 3.10 系列
Torch 2.6.0+cpu
NumPy 1.26.4
FastAPI 0.141.1
Uvicorn 0.52.3
Pydantic 2.13.4
```

服务器中的 `2.6.0+cu124` 是 CUDA wheel，不能直接装进 CPU Docker；本地应使用同版本线的
`2.6.0+cpu`。

### 5. Python 3.10 Bullseye 的 Debian Security 源过期

第一次切换到 `python:3.10-slim-bullseye` 后，Core 构建失败：

```text
Release file ... bullseye-security/InRelease is expired
```

没有继续绕过安全校验，而是改用同为 Python 3.10 系列的：

```text
python:3.10-slim-bookworm
```

这是更稳妥的处理方式。

### 6. Debian HTTP 镜像返回 502

Bookworm 初次安装 C/C++ 编译依赖时出现：

```text
502 Bad Gateway
```

Dockerfile 已做两项处理：

- 将 Debian 源从 HTTP 改为 HTTPS；
- `apt-get update/install` 增加 `Acquire::Retries=5`。

改完后 CMake、G++、Ninja 和 `libgomp1` 均安装成功。

### 7. PyPI/PyTorch 下载会出现 SSL EOF 或读超时

安装 Python 包时出现过 `SSLEOFError` 和 `ReadTimeoutError`，但重试后成功。Torch 最终安装结果为：

```text
torch-2.6.0+cpu
```

首次构建 Core 会比较慢，因为 CPU Torch wheel 约 178 MB，且 Core builder 还要下载 C/C++ 编译依赖。

### 8. Core 编译工具链不是 `gwent-build` 的精确复刻

本次 Docker Core builder 使用 Bookworm apt 工具链，实际主要版本是：

```text
GCC/G++ 12.2.0
CMake 3.25.1
Ninja 1.11.1
```

这与服务器 `gwent-build` 中的 Conda GCC 11.2.0、CMake 3.27.9、Ninja 1.13.2 不完全相同。
本次 C++ Core 已成功编译，说明当前 ABI/运行需求可用；但如果未来要求二进制完全可复现，需另建专门的
`gwent-build` 工具链镜像，不能把当前 Docker 说成完全一致。

### 9. 前端缺少 Vite 类型声明

前端首次构建报错：

```text
TS2882 Cannot find module or type declarations for side-effect import './styles.css'
```

已增加：

```text
apps/web/frontend/src/vite-env.d.ts
```

内容为 Vite 类型引用，之后 Web 镜像构建成功。

### 10. `installed.json` 缺失不是模型缺失

Core 启动时提示：

```text
no optional model provenance record: /app/models/v3/installed.json
```

这是可选的模型来源和哈希记录，不是推理前置条件。只要下面文件存在且非空即可：

```text
models/v3/policy.pt
```

### 11. Teacher 默认不启动

Teacher 服务使用 Compose profile：

```text
--profile teacher
```

正常单机对战默认只启动 Core、BFF、Web；Teacher 镜像可以构建，但不启用解释面板时无需启动。

### 12. 权限审批可能阻止 `up -d`

本次新版镜像已经构建成功，但最后替换旧容器时，Docker 权限审批连续超时，导致 `up -d` 没有实际执行。
因此不能把“镜像构建成功”误报成“新版本容器已经运行”。当权限可用时，需要重新执行启动命令。

## 三、成功流程

### 1. 检查 Docker Desktop

确认 Docker Desktop 已启动，并使用 Linux containers / WSL2 backend。

```powershell
$dockerCli = 'C:\Users\Ruikai Chen\AppData\Local\Programs\DockerDesktop\resources\bin\docker.exe'
& $dockerCli version
& $dockerCli context show
```

预期：Server 能返回 Docker Engine 版本，context 为 `desktop-linux`。

### 2. 检查模型

在仓库根目录确认：

```powershell
Test-Path -LiteralPath 'models/v3/policy.pt'
((Get-Item -LiteralPath 'models/v3/policy.pt').Length -gt 0)
```

不需要把 checkpoint、`runs/` 或训练服务器目录复制到本地。

### 3. 下载 Python 3.10 基础镜像

```powershell
& $dockerCli pull python:3.10-slim-bookworm
```

成功时应看到：

```text
Status: Downloaded newer image for python:3.10-slim-bookworm
```

### 4. 检查 Compose 配置

本次为了不覆盖用户配置，直接使用模板文件：

```powershell
& $dockerCli compose `
  --env-file deploy/docker/.env.example `
  -f deploy/docker/compose.cpu.yml `
  config --quiet
```

日常使用可以先复制配置：

```powershell
Copy-Item deploy/docker/.env.example deploy/docker/.env
```

之后把命令中的 `--env-file deploy/docker/.env.example` 换成 `--env-file deploy/docker/.env`。

### 5. 构建镜像

```powershell
& $dockerCli compose `
  --env-file deploy/docker/.env.example `
  -f deploy/docker/compose.cpu.yml `
  build core bff teacher web
```

Core 构建完成的关键证据应包括：

```text
Successfully installed ... numpy-1.26.4 ... pydantic-2.13.4 ... fastapi-0.141.1 ... uvicorn-0.52.3
Successfully installed ... torch-2.6.0+cpu
Image gwent-local-core Built
Image gwent-local-bff Built
Image gwent-local-teacher Built
Image gwent-local-web Built
```

### 6. 启动默认单机对战服务

```powershell
& $dockerCli compose `
  --env-file deploy/docker/.env.example `
  -f deploy/docker/compose.cpu.yml `
  up -d
```

### 7. 验证容器状态和版本

```powershell
& $dockerCli compose `
  --env-file deploy/docker/.env.example `
  -f deploy/docker/compose.cpu.yml `
  ps

& $dockerCli compose `
  --env-file deploy/docker/.env.example `
  -f deploy/docker/compose.cpu.yml `
  exec -T core python -c "import sys, torch, numpy; print('PYTHON='+sys.version.split()[0]); print('TORCH='+torch.__version__); print('NUMPY='+numpy.__version__)"
```

新容器预期版本：

```text
PYTHON=3.10.x
TORCH=2.6.0+cpu
NUMPY=1.26.4
```

### 8. 验证服务入口

```powershell
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8080/
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8080/api/health
```

预期：Web 返回 HTTP 200，API 返回 `ok: true`，并显示 `schema_version: 13`、`device: cpu` 和模型路径。

### 9. 打开游戏

浏览器打开：

```text
http://127.0.0.1:8080
```

### 10. 可选启动 Teacher

```powershell
& $dockerCli compose `
  --env-file deploy/docker/.env.example `
  -f deploy/docker/compose.cpu.yml `
  --profile teacher up -d --build
```

Teacher 只负责解释已经执行的动作，不参与合法动作决定或游戏状态推进。

## 四、本次结果

已完成并确认：

- Docker Desktop Engine 可用；
- Python 3.10 Bookworm 基础镜像下载成功；
- Web 镜像构建成功；
- BFF 镜像按指定依赖版本构建成功；
- Teacher 镜像按指定依赖版本构建成功；
- Core C++ shared library 编译成功；
- Core 镜像按 Python 3.10、Torch 2.6.0 CPU、NumPy 1.26.4 构建成功；
- 模型只需要 `policy.pt`，checkpoint 不需要。

后续已完成 clean Compose 重建与运行态复核：Core、BFF、Teacher、Web 均 healthy；
浏览器入口为 `http://127.0.0.1:8080`。M4 自动验收还确认了《盖尔》连续选择、
领袖目标、旧 revision 的 409 和 Teacher 故障隔离。因此下一次只需要从“启动默认
单机对战服务”开始，不需要重新下载或修改模型。

## 五、常用停止命令

```powershell
& $dockerCli compose `
  --env-file deploy/docker/.env.example `
  -f deploy/docker/compose.cpu.yml `
  down
```

该命令不会删除宿主机的 `models/v3/policy.pt`。
