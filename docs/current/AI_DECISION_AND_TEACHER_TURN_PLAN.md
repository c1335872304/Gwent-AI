# AI 决策过程与 AI 教师单次指导行动：设计与实施记录

> **状态：已实现。** 当前运行时基线见
> [`PROJECT_BASELINE.md`](PROJECT_BASELINE.md)。本文保留原始设计、分阶段方案和验收意图，方便追溯；
> 其中“建议”“规划”“当前问题”等措辞描述的是实施前状态，不再表示当前源码。

> **当前使用方式：** 阅读当前字段和请求方式时，以
> [`apps/web/docs/CORE_API_CONTRACT.md`](../../apps/web/docs/CORE_API_CONTRACT.md)、
> [`TEACHER_AND_WEB.md`](TEACHER_AND_WEB.md) 和
> [`LOGIC_OPTIMIZATION_PLAN.md`](LOGIC_OPTIMIZATION_PLAN.md) 为准。当前教师边界是
> “一个根行动及其 Core-required choices”，不是完整对局、下一回合或整场比赛。

## 1. 文档目的

本文记录人类 vs AI 对局中两个容易混淆的产品模块如何分工、如何修改数据流，以及如何验证单次指导行动链。

本规划只讨论产品运行时，不修改训练流程、不要求下载 checkpoint、不把完整对局推演到比赛结束。

当前实现范围是：从人类当前可操作状态开始，建立一个只读的 AI 接管分支，并模拟一个根行动及其 Core-required choices；不延续到下一次自由行动。

## 2. 用户目标与模块定义

### 2.1 AI 决策过程

这是真实对局中的观察面板，展示 AI 玩家已经做了什么：

- AI 最近一次实际执行的动作；
- 该次获得控制权后产生的全部内部决策；
- 每一步的动作类型、卡牌、目标、置信度、价值和执行状态；
- 可选查看本局之前已经发生的 AI 决策过程。

它不回答“如果人类由 AI 控制会怎么做”。

建议将当前的“AI 决策核心”改名为“AI 决策过程”。

### 2.2 AI 教师

这是一个独立的只读教学模块，回答：

> 从人类当前局面开始，如果这一次自由行动由 AI 控制人类，AI 会选择什么，并完成它必需的后续选择？

它需要：

- 从当前人类状态开始；
- 使用同一份正式模型进行策略推理；
- 处理模型选择的根行动，以及该行动强制产生的目标、排、插入位置等决策；
- 在根行动 resolution 完成、控制权离开人类或对局结束时停止；
- 展示完整推演轨迹、每一步的结构化证据和最终停止原因；
- 不执行这些动作，不改变真实对局。

它不负责：

- 重新实现游戏规则；
- 自己枚举或修改合法动作；
- 解释真实 AI 玩家已经完成的动作；
- 推演下一回合、下一小局或整场比赛；
- 暴露 AI 玩家的隐藏手牌或未公开候选动作；
- 生成所谓的神经网络隐藏思维过程。

## 3. 历史问题与实施结果

原实现曾把真实 AI 动作和教师内容混用。当前已经完成数据流分离：

1. `last_ai_actions` 仍只保存真实 AI 玩家执行的动作，并由“AI 决策过程”展示；
2. `TeacherPanel` 根据当前人类状态请求 `/api/teacher/preview-turn`；
3. Core 的 `preview_current_human_turn()` 在 clone 环境中使用正式策略完成一个根行动及其必要选择；
4. Teacher 只解释该 clone trace 中已经执行的 branch action；
5. `gwent_rl_env_clone` 会深拷贝 pending `ResolutionFrame`，避免分支的顺序选择污染真实 decision prefix。

因此教师结果既不是历史数据，也不是单步猜测；它是与真实对局隔离的当前局面 AI 指导行动链。

## 4. 目标运行时架构

```text
真实对局
  └─ Core state
       ├─ AI 玩家真实执行
       │    └─ executed AI decision trace
       │          └─ AI 决策过程面板
       │
       └─ 人类当前可操作状态
            └─ Core/Strategy 只读分支
                 └─ counterfactual action-chain trace
                      └─ Teacher evidence builder
                           └─ AI 教师单次指导行动面板
```

