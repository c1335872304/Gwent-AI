# 昆特牌 AI 本地产品：逻辑可靠性优化方案

**状态：M0–M3 已实施；M4 自动验收已通过，浏览器手工验收待执行。**  
**依据：** [项目当前基线](PROJECT_BASELINE.md)、当前 Core/BFF/Teacher 实现与已存在的 `PREFIX_OVERFLOW` clone 回归。  
**目标：** 不增加卡牌、训练能力或玩法功能；将“真实对局、AI 实际决策、AI 教师反事实推演”之间的状态边界、动作语义与故障行为变成可证明的 contract。

## 1. 结论与优先级

当前版本的 Schema、Action Grammar、模型 metadata 与本地运行时一致，因此**不需要为本方案重新训练或修改模型**。需要优化的是产品运行时的状态管理逻辑：

1. 将 AI 教师的“模拟一个回合”定义为**一次根行动及其强制后续选择**，而不是“直到行动方交出控制权”；
2. 给单局对战引入权威的 `match_id + revision`，使 step、预演、页面结果都能识别过期局面；
3. 将真实对局与预演分支的隔离，从一次已修复的 `ResolutionFrame` 问题提升为通用不变量；
4. 对预演、真实 step、Teacher 服务故障分级处理，并保留可诊断但不泄露私有信息的 trace；
5. 消除同一局面被 React 重渲染、切换教师难度或并发请求重复预演的浪费。

实施顺序：**行动链语义 → revision/并发控制 → branch isolation 回归矩阵 → 缓存与错误诊断 → 前端文案与验收**。

## 2. 真实现状与问题定位

| 现有事实 | 位置 | 逻辑风险 |
|---|---|---|
| AI 实战动作保存在 `last_ai_actions` | `tools/server/human_vs_ai.py` | 语义正确：它必须只表示 AI 玩家真实执行的历史。 |
| 教师曾调用 `/preview/current-human-turn` 并循环到 actor 不再是人类 | 历史 `preview_current_human_turn()` | 曾把人类连续控制阶段误当成一个教学回合，可能包含两次自愿行动。M1 已改为根行动链截断。 |
| 预演曾为扁平 `steps` | 历史 `counterfactual-turn-v1` | M1 已迁移到有 `root_action` / `required_choice` 父子关系的 v2 trace。 |
| clone 已深拷贝 pending `ResolutionFrame` | `src/c/core_c.cpp` | 修复了已知污染，但尚未形成覆盖所有 pending choice 类型的通用回归矩阵。 |
| GameService 只是 HTTP 转发 | 历史实现 | 现已由 Core match/revision 和 BFF preview cache/single-flight 补齐过期语义。 |
| TeacherPanel 自行拼接浏览器侧 signature | 历史实现 | 已替换为权威 `match_id/revision`，旧响应不会渲染。 |

## 3. 目标语义：一次“教学行动链”

为消除“为什么 AI 打了两张牌”的歧义，默认定义如下：

```text
一次教学行动链
  = 一个模型选出的根行动
  + 该根行动强制产生的 target / row / insert position / choose_card 等后续选择
  + 该链的规则结算
  ≠ 同一行动方之后可以自由选择的第二张牌、第二次领袖能力或 pass
```

例如：

```text
根行动：use_leader《腥膻之味》
  └─ 强制选择：目标 = 敌方《暗影长者》
  └─ 结算完成
停止；不继续模拟“随后是否再打出《暗影长者》”。
```

若根行动是 `play_card`，其排、位置、目标等属于同一链；链完成后立即停止。若将来确实需要“整段连续控制阶段”的战略演示，应使用另一个明确命名的模式，不能复用本接口的“一个回合”含义。

### 3.1 新 trace contract（产品 contract，不是 RL schema）

已将 `counterfactual-turn-v1` 迁移到 `counterfactual-action-chain-v2`。该变更只影响 Core HTTP → BFF → Teacher → React 的产品数据，不改变 observation、Action Grammar、reward 或 checkpoint。

