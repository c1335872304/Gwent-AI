# System Architecture

## 1. Development Plane

```text
                         Change Request
                               │
          ┌────────────────────┼────────────────────┐
          ▼                    ▼                    ▼
      Core Agent           Trainer Agent        Product Agent
          │                    │                    │
 $core-environment     $training-config   $product-integration
          │                    │                    │
          └──────────── contracts / tests ──────────┤
                                                    ▼
                                              Teacher Agent
                                         $teacher-explanation
                                                    │
                                                    ▼
                                            Validated Change
```

Development Plane 负责约束“代码怎么被修改”，不参与游戏运行。

## 2. Runtime Plane

```text
                         V3 checkpoint
                              │
                              ▼
+----------------+      +------------------+
| C++ Game Core  | ---> | Strategy Core    |
| state / rules  | <--- | policy + value   |
+-------+--------+      +---------+--------+
        │                         │ executed action
        │ public state            │ evidence
        ▼                         ▼
+----------------+        +------------------+
| FastAPI BFF    | <----- | Teacher Runtime  |
+-------+--------+        +------------------+
        │
        ▼
+----------------+
| React Frontend |
+----------------+
```

## 3. 权威边界

### Game Core

- 唯一规则事实来源；
- 生成 legal actions；
- 执行 action 并推进状态；
- 暴露 C ABI / observation / trace。

### Strategy Core

- 不重新判断合法性；
- 只对 Core action candidates 打分；
- 输出 policy、value、执行动作和可解释 evidence。

### Teacher Runtime

- 只读；
- 不修改动作；
- 不参与训练 reward；
- 不把隐藏候选或 AI 手牌返回给人类玩家。

### Web Product

- BFF 负责 contract 校验与服务编排；
- React 负责交互与展示；
- 不直接加载 C++ shared library 或 `.pt`；
- 不解析文本 label 反推规则。

## 4. 数据流

```text
Core GameState
   │
   ├─> legal actions ─> Strategy ─> chosen option_index ─> Core step
   │                         │
   │                         └─> decision evidence
   │                                   │
   └─> public state --------------------┼─> Teacher
                                       │
                                       └─> BFF ─> React panel
```

## 5. 训练与产品隔离

训练系统负责 collector、PPO、checkpoint 和 evaluation；产品 runtime 只加载封盘 checkpoint。服务器训练目录、optimizer state 和历史 update 不属于产品依赖。