关键原则：

- 真实对局和教师分支使用不同的环境实例或明确的 snapshot/clone；
- 教师分支不能调用真实环境的 `step`；
- Strategy/Core 负责产生动作轨迹；
- Teacher Runtime 只把轨迹翻译成有证据的解释；
- React 只展示结构化结果，不复制规则或策略逻辑。

## 5. 推演边界定义

### 5.1 起点

推演起点是 BFF 请求时 Core 返回的当前人类状态，必须包含：

- 当前 `summary`；
- 人类可见的牌面和自己的手牌；
- 当前 Core 返回的合法动作；
- 当前 `summary.decision`；
- 当前回合、行动方和必要的公开局面信息。

推演只允许在人类是当前可操作方时启动。AI 正在真实行动、对局结束或状态不满足推演条件时，前端显示不可用原因，不使用旧结果冒充当前推演。

### 5.2 终点

终点以 Core 的状态转移为准，不由前端猜测。建议定义为：

- 当前人类的 `end_turn` 或 `pass` 导致本次控制权结束；
- 当前换牌阶段完成；
- Core 标记本次人类回合已完成；
- 对局异常结束或达到安全步数上限。

推演不会进入下一回合、下一小局，也不会推演到整场比赛结束。

### 5.3 多阶段动作

一张牌可能产生多个连续决策，例如：

```text
play_card
  -> choose_row
  -> choose_insert_position
  -> choose_card / choose_target
```

这些都属于同一根行动的完整 resolution，必须逐步记录；但 resolution 完成后不能继续选择新的自由 `play_card`、领袖或 pass。

## 6. Core / Strategy 层修改规划

### 6.1 增加只读回合预览能力

建议在 `tools/server/human_vs_ai.py` 增加类似以下语义的方法，而不是复用 `last_ai_actions`：

```text
preview_current_human_turn() -> CounterfactualActionChainTrace
```

该方法必须：

1. 读取当前真实环境状态；
2. 创建独立的环境分支，或通过 Core 提供的 snapshot/clone 能力建立等价副本；
3. 将策略 actor 路由到人类卡组和人类当前状态；
4. 对分支中的每个合法决策调用正式策略；
5. 只在分支环境中执行 `option_index`；
6. 记录每次决策前后的公开状态摘要、动作结构、概率、价值和状态；
7. 在根行动的 resolution 边界停止；
8. 丢弃分支，保证真实游戏句柄、真实 `last_human_action`、真实 `last_ai_actions` 和真实合法动作完全不变。

如果现有 C API 没有安全的 clone/snapshot 能力，应由 Core 增加明确的只读分支接口。禁止通过直接修改真实句柄后再“尝试恢复”来实现，因为这会污染随机数、pending choice、牌堆顺序或真实对局状态。

### 6.2 策略复用边界

推演必须复用生产路径中的：

- 同一份 promotion 模型；
- 同一 observation 转换；
- 同一合法 action mask；
- 同一 `option_index` 选择逻辑；
- 同一多阶段 action grammar。

不能在 Teacher 或前端中重写一套“推荐动作算法”。

推演时可以使用人类自己的手牌，因为它已经是当前玩家可见信息；不得把 AI 对手的隐藏手牌写入返回浏览器的 trace。

### 6.3 安全上限

为了防止 pending choice 或规则异常造成无限循环，分支需要：

- 当前回合最大决策步数；
- 单次推演时间上限；
- 超限时返回 `stopped_reason=budget_exceeded`；
- 不把超限视为真实对局失败；
- 本地 CPU 默认只运行一条分支，不启动并行 512 局训练。

推荐先复用现有的顺序决策保护上限，再根据真实回合轨迹调整，不改变训练并行参数。

## 7. Core HTTP Contract 规划

### 7.1 保留真实对局字段

现有字段继续表示真实 AI 执行结果：

