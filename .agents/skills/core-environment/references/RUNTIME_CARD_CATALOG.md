# Runtime Card Catalog Contract

## 目的

避免“卡牌级单元测试通过，但真实 RL / C API 对局缺少卡牌能力”的环境错误。

C/C++ Core 是卡牌规则与合法动作的唯一事实来源。RL 只消费 Core 暴露的 observation / legal decisions，不复制卡牌能力。

## 典型失败模式

错误初始化：

```text
CardCatalog = deck0 definitions + deck1 definitions
```

当某个 effect 在运行时创建牌组外卡牌、变形、或引用 helper definition 时：

```text
effect
  -> lookup helper card id
  -> catalog miss
  -> handler early return / incomplete resolution
  -> RL 根本看不到本应出现的 pending choice
```

这不是“RL 不懂卡牌”，而是 Core 的真实 match catalog 不完整。

## 当前约束

标准 `api::Game::create()` 必须在 deck setup 后补齐所有
`supported_cards::make_all_definitions()`，保证 supported runtime effects
可查询其依赖 definition。

不要在 Python collector、Trainer、Product 或 Teacher 中补救 catalog 缺失。

## 新卡检查清单

对每个新增/修改的 supported card：

1. 列出直接 effect id。
2. 列出运行时 spawn/create/transform 目标。
3. 列出 card-definition choice/helper ids。
4. 递归检查 helper effect 是否再引用其他 definition。
5. 确认这些定义进入真实 `Game::create()` 后的 catalog。
6. 添加卡牌级 unit test。
7. 添加或扩展 generic Game API regression，覆盖真实初始化路径。
8. 若涉及 pending decision，验证 choice 在真实路径出现。
9. 若 Trainer 报“模型不会某动作”，先用 decision trace 确认 Core 是否真的提供过该动作。

## 测试层级

```text
effect/unit test
    ↓
generic Game::create integration
    ↓
C API / collector smoke（contract 有改动时）
    ↓
policy decision trace
```

只通过第一层不代表真实训练环境正确。

## 这次 Red Riders 事故的回归原则

`提尔纳丽雅 -> 生成红骑士 -> 红骑士三选一` 依赖的定义必须在真实
Deck B vs Deck A match catalog 中可见。回归测试至少检查：

- Red Riders 本体；
- long-frost helper；
- replay helper；
- both-rows helper。

如果以后增加新的 runtime helper，也按同样模式补 test，而不是只改牌组数据。
