# Training and V3 Model

## 1. RL Pipeline

```text
C++ Core
  -> parallel collector
  -> structured observation + legal candidates
  -> policy/value network
  -> PPO update
  -> checkpoint
  -> matchup evaluation
```

训练层不拥有规则；发现 illegal action、异常状态或规则回归时，应优先定位 Core/collector contract，而不是先调学习率。

## 2. Config 与 Task

- `configs/training/*.yaml`：模型、PPO、reward、batch 等算法参数；
- `training/tasks/*.yaml`：一次正式运行的预算、初始化、runtime、checkpoint 与 resume policy。

Training Task 是运行声明，不复制 PPO 实现。

## 3. Warm-start 与 Resume

两者语义不同：

- **warm-start**：只把旧 checkpoint 参数迁移到新运行，允许 schema/grammar migration；
- **resume**：继续同一个运行，恢复 update/game counter 和 optimizer state。

发生规则环境变化时，不应把旧 optimizer state 当作同一轨迹继续 resume。

## 4. V3 Shared / Private

一个 V3 checkpoint 包含：

```text
Shared representation
  ├─ Deck A private branch
  └─ Deck B private branch
```

每条实际决策路径约一半 Shared、一半 Private。Private 降低卡组间策略干扰；Shared 保留通用局面表示，因此 Joint 阶段仍可能存在共享梯度竞争。

## 5. Joint Training

Joint 训练同时更新 Shared、A Private 与 B Private。best gate 同时关注：

- joint score；
- per-deck safety floor；
- illegal result count。

best checkpoint 与 latest checkpoint 语义不同：latest 只是最后一次保存，best 是通过 gate 的候选。

## 6. 当前封盘结果

修复 Veil / Unseen Elder 交互后，以旧 best 作为初始化重新 Joint 训练 50,000 局，最终 best 为 u20：

| 指标 | 结果 |
|---|---:|
| Joint score | 55.2% |
| A score vs previous best | 50.9% |
| B score vs previous best | 59.5% |
| Illegal results | 0 |

最终产品只需要一个 checkpoint；`models/v3/policy.pt` 同时包含 A/B 分支。

## 7. Evaluation

评估至少区分：

- same-deck teacher comparison；
- A/B cross matchup；
- swap-sides 先后手对称评估；
- current-vs-best promotion evaluation。

不能仅凭单个短样本胜率解释卡组机制；规则正确性与策略强度应分别验证。