- `last_ai_actions`：最近一次 AI 获得控制权后实际执行的动作列表；
- `last_human_action`：人类实际执行的最后一个动作；
- `actions`：当前真实状态下的合法动作列表。

不把教师预览结果写入这些字段，避免前端误把预测动作当成已经执行的动作。

### 7.2 新增预览响应

建议增加独立的 Core endpoint 或 Core service 方法，例如：

```text
POST /preview/current-human-turn
```

响应可以采用以下结构，具体字段名以 Core 最终 contract 为准：

```json
{
  "schema_version": "counterfactual-action-chain-v2",
  "base_state_signature": "...",
  "controlled_player": 0,
  "boundary": "one_root_action_with_required_choices",
  "status": "complete",
  "stopped_reason": "turn_finished",
  "steps": [
    {
      "serial": 1,
      "actor_id": 0,
      "decision_kind": "mulligan",
      "option_index": 3,
      "kind": "mulligan",
      "card_id": 202889,
      "source_object_index": 7,
      "target_object_index": -1,
      "target_side": -1,
      "target_zone": -1,
      "target_row": -1,
      "insert_position": -1,
      "probability": 0.248,
      "value": 0.014,
      "status": "APPLIED",
      "public_evidence": {}
    }
  ],
  "summary": {
    "step_count": 1,
    "start_decision": "mulligan",
    "end_decision": "turn",
    "public_score_before": {},
    "public_score_after": {}
  }
}
```

约束：

- `option_index` 必须来自 Core 实际合法动作；
- `kind`、对象索引、目标和插入位置必须保留结构化字段；
- 不能只返回自然语言 `label`；
- `probability` / `value` 没有真实值时必须返回 null，而不是编造；
- trace 中的公开状态只能是 human-vs-AI 安全视图；
- 该 contract 属于产品运行时预览 contract，不自动改变 RL Observation Schema；
- 如需改变 C ABI / Observation，必须单独评估 schema 版本和 checkpoint 兼容性。

## 8. BFF 与 Teacher Runtime 修改规划

### 8.1 BFF 分层

在 `apps/web/backend` 增加独立的教师回合推演接口，例如：

```text
POST /api/teacher/preview-turn
```

BFF 流程：

1. 调用 Core 的只读预览接口；
2. 校验严格的 `CounterfactualActionChainTrace`；
3. 删除或过滤不应发送给 Teacher/React 的隐藏字段；
4. 将结构化 trace 和安全的公开局面传给 Teacher Runtime；
5. 返回单次指导行动链的教学响应；
6. Core 预览失败或 Teacher 超时时，只让教师面板降级，不阻断真实对局。

现有 `/api/teacher/explain` 可以保留，用于调试或解释真实 AI 已执行动作，但不能继续作为“人类当前回合教师”的前端数据源。

### 8.2 Teacher Runtime

Teacher Runtime 应新增“单次指导行动链 trace explanation”能力：

- 读取 Core/Strategy 已生成的步骤列表；
- 为每一步生成 grounded explanation；
- 生成回合级摘要；
- 说明推演终点和停止原因；
- 在没有 LLM provider 时使用 deterministic fallback；
- 不新增动作、不排序动作、不覆盖 `option_index`。

建议响应包括：

- `mode: counterfactual_turn`；
- `headline`：例如“如果由 AI 控制，本回合会这样进行”；
- `steps`：逐步动作标签、解释、概率、价值和依据；
- `summary`：回合级解释；
- `stopped_reason`；
- `grounded_facts`；
- `caveats`：明确这是策略回放，不是神经网络隐藏思维过程。

Teacher 仍然遵守以下原则：

- Strategy/Core 选择动作，Teacher 解释动作；
- 证据不足时降级，不补造规则理由；
- 不通过 `label`、`source` 或 `target` 文本反推规则；
- 不返回 AI 隐藏手牌或未公开候选；
- 不进入 PPO observation、reward 或训练更新链。

## 9. 前端修改规划

### 9.1 AI 决策过程面板

修改 `AiPanel`：

