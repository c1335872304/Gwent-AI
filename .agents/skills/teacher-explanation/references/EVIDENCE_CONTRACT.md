# Teacher Evidence Contract

Teacher 的解释只允许建立在可追溯的结构化证据上。

## Evidence classes

### Executed action

- decision kind;
- chosen option/action;
- source card/object（若公开）；
- target / row / insert position（若 contract 提供）；
- action probability / rank（若 Strategy 提供）。

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
