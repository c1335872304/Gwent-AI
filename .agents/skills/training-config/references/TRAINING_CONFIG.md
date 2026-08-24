# Training Configuration Reference

## 1. Algorithm Config

`configs/training/*.yaml` 的字段真相来自 `python/src/gwent_rl/config.py`，主要包括：

- device / seed / run_dir；
- collector 并行度、batch、reward mode；
- model capacity 与 embedding；
- PPO learning rate、epochs、GAE、clip、entropy、KL；
- eval cadence、games、num_envs、deterministic。

## 2. Formal Training Task

`training/tasks/*.yaml` 的字段真相来自 `python/src/gwent_rl/training/task.py`：

- algorithm config reference；
- optional compatibility pin；
- initialization；
- total game budget；
- runtime；
- checkpoint/evaluation；
- execution/run_dir/resume policy。

Task 描述“这次运行是什么”，不复制 PPO 实现。

## 3. Output Ownership

```text
runs/       一次运行的 checkpoint / log / eval
artifacts/  需要长期固定引用的训练资产
models/v3/  产品最终推理模型
```

三者不要混用。

## 4. Experiment Discipline

一次实验尽量只改变一个主要假设。吞吐问题先查 runtime/collector；行为问题再查 reward/model/PPO；出现非法动作或规则异常时先回到 Core contract。

## 5. Multi-deck Sampling

N 套卡组存在 N² 个有方向 matchup。预算设计需要同时考虑：

- cross matchup；
- same-deck mirror；
- swap-sides；
- 最弱 matchup；
- 遗忘监测。

不要求天然克制关系被训练成 50:50。

## 6. Evaluation and Promotion

- `evaluation.games` 是完整 episode 总预算；
- evaluator 必须达到目标 completed games；
- matchup 必须显式指定 deck/side，避免默认配置产生错误对局；
- `illegal_result_count` 必须为 0；
- promotion 需要记录 candidate、baseline/best、per-deck score 与 gate；
- `latest.pt` 与 `best*.pt` 语义不同。

## 7. Warm-start vs Resume

- **resume**：继续同一 contract、同一 run，恢复 optimizer 与计数；
- **warm-start**：把经过审查的策略参数迁移到新 run；
- **migration**：source/target contract 不同，必须由 `training.migrations` 的显式 policy 管理。

准备外部 checkpoint 时先运行：

```bash
python scripts/prepare_warmstart_source.py /path/to/checkpoint.pt
```

工具应 fail-closed：身份、metadata 或 migration 不匹配时停止，而不是强行加载。