- 标题从“AI 决策核心”改为“AI 决策过程”；
- “AI 最近一步”改为“AI 最近一次实际动作”；
- 保持当前真实 AI 动作和决策历史展示；
- 在动作卡片上明确标注“已执行”；
- 不展示教师预测结果；
- 如果没有真实 AI 动作，显示“本局尚未发生 AI 实际动作”，不能沿用旧数据。

### 9.2 AI 教师面板

重构 `TeacherPanel` 的数据源：

- 不再根据 `game.last_ai_actions` 自动请求解释；
- 改为根据当前 Core `match_id/revision` 请求 `preview-turn`；
- 新局、换牌完成、人类每次实际动作后重新生成；
- 状态变化时取消旧请求，防止旧推演覆盖新状态；
- 显示“AI 接管当前回合推演”而不是“AI 最近一步解释”；
- 展示完整步骤时间线，而不是只显示单步 headline；
- 标记预测步骤为“未执行，仅供参考”；
- 显示推演完成、不可用、超时和超步数等状态。

建议界面结构：

```text
AI 教师
└─ 当前回合 AI 接管推演
   ├─ 推演状态：已完成 / 推演中 / 暂不可用
   ├─ 回合级结论
   ├─ 第 1 步：动作 + 依据
   ├─ 第 2 步：动作 + 依据
   ├─ ...
   └─ 停止原因：当前回合结束
```

教师推演默认不提供“一键执行整条路线”，人类仍然通过真实合法动作面板操作。后续如果需要动作对照，可以增加“定位到对应合法动作”，但仍不能绕过 Core action contract。

## 10. 状态一致性与缓存

每次预览必须绑定 Core 的 `match_id + revision`。`base_state_signature` 仍可作为
诊断和状态不变性辅助证据，但不能替代权威 revision，也不能由浏览器自行拼接
对象列表来决定结果是否过期。

如果真实对局状态发生变化：

- 旧推演标记为过期；
- 不继续显示为当前建议；
- 新推演完成前显示“正在根据最新局面重新推演”；
- 请求返回时检查 `base_match_id/base_revision`，旧版本直接丢弃。

缓存由 BFF 负责：Core trace 键为 `(match_id, revision, trace schema)`，Teacher
文本键额外包含 level；不能仅按游戏 ID 缓存。

## 11. 修改顺序

### Phase 0：契约确认

- 确认 Core 当前是否支持安全 clone/snapshot；
- 确认“当前回合结束”的 Core 状态边界；
- 定义 `counterfactual-action-chain-v2` 字段；
- 明确公开信息和隐藏信息过滤规则；
- 不修改代码前先补充 contract 文档和测试样例。

### Phase 1：Core/Strategy 只读分支

- 增加分支环境创建与销毁；
- 复用生产推理路径；
- 实现当前回合多步追踪；
- 增加换牌、普通出牌、多阶段目标选择的 trace 测试；
- 验证预览前后真实环境 state signature 完全一致。

### Phase 2：BFF contract

- 增加严格 Pydantic 模型；
- 增加 Core preview client；
- 增加 `/api/teacher/preview-turn`；
- 增加错误隔离和超时处理；
- 更新 `apps/web/docs/CORE_API_CONTRACT.md` 和相关 Product 文档。

### Phase 3：Teacher Runtime

- 增加单次指导行动链 trace evidence builder；
- 增加逐步解释和回合级摘要；
- 增加 deterministic fallback；
- 增加隐私过滤和不泄露隐藏信息的测试；
- 保留现有已执行动作解释能力用于兼容和调试。

### Phase 4：前端

- 重命名“AI 决策核心”为“AI 决策过程”；
- 修正真实动作面板的文案；
- 将 Teacher 数据源切换到预览接口；
- 增加根动作/必要选择分组和版本化过期状态；
- 加入加载、失败、超时和无可推演状态；
- 只消费结构化字段，不根据文本猜规则。

### Phase 5：Docker 与浏览器验收

