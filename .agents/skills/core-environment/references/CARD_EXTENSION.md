# Card Extension Reference

这是新卡扩展的简要入口，不复制 Core 架构文档。

## 典型路径

```text
card identity / static data
        -> catalog / generated data
        -> effect / primitive / trigger / decision
        -> state transition
        -> unit test
        -> golden / trace（需要时）
```

优先查看：

- `data/cards/`：卡牌静态数据输入。
- `src/cards/`：牌组/卡牌相关实现与已有范例。
- `src/game/card_catalog.cpp`：catalog 接线。
- `src/engine/effect_registry.cpp`、`primitive_effects.cpp`、`primitives.cpp`：效果能力。
- `src/engine/kernel_*`、`decision.cpp`、`legal_actions.cpp`：需要交互/决策/合法动作时。
- `tests/unit/deck_a_*`：卡牌级测试范例。
- `tools/golden/`、`src/trace/`：跨步骤语义验证。

## 选择原则

先找“行为最像”的现有卡，而不是先找“名字最像”的文件。能复用已有 primitive/effect 就不要新增 Core 概念；只有现有能力无法表达卡牌语义时才扩展引擎。

## 当前占位

未来可以让 `scripts/card_extension_stub.py` 根据 card id/name 生成 data/test skeleton，但**不自动生成复杂规则代码**。规则语义仍由 Core Agent 基于已有能力和测试实现。


## Runtime-generated / helper definition 检查

新增或修改卡牌时，除了检查“这张牌自己是否在牌组里”，还必须列出它的 **runtime dependency closure**：

```text
本体
  -> 运行时生成的卡
  -> 变形目标
  -> 择一/helper definitions
  -> helper effect 再引用的 definitions
```

如果其中任何 definition 可能不在两副起始牌组里，就不能依赖 deck-only catalog 初始化。

最低验证要求：

- 卡牌级 unit test：验证效果语义；
- `api::Game::create()` integration test：验证真实 match catalog 能查到 runtime/helper definitions；
- 涉及 pending choice 时：验证真实路径确实产生对应 decision，而不是直接收尾进墓地；
- 涉及 RL 时：decision trace 应能看到 Core 暴露出的合法 choice。

详细规则见 `RUNTIME_CARD_CATALOG.md`。