最小结构：

```json
{
  "schema_version": "counterfactual-action-chain-v2",
  "base_state_signature": "64-char-sha256",
  "controlled_player": 0,
  "boundary": "one_root_action_with_required_choices",
  "root": { "decision_serial": 1, "kind": "use_leader" },
  "steps": [
    { "decision_serial": 1, "parent_decision_serial": null, "role": "root_action" },
    { "decision_serial": 2, "parent_decision_serial": 1, "role": "required_choice" }
  ],
  "status": "complete",
  "stopped_reason": "action_chain_resolved"
}
```

`kind`、来源、目标、排与位置仍使用 Core 的结构化字段；禁止由 BFF、Teacher 或 React 解析中文 label 推断父子关系。

## 4. 工作包 A：Core 的分支与行动链 contract

**Owner：Core。涉及 Product/Teacher 的 contract handoff。**

### A1. 明确定义 clone 的不变量

保留 `gwent_rl_env_clone`，但把以下规则写入 C API regression 与代码注释：

- clone 后任意 `step_option` 只能改变 clone；
- live handle 的 observation、合法 actions、pending choice、decision prefix、预算、checksum/fingerprint 均不变；
- clone 和 live 不共享可变 `ResolutionFrame`、root rollback snapshot 或之后新增的可变 continuation；
- clone 销毁不影响 live handle。

不通过把 prefix 从 16 增至 64 来掩盖 branch 污染。prefix 容量仍是现有模型 observation contract；只有确证真实合法连续 prefix 本身超过容量时，才由 Core + Trainer 单独评估 schema 与模型兼容性。

### A2. 由 Core 产生行动链边界

在 Strategy/Core adapter 中建立显式的 action-chain collector，而不是按“actor 是否变化”判断预演结束：

1. 模型在 clone 的初始合法 actions 中选择一个根 `option_index`；
2. 记录根 action 的稳定标识；
3. 只继续执行该 root resolution 强制产生的 pending choices；
4. resolution 完成、回到自由 `turn` decision 或局面结束时停止；
5. 将每个后续选择与 root 建立 `parent_decision_serial`，输出 `role`；
6. 不执行新的自由 `play_card`、`use_leader`、`pass`、`end_turn` 根行动。

这里需要 Core 暴露权威的“当前 pending choice 是否仍属于同一 resolution”的结构化事实。若现有 `ResolutionFrame` 已能提供该事实，adapter 只消费它；若不能，优先在 Core C API 增加**仅用于 trace 的结构化字段**，不向 RL observation 塞入产品语义。

### A3. Core 测试矩阵

在 `tests/unit/rl_c_api_tests.c` 与最接近的 Core HTTP/adapter 测试中覆盖：

| 场景 | 必须证明 |
|---|---|
| 普通打牌，无后续选择 | 只有一个 `root_action`，停止原因正确。 |
| 打牌 → 选排 → 选位置 | 全部是一个 root 的 required choices，不出现第二张主动出牌。 |
| 领袖 → 选敌方单位 | target 作为 leader 的子步骤展示，且目标结构化字段正确。 |
| 多层 choose_card / target | parent chain 连续，live fingerprint 与合法 actions 不变。 |
| 连续调用预演 | 每次结果可重复；真实环境不会增长 decision prefix 或触发 `PREFIX_OVERFLOW`。 |
| clone 释放 | live handle 继续可 step，且没有悬空/共享可变状态。 |

## 5. 工作包 B：单局 revision 与并发语义

**Owner：Core；Product 负责 HTTP、UI 和错误呈现。**

### B1. 权威局面标识

在 Core server 的单局状态引入：

- `match_id`：每次 `/new` 创建新的随机标识；
- `revision`：每次成功改变真实局面的 `/step` 后单调递增；新局从 0 开始；
- `GameState` 返回两者；预演 trace 返回其 `base_match_id` 与 `base_revision`。

