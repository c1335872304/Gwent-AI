# Teacher Evidence Contract

Teacher 的解释只允许建立在可追溯的结构化证据上。

## Evidence classes

### Executed action / Core branch action

动作可以来自真实 AI 对局，也可以来自 Core clone 中已经执行的当前人类回合 branch。两种情况都必须由
Strategy/Core 实际选择并执行，不能由 Teacher 自行补出。

- decision kind;
- chosen option/action;
- source card/object（若公开）；
- target / row / insert position（若 contract 提供）；
- action probability / rank（若 Strategy 提供）。

### Counterfactual action-chain hierarchy

`counterfactual-action-chain-v2` 的第一个 `root_action` 是 AI 对人类当前局面的建议动作；后续
`required_choice` 必须带同一个 `parent_decision_serial`，仅表示 Core 为完成该动作要求的目标、排或位置。
Teacher 可以解释该层级，但不得把子选择说成新的独立出牌，也不得补造第二个 root action。

### Public state

- round / score / turn owner；
- visible board；
- graveyard / public statuses；
- public deck definition knowledge。

### Strategy metadata

- chosen probability；
- state value；
- policy top-k，仅在信息边界允许时。

### Rule/card knowledge

只使用项目本地 card definition / glossary 中可验证的公开语义。

## Grounding rule

解释中的事实断言必须至少能映射到一个 evidence field。无法 grounding 的内容应使用保守措辞，或不输出。
