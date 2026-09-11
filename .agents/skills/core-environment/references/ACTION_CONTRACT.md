# Action Contract Changes

## 事实来源与顺序决策

`include/gwent/engine/action.hpp`、`src/engine/legal_actions.cpp` 和 event kernel 是动作语义的事实来源。
当前玩家出牌使用候选式顺序 grammar，而不是一次枚举笛卡尔积：

```text
PLAY_CARD(source) -> CHOOSE_ROW(row) -> CHOOSE_INSERT_POSITION(position)
```

后续 deploy target 继续作为 nested pending choice。修改任一阶段时，检查
`PendingChoice`、`ResolutionFrame`、suspended task tail、choice budget、decision prefix 和
resume 路径；不要只修改 `legal_actions`。

## 动态插入位置不变量

- `insert_position` 是动作执行瞬间相对于当前 row vector 的插入索引，不是固定棋盘格，也不是单位身份。
- row 有 `n` 张牌时合法位置严格为 `0..n`；移除或移动后按新 vector 长度重新生成。
- fully specified 玩家 `PlayCard` 必须显式携带合法位置，缺失或越界返回 `InvalidTarget`，不得 clamp 或默认 append。
- 非玩家 summon/spawn/play-from-deck 没有位置选择时，调用方显式传 `row.size()` 保持 append；不要给玩家 API 加默认参数。
- Action equality、stable hash、trace/JSON/debug identity 都必须包含新增动作参数。

## 边界同步清单

动作字段或候选类型变化时逐层检查：

1. C++ Action/Task、validation、kernel/reducer、legal actions。
2. pending choice、continuation、prefix、invariant、trace/checksum。
3. `include/gwent/c/core.h` 与 `src/c/core_c.cpp` 的 observation/batch storage、copy/export 和 sentinel。
4. Python `_ctypes.py`、collector、rollout buffer、policy input/embedding、explain/schema names。
5. C API、RL C API、collector 与 decision-packet round trip 测试。

新增 decision/option grammar 是 Action Grammar breaking change。新增被模型消费的 observation/batch字段也是
Observation schema breaking change；两者应分别判断并按 `SCHEMA_CONTRACT.md` 更新，不能只 bump grammar。

## 最低回归面

除专项语义测试外，至少覆盖：core model、legal actions、pending decision、kernel/reducer、trace snapshot、
C API smoke、RL C API、RL collector C API 和 Python ctypes/decision packet。权威结果只来自 AGENTS.md
指定的 Linux server/build环境。

## 只读分支 / clone 不变量

产品的 Teacher 或分析工具可以在 clone 环境中执行反事实分支，但 clone 不是浅复制的 synonym。
`PendingChoice` 的 `ResolutionFrame` 保存可变的 decision prefix、trigger stack、预算和 root rollback snapshot；
它在同一真实对局的 staged choice 内可以共享，在不同环境实例之间绝不能共享。

当修改 `gwent_rl_env_clone`、snapshot 或 pending-choice copy 路径时：

1. 分支必须深拷贝全部可变 `ResolutionFrame` 状态与回滚快照；
2. 只读分支每一步只能执行自己的 Core handle；
3. 回归至少连续运行多次 clone 分支，覆盖 card/row/insert 等顺序选择；
4. 验证真实环境的 checksum、legal actions、prefix 和后续真实 choice 均不变；
5. 若分支导致真实局面的 `PREFIX_OVERFLOW`、预算变化或 stale decision，优先修复 clone 隔离，不能以扩大 RL 容量掩盖问题。