不要用浏览器拼接对象列表作为权威 revision。浏览器可保留本地 signature 仅供渲染优化，但不得用于判断预演正确性。

### B2. 命令的乐观并发控制

前端提交 `/step` 时携带最后看到的 `match_id`、`expected_revision`。Core 在同一个 match mutex 中检查并执行：

```text
match_id 不同 / revision 不同 → HTTP 409 stale_state
revision 相同且 option 合法       → 执行，revision + 1，返回新 GameState
revision 相同但 option 非法       → HTTP 422 invalid_action
```

先以可选字段兼容旧调用方，待 BFF、前端和测试全部迁移后再设为必填；该时点按 [CORE_API_CONTRACT.md](../../apps/web/docs/CORE_API_CONTRACT.md) 判断是否升级 Product `api_version`。

### B3. 预演与 Teacher 的时序

1. 当前实现让 Core RLock 覆盖 live clone 与 preview 计算，保证真实 step 和预演严格串行；若以后要缩短锁持有时间，必须先提供独立 clone snapshot 的并发证明；
2. BFF 用 `(match_id, base_revision, trace_schema)` 缓存一次 Core branch trace，并使用 single-flight 合并相同请求；
3. Teacher 的文本缓存键额外包含 `level`；缓存只保留服务端，不向浏览器暴露 trace 中未获准公开的字段；
4. React 收到结果时，只有 `base_match_id/base_revision` 与当前 `GameState` 一致才渲染，否则丢弃并等待当前局面的新请求；
5. 任何真实 `/step` 或 `/new` 均失效该局面的 preview/Teacher cache。

这样 React Strict Mode、切换解释级别与慢速 Teacher 不会使旧局面说明覆盖新局面，也不会反复运行模型预演。

## 6. 工作包 C：故障分级与可诊断性

**Owner：Core 定义错误；Product 做转换与展示；Teacher 保持 optional。**

| 类别 | 示例 | 用户可见行为 | 内部处理 |
|---|---|---|---|
| `stale_state` | 双击、旧页面、并发预演后点击 | 提示“局面已更新”，刷新 state；不显示为 Core 崩溃 | 记录 match/revision，不记录隐藏手牌。 |
| `invalid_action` | 当前 option 已不合法 | 重新加载合法 actions | 保留 option index、revision、public summary。 |
| `preview_unavailable` | clone/模型/Teacher 超时 | 教师面板降级；真实游戏继续 | 带 correlation id，保留服务器 trace。 |
| `core_invariant_failed` | prefix、预算、clone 污染等 | 明确“本局规则引擎异常，可重新开局”；不伪装成普通 500 | 保留受限诊断包，优先新增回归。 |

诊断包必须只存在 Core/BFF 日志或本地受限调试导出中，浏览器只得到 correlation id 与安全错误码；不得包含 AI 私有手牌、未执行候选或 oracle observation。

## 7. 工作包 D：产品呈现的最小调整

**Owner：Product；Teacher 负责解释卡片的 grounded 表述。**

- `AI 决策过程` 继续只显示 `last_ai_actions`，明确为“AI 玩家实际行动”；
- `AI 教师` 标题/说明改为“当前局面的 AI 单次行动推演”；
- 一个 root action 显示为卡片标题，子选择折叠在其下，目标信息应写成“目标：敌方《暗影长者》”，不能只显示 `use_leader`；
- 显示 `action_chain_resolved`，不要将正常停止表述成“模型没有继续思考”；
- stale preview 静默丢弃并重新请求；只有连续失败才展示可读错误与 correlation id；
- 不将完整 branch trace 写进 `last_ai_actions`、浏览器历史或 URL。

这不是新增玩法：它只纠正已有数据的命名、分组和生命周期。

## 8. 版本、模型与训练影响

