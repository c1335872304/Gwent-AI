---
name: training-config
description: 配置、验证和运行 Gwent Python/RL 训练时使用；覆盖 PPO/collector/reward、算法 config、正式 Training Task、warm-start、resume、checkpoint compatibility、evaluation、model promotion 和训练回归分层排查。
---

# Training Config

把“想训练什么”转换成可验证、可恢复、可比较的 Training Task。Trainer 消费 Core contract，不通过调参掩盖环境或 collector 的能力缺口。

## Authoritative sources

先读 `references/TRAINING_CONFIG.md`。配置分两层：

- `configs/training/*.yaml`：算法与实验参数；
- `training/tasks/*.yaml`：正式运行的预算、初始化、runtime、checkpoint、evaluation、run_dir、resume policy。

环境 schema / action grammar 由 `$core-environment` 定义；checkpoint metadata 只记录并验证该 contract。

## Workflow

1. **Classify**：明确用户是在做短实验、正式 Training Task、恢复训练、warm-start、评估还是 promotion。
2. **Check capability first**：确认 Core legal decision → C ABI → collector → rollout 已暴露所需能力；能力缺口不能靠 reward 或 learning rate 修复。
3. **Choose a baseline**：从最接近的 config/task 复制，只改当前假设相关字段。
4. **Preflight**：用 `scripts/validate_training.py` 检查 task/config；涉及 checkpoint 时显式查看 metadata 和 source contract。
5. **Smoke before scale**：先短预算/最小环境跑通，再扩大 games/updates/并行度。
6. **Evaluate**：按完整 episode 统计，检查 matchup、swap-sides、illegal results、completed games，避免 partial result 被当作 promotion 证据。
7. **Promote deliberately**：长期 source 放 `artifacts/`，run 输出放 `runs/`，最终模型通过 repository install entrypoint `./scripts/install_model.py` 安装到产品 slot。
8. **Verify**：运行 `python .agents/skills/training-config/scripts/verify.py`；需要 collector/PPO smoke 时追加 `--smoke`。

## Decision rules

- **resume**：训练状态、optimizer、scheduler、contract 都兼容，继续同一次 run。
- **warm-start**：只迁移允许复用的模型权重，source/target contract 和 migration 必须显式记录。
- **incompatible**：不能用 `strict=False`、静默裁剪或伪 metadata 强行加载。
- **regression triage**：先检查 observation/action mapping、reward actor/side/sign/timing、terminal/done、GAE/return、evaluation protocol，最后才调超参数。
- **multi-deck**：优先保持 reward 通用；单侧适配用 learner-vs-frozen ownership，而不是卡组专属 reward。

## Invariants

- Trainer 不修改或复制游戏规则、legal action。
- 不用 learning rate/reward 掩盖 Core、C ABI 或 collector bug。
- 不静默绕过 checkpoint incompatibility。
- 不把 partial evaluation 当作 promotion 证据。
- 服务器训练环境不是 Web runtime 依赖。
- “训练能跑”不等于“策略已经学会”；能力接通和行为学习要分开验证。

## Verification

验证单个 task/config：

```bash
python .agents/skills/training-config/scripts/validate_training.py <path>
```

验证全部定义：

```bash
python .agents/skills/training-config/scripts/verify.py
```

涉及 collector/PPO runtime：

```bash
python .agents/skills/training-config/scripts/verify.py --smoke
```

最终回复必须给出：baseline、改动假设、source/target contract、初始化模式、验证结果和 promotion 结论。

## Handoff

- legal decision 在 rollout 不可达：交给 Core 检查 Decision/C ABI；Trainer 负责 collector/policy 消费。
- Observation / Action Grammar breaking change：读取 Core contract，再决定 migration。
- 产品加载最终模型：Trainer 只提供已安装模型与 metadata；Product 不读取 `runs/`。
- Teacher 解释 metadata：只提供允许公开的 policy/value 信息，不把 Teacher 接进 reward/update 链。
