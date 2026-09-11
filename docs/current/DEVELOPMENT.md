# Development and Runtime

## 1. 快速验证

```bash
python scripts/check.py quick
```

该入口用于源码卫生检查，默认要求仓库不携带产品模型二进制。已经安装
`models/v3/policy.pt` 的本地 Docker 工作区，应额外使用 `LOCAL_DOCKER.md` 的模型、schema 和
healthcheck 验收；不要把该模型存在误判为模型不兼容。

完整测试入口：

```bash
python scripts/check.py test
```

## 2. C++ 构建

```bash
cmake -S . -B build-release \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DGWENT_BUILD_TESTS=ON

cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

不同机器可以指定自己的 C/C++ compiler；仓库不依赖固定服务器路径。

## 3. Python 环境

从仓库根目录运行 Python 工具时：

```bash
export PYTHONPATH=$PWD/python/src:$PYTHONPATH
```

测试：

```bash
python -m pytest python/tests
```

## 4. V3 模型

最终模型运行槽位：

```text
models/v3/policy.pt
```

可使用：

```bash
python scripts/install_model.py /path/to/gwent_v3_final.pt
```

一个 checkpoint 同时包含 Deck A / Deck B。

## 5. Training

训练入口示例：

```bash
python -m gwent_rl.train_ppo \
  --config configs/training/ppo_ab_joint_v3_50k.yaml \
  --library build-release/libgwent_core.so
```

正式任务优先使用 `training/tasks/` 与 orchestrator；具体参数以对应 YAML 为准。

## 6. Teacher

CLI：

```bash
PYTHONPATH=. python -m services.teacher.cli \
  services/teacher/examples/decision_packet.json \
  --level beginner
```

HTTP：

```bash
python -m pip install -r services/teacher/requirements.txt
uvicorn services.teacher.api:app --host 127.0.0.1 --port 8020
```

## 7. Web

Web 由 Core/Strategy HTTP service、Teacher service、FastAPI BFF 和 React 组成。模块启动说明见 [`../../apps/web/README.md`](../../apps/web/README.md)。