| 项目 | 是否改变 | 说明 |
|---|---|---|
| 游戏规则 / 卡牌效果 | 否 | Core 规则语义不变。 |
| RL Observation Schema | 否 | 不向 observation 加入产品 trace/revision。 |
| Action Grammar / Reward ABI | 否 | 合法动作和 reward 不变。 |
| 已安装 `policy.pt` | 否 | 不重训、不迁移 checkpoint。 |
| C ABI | 可能新增 trace/fingerprint 读取字段 | 若新增，只扩展产品诊断/trace contract；需 C/Python ctypes 同步与 ABI 测试。 |
| Core HTTP / BFF / React | 是 | 增加 revision、action-chain-v2 与 stale/error contract。 |
| Teacher input | 是 | 由 v1 扁平 turn trace 迁移到 v2 树状 action-chain evidence。 |

Trainer 只需要验证现有模型在改造后的 Core adapter 中仍能完成 inference smoke；不启动本地或学校服务器训练。`installed.json` 若缺失，可在模型来源可确定后补写，以提升排障可追溯性。

## 9. 实施里程碑与验收门槛

### M0：contract 设计评审

- 已确认本文件第 3 节的默认边界：一个根行动加其 Core-required choices；
- 已在 `apps/web/docs/CORE_API_CONTRACT.md` 写出 v2 payload；
- v2 是 Core HTTP/BFF/Teacher 的同步产品 contract 变更，不改 RL contract，也不需 Product `api_version` bump；
- v1 不再接受为当前本地链路输入，避免同一部署内同时存在两种“回合”语义。

### M1：Core branch isolation 与 action-chain

- 已实现根行动/强制选择边界：Core adapter 在自由 `turn` 返回时停止，不再选择第二个自愿动作；
- 已为 v2 trace 增加 `root`、`role` 和 `parent_decision_serial`，由 BFF strict models 与 Teacher 共同校验；
- 已补 adapter regression，覆盖 root → required choice → free turn 的截断；既有 C API clone regression 持续覆盖重复 clone 的 row/insert 分支；
- 本地当前链路只运行 v2；clone 不变量仍以真实 live checksum、legal action、prefix 和后续 choice 为验收事实。
- 已完成本地 Compose smoke：Core 返回 v2、一个 `root_action` 加 Core-required choices，并以 `action_chain_resolved` 停止；BFF/Teacher 成功响应，预演前后真实 state 与 `last_ai_actions` 相同。

### M2：revision 与 BFF contract（已实现并完成容器回归）

- Core 的单局状态已带随机 `match_id` 和 `revision`；每次成功真实 step
  在原子执行中递增 revision；`/new` 创建新 id 并从 0 开始；
- `/step` 与 preview 接受 `match_id + expected_revision`，过期命令为结构化
  `409 stale_state`，当前 state 上非法 option 为 `422 invalid_action`；
- BFF strict models 已验证状态/trace 的版本字段；Teacher preview service
  以 Core trace key 与 level key 缓存，并为相同请求 single-flight；`/new`、
  `/step` 会失效旧 cache；
- 回归覆盖 direct stale command、BFF request forwarding 和 concurrent preview
  single-flight/invalidation。Core 的现有 RLock 保持真实 step 与 clone preview
  串行，不改变游戏规则或 RL contract。

### M3：Teacher 与 React（已实现并完成容器回归）

- Teacher 继续只消费 Core v2 root/child evidence；BFF response 额外携带
  `base_match_id/base_revision`，不向浏览器返回 branch trace；
- React 以权威 match/revision 替换对象拼接 signature；旧 response 静默丢弃，
  stale step 刷新当前 state 并显示可恢复提示；
- 教师面板把 root 显示为“指导动作”，并将其余步骤缩进为“完成此动作所需的
  Core 选择”，避免误读为两张主动出牌；
- 仍需执行 `npm run build`、Teacher/BFF tests，并手工覆盖盖尔、领袖目标、
  选排/位置、连续点击、新开局及 Teacher 不可用。

### M4：发布判定（自动验收已完成，浏览器手工项待执行）

2026-09-11 在 clean Docker Compose 上完成以下自动验收：

