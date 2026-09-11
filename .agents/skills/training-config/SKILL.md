---
name: training-config
description: 配置、验证和启动 Gwent Python/RL 训练；区分算法 config、正式 Training Task、warm-start、resume、evaluation 与 model promotion。
---

# Training Config

这个 Skill 负责把“想训练什么”变成**可验证、可恢复、可比较**的训练任务。

## 配置分层

- `configs/training/*.yaml`：算法与实验参数；
- `training/tasks/*.yaml`：预算、初始化、runtime、checkpoint、evaluation、run_dir、resume policy。

短实验可以直接使用算法 config；正式长跑使用 Training Task。

## Workflow

1. 阅读 `references/TRAINING_CONFIG.md`。
2. 找最接近目标的 config/task 作为基线，只改变与当前假设有关的字段。
3. 先确认 Core 已经暴露所需 decision/action；训练不能弥补 capability gap。
4. 用 `scripts/validate_training.py` 验证定义。
5. 先 smoke，再扩大预算。
6. checkpoint 与日志写入 `runs/`；长期 pinned source 放 `artifacts/`；产品模型放 `models/v3/policy.pt`。
   使用 `scripts/install_model.py` promotion/安装时，同时保留 `models/v3/installed.json` 的来源文件名、hash 和
   contract metadata；它不是推理硬依赖，但缺失时产品模型的来源不可审计。
7. 区分 warm-start 与 resume；跨 contract checkpoint 必须走显式 migration policy。
8. 评估按完整 episode 计数，并检查 matchup、swap-sides、illegal results 与 completed games。
9. 多卡组训练优先保持 reward 通用；需要单侧适配时使用 learner-vs-frozen ownership，而不是卡组专属 reward。
10. promotion 后用 `scripts/install_model.py` 安装最终模型，不把训练目录直接当产品依赖。

## Invariants

- Trainer 不修改游戏规则；
- 不用 learning rate/reward 掩盖 Core 或 collector bug；
- 不用 `strict=False`、伪 metadata 或静默裁剪绕过 checkpoint incompatibility；
- 不把 partial evaluation 当作 promotion 证据；
- 服务器训练环境不是 Web runtime 依赖。

## Contract Boundary

当前 environment contract 由 `$core-environment` 定义。Trainer 只负责：

- 验证 task 与 runtime contract 是否兼容；
- 判断 checkpoint 是 resume、warm-start/migration 还是 incompatible；
- 保存 source/target metadata 与运行证据。

涉及 schema/action grammar 时，同时读取 `$core-environment` 的 `SCHEMA_CONTRACT.md`。
