# Action Script Schema

Action scripts are line-oriented text files consumed by `gwent_trace_runner --script <file>`. They are meant to be readable, deterministic, and resilient to entity-id allocation changes.

## Lexical format

```text
operation key=value key=value ...
```

Rules:

1. Blank lines are ignored.
2. `#` begins a comment. Inline comments are allowed.
3. Tokens are whitespace-separated.
4. Every argument after the operation must be `key=value`.
5. Selectors should prefer semantic ids (`cid`, `name`) over fragile hand indexes when a card identity matters.

## Common arguments

| Argument | Meaning |
| --- | --- |
| `p` or `player` | Acting player id. Defaults to `state.current_player_id`. |
| `h` or `hand` | Index in the acting player's hand. |
| `cid` or `card_id` | Static card definition id selector. |
| `name` or `card_name` | Card name selector. |
| `copy` | Zero-based copy index among matching candidates. |
| `row` | `melee` or `ranged`. |
| `side` | `self`, `enemy`, `0`, or `1`. |
| `target` | Board reference `side:zone:index`. |
| `source` / `order` | Board reference for an order source. |

## Board references

A board reference has this format:

```text
side:zone:index
```

Examples:

```text
self:melee:0
敵方: unsupported; use enemy:melee:0
0:ranged:2
enemy:cemetery:0
```

Supported side values: `self`, `me`, `actor`, `enemy`, `opponent`, `0`, `1`.

Supported zone values: `melee`, `ranged`, `hand`, `deck`, `graveyard`, `cemetery`, `banished`, `stay`.

## Supported operations

### `pass`

```text
pass p=0
```

Creates `Action::pass(player)`.

### `end_turn`

```text
end_turn p=0
```

Creates `Action::end_turn(player)`. This is used for post-play/order-close states that require explicit turn closure.

### `mulligan`

```text
mulligan p=0 h=2
mulligan p=0 cid=202437
```

Selects a hand card and creates a mulligan action.

### `discard_hand`

```text
discard_hand p=0 h=0
discard_hand p=0 cid=203099
```

Creates `Action::discard_card(player, hand_card)`. This consumes one hand card without playing it, making `end_turn` legal.

### `play_hand`

```text
play_hand p=0 h=0 row=melee side=self
play_hand p=0 cid=203099 row=ranged side=self position=1
```

For unit/artifact/stratagem-like cards, `row` and `side` choose the target row. `position=` (alias: `index=`) optionally selects the exact dynamic insertion index in that row. If omitted, the trace runner appends the card to the end of the current row so older trace scripts remain valid. If the selected hand card is a Special, the runner creates a source-only play-card action and ignores row placement fields.

### `play_special`

```text
play_special p=0 h=4
play_special p=0 cid=200301
```

Forces a source-only play-card action for a Special card.

### `use_leader`

```text
use_leader p=0
use_leader p=0 target=enemy:melee:0
```

Uses the acting player's leader. Direct targets are optional; if omitted, metadata-driven effects may create a pending target choice.

### `use_order`

```text
use_order p=0 source=self:melee:0
use_order p=0 source=self:melee:0 target=enemy:melee:0
```

Uses a battlefield entity order. `order=` is accepted as a fallback alias for `source=`.

### `choose_target`

```text
choose_target p=0
choose_target p=0 target=enemy:melee:0
choose_target p=0 cid=202437
```

Resolves a pending card-target choice. Without a selector, it chooses the first legal target.

### `choose_row`

```text
choose_row p=0 side=self row=melee
```

Resolves a pending row choice. Without explicit args, the runner chooses the first legal row target.

## Determinism guidance

Use `cid`, `name`, and explicit `target` selectors for golden cases that should survive hand/deck ordering changes. Use indexes only when the purpose of the test is specifically to lock down setup order or row-vector order.