- 连续两次相同局面 Teacher 预演均成功，返回同一 `match_id/revision`，每次
  只有一个 `root_action`，本次样例包含 3 个 branch steps；真实状态在预演前后
  完全一致；
- 实际执行 Core 返回的 `play_card · 盖尔`，连续完成 Core 返回的
  `row_target → insert_position → card_target` 选择，没有 HTTP 500 或
  `PREFIX_OVERFLOW`，revision 按真实动作递增；
- 实际执行 `use_leader · 腥膻之味 → card_target`，目标对象、目标方和目标区
  来自 Core 结构化字段；
- 重用旧 revision 返回 `409 stale_state`，新开局生成新 `match_id` 且 revision
  重置为 0；human-vs-AI 公共 state 没有 P1 手牌对象，公开对象包含卡牌能力文本；
- 停止 Teacher 容器后，教师接口正确降级，但真实游戏 `/step` 仍成功；Teacher
  已重新启动并恢复 healthy。

以下全部成立才合入本地 Docker 默认链路：

1. 教师一次预演只包含一个根行动，不出现第二个自愿出牌/领袖动作；
2. leader/卡牌的后续目标、排、位置在 UI 中有父子关系和结构化目标描述；
3. 任意次数预演后，真实 `summary`、objects、actions、revision、decision prefix 和后续 step 均保持正确；
4. 旧 action/revision 必定得到可恢复的 409，而不是随机 HTTP 500；
5. Teacher 故障不影响真实游戏；（自动验收已通过）
6. 无 AI 隐藏手牌、未执行候选或 oracle 信息流向浏览器；
7. 现有模型 CPU 推理 smoke 通过，无 schema/model 迁移。（自动验收已通过）

尚未自动替代的部分是浏览器手工验收：打开 `http://127.0.0.1:8080`，由用户
点击盖尔、领袖目标、选排/插入位置、连续点击和新开局，确认视觉分组与实际
交互符合预期。完成这些点击后，M4 才能标记为全部完成。

## 10. 文件级改动地图

| 边界 | 主要文件 | 预期职责 |
|---|---|---|
| Core clone / action trace | `src/c/core_c.cpp`、`include/gwent/c/core.h`、`tools/server/human_vs_ai.py` | clone 不变量、行动链收集、结构化 trace、revision。 |
| Core tests | `tests/unit/rl_c_api_tests.c`、`tools/server/tests/test_human_vs_ai_modes.py` | branch isolation、边界、重复预演。 |
| Product contract | `apps/web/backend/app/models/core_contract.py`、`apps/web/docs/CORE_API_CONTRACT.md` | v2 trace、revision、错误类型。 |
| BFF orchestration | `apps/web/backend/app/services/game_service.py`、`apps/web/backend/app/api/teacher.py` | request serialization、cache、single-flight、Teacher 降级。 |
| React | `apps/web/frontend/src/components/TeacherPanel.tsx`、`AiPanel.tsx`、`types/game.ts` | revision 匹配、根/子步骤显示、实际 AI 与教师推演分开。 |
| Teacher | `services/teacher/` | 只将 v2 evidence 翻译为说明；不选择动作、不解析 label。 |
| Skills/docs | `.agents/skills/*`、`docs/current/*` | 合同稳定后更新不变量与验证命令。 |

## 11. 不采用的“修复”

- 不扩大 `decision_prefix` 上限来处理 clone 污染；
- 不让 Teacher 自己计算合法动作或选择目标；
- 不在 React 根据卡牌文字补父子关系；
- 不把完整预演动作写回 `last_ai_actions`；
- 不因为 HTTP trace 增加字段就升级 RL Schema 或重训模型；
- 不用静默吞掉 `PREFIX_OVERFLOW`、随机重试或截断 trace 来伪造成功。

## 12. 后续决策

本方案采用“一个根行动链”作为默认教师边界。若产品希望教师展示更长的战略计划，应另立 `strategic-sequence` contract，明确最大根行动数、停止条件、隐私边界和算力预算；不得改变本方案的单次教学行动链语义。
