---
name: teacher-explanation
description: 维护 Gwent Teacher 的 grounded explanation、evidence contract、隐私边界、provider 和前后端解释集成时使用。
---

# Teacher Explanation

这个 Skill 维护一个原则：**Teacher 可以把 Strategy 的证据翻译成人类可理解的解释，但不能反向成为游戏决策者。**

## Trigger

出现以下任务时读取本 Skill：

- 新增/修改 Teacher explanation；
- 给 Teacher 增加新的 evidence 字段；
- 接入或替换 LLM/provider；
- 修改 `/v1/explain` 或 BFF Teacher contract；
- 修改 React `TeacherPanel`；
- 处理“为什么 AI 这么下”的可解释性需求；
- 处理隐藏手牌、候选动作、隐私泄露问题。

## Workflow

### 1. 先确定解释对象

Teacher 解释的是 **已执行 decision/action**，不是重新求解动作。这里的“已执行”可以是：

- 真实 AI 对局中已经落地的动作；
- Core clone 在反事实“当前人类回合 AI 接管”预演中已经落地的 branch action。

第二种场景仍由 Core/Strategy 选择和执行动作，Teacher 只读取返回的 trace；Teacher 自身不得持有或 step
任何 Core handle。

先定位：

```text
Strategy / Core
    ↓
DecisionPacket / TraceDecision
    ↓
Teacher evidence builder
    ↓
provider / deterministic renderer
    ↓
TeacherResponse
```

如果输入里没有足够 evidence，先修结构化 contract；禁止从自然语言 label 猜 target、row、card relation。

### 2. 建立 grounded evidence

读取 `references/EVIDENCE_CONTRACT.md`。

Evidence 至少区分：

- executed action；
- public state；
- policy/value metadata；
- card/rule knowledge；
- optional alternatives（只在不会泄露隐藏信息时）。

任何解释句都应该能够追溯到这些字段之一。Provider 只能改表达，不能补造事实。

### 3. 先过 privacy filter

读取 `references/PRIVACY_BOUNDARY.md`。

human-vs-AI 模式默认：

- 已执行 AI 动作可解释；
- 公开 board / score / graveyard 可解释；
- AI 隐藏手牌不可发送到浏览器；
- 未公开候选卡牌/动作不可作为 alternatives 展示；
- opponent hidden information 不得通过 Teacher 间接泄露。

### 4. Provider-neutral

Provider 接口只做：

```python
generate(prompt: str) -> str
```

证据提取、隐私过滤、response schema、fallback 都不能依赖某个具体 LLM。

没有 Provider 时 deterministic fallback 必须仍能工作。

### 5. Product integration

Teacher Runtime 独立于 gameplay：

```text
Core/Strategy success -----> game continues
          |
          +----> Teacher failure ----> panel degrades only
```

BFF 应把 Teacher 当 optional dependency。Teacher 超时或失败不能阻断 `step`。

反事实回合预演的 Core clone 也属于 gameplay 的可选旁路：预演前后真实 game state 必须不变。若预演污染
pending choice、decision prefix 或 legal actions，停止在 Core/Product 边界并请求修复 clone 隔离；禁止通过
截断 trace、扩大 prefix 容量或让 Teacher 绕开 Core 来掩盖。

## Invariants

- Teacher 不产生或覆盖 `option_index`。
- Teacher 不重新计算 legal action。
- Teacher 不进入 PPO observation/reward/update 链。
- Teacher 不声称输出神经网络隐藏思维过程。
- Teacher 不从 label/source/target 文本反向推导结构化事实。
- Teacher 不把隐藏信息暴露给 React。
- 同一 evidence 在无 LLM 时必须有 deterministic explanation。

## Verification

最小验证：

```bash
python .agents/skills/teacher-explanation/scripts/check_teacher.py
PYTHONPATH=. pytest -q services/teacher/tests
PYTHONPATH=apps/web/backend pytest -q apps/web/backend/tests/test_teacher_api.py
```

如果修改 React TeacherPanel，再运行：

```bash
cd apps/web/frontend && npm run build
```

## Handoff

- 需要新增 authoritative game fact：交给 Core Agent 定义结构化字段；
- 需要 Strategy 输出新的 policy/value metadata：与 Trainer/Core 协作；
- 只是展示、布局、loading/error UX：交给 Product Agent；
- 不允许 Teacher Agent 自己修改规则来“让解释更容易”。