- 重建 Core/BFF/Teacher/Web 镜像；
- 以本地最终模型启动，不涉及训练 checkpoint；
- 测试单条人类 vs AI 对局；
- 在浏览器确认真实 AI 决策过程和教师单次指导行动不会混淆；
- 检查 CPU 内存和单次推演耗时。

## 12. 验证计划

### Core/Strategy

- 新局后预览不会生成真实 `last_ai_actions`；
- 预览不会改变真实 `actions`、回合、牌堆、随机数和对象；
- 换牌阶段能完整记录当前换牌流程；
- 多阶段动作能记录所有 pending choice；
- 到达 `end_turn` / `pass` 后停止，不进入下一回合；
- 达到预算时返回明确停止原因；
- 预览使用人类卡组路由策略；
- 不返回对手隐藏手牌。

### BFF/Teacher

- 严格模型拒绝未知字段和错误对象索引；
- Teacher 只解释 trace 中已有动作；
- Teacher 不创建新的 `option_index`；
- Teacher 服务不可用时游戏 step 仍成功；
- deterministic fallback 能解释完整 trace；
- hidden-information regression 通过。

### Frontend

- AI 决策过程显示真实最新已执行动作；
- 教师显示当前人类状态对应的单个根行动及其必要选择；
- 两者使用不同标题、不同 badge 和不同数据源；
- 新状态返回后旧教师结果不会短暂覆盖新结果；
- 前端 build 通过。

### Docker/浏览器

- Web、BFF、Core、Teacher 健康检查通过；
- 人类 vs AI 新局可正常进入；
- 完成一次换牌后能看到完整教师推演；
- 实际点击人类动作后，教师重新基于最新状态推演；
- 真实对局不因教师失败中断；
- 不运行 512 局并行训练。

## 13. 不做的事情

- 不把 Teacher 改成新的游戏决策核心；
- 不在 React 中加载 checkpoint 或 C++ shared library；
- 不在 BFF/Teacher 重写合法动作；
- 不把当前教师预测写入 `last_ai_actions`；
- 不把推演扩展到下一回合、下一小局或整场比赛；
- 不为了此功能下载服务器 checkpoint 历史目录；
- 不修改 PPO、collector、reward 或训练并行规模；
- 不向浏览器展示 AI 隐藏手牌、未公开候选或模型 chain-of-thought。

## 14. 完成标准

只有同时满足以下条件，才认为本需求完成：

1. 页面能明确区分“AI 真实决策过程”和“AI 教师当前回合推演”；
2. AI 教师从人类当前状态开始，返回一个根行动及其必要选择的行动链；
3. 推演过程中真实对局状态完全不变；
4. 推演在当前回合结束时停止，不进入后续回合；
5. 每一步都来自 Core 合法 action 和正式策略输出；
6. 解释内容可追溯到结构化 evidence；
7. 隐藏信息边界和 Teacher 旁路失败隔离均通过测试；
8. Docker 本地 CPU 部署可以在浏览器中完成实际验收。

## 15. 相关事实来源

- [`tools/server/human_vs_ai.py`](../../tools/server/human_vs_ai.py)：真实 AI 执行、`last_ai_actions`、公开状态和本地模型推理入口。
- [`apps/web/frontend/src/components/AiPanel.tsx`](../../apps/web/frontend/src/components/AiPanel.tsx)：真实 AI 决策过程展示。
- [`apps/web/frontend/src/components/TeacherPanel.tsx`](../../apps/web/frontend/src/components/TeacherPanel.tsx)：当前单步 Teacher 展示入口。
- [`apps/web/backend/app/api/teacher.py`](../../apps/web/backend/app/api/teacher.py)：当前已执行动作解释 BFF。
- [`services/teacher/`](../../services/teacher/)：Teacher evidence、privacy 和 deterministic explanation。
- [`docs/current/CORE_CONTRACTS.md`](./CORE_CONTRACTS.md)：Core 规则、动作和跨层 contract 原则。
- [`docs/current/TEACHER_AND_WEB.md`](./TEACHER_AND_WEB.md)：Teacher 与 Web 的现有边界。
