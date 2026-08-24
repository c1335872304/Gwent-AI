# Legal Surface Schema

Legal surface JSON is included in each trace step when `gwent_trace_runner` is called with `--include-legal-surface`.

It is a compact, sorted description of the decisions available to the active player or pending-choice owner. It is not a replacement for full `Action` JSON; it is designed for human diffs, parity audits, and AI/front-end smoke tests.

## Shape

```json
{
  "decision_type": "turn",
  "player_id": 0,
  "actions": [
    "pass",
    "play_hand:eid=0:cid=trace_p0.unit_0:name=Trace Unit 0-0:target=none"
  ]
}
```

Required fields:

| Field | Type | Contract |
| --- | --- | --- |
| `decision_type` | string | One of `none`, `turn`, `post_play`, `effect_card`, `place_row`. |
| `player_id` | integer | Player expected to choose from this surface. |
| `actions` | array of strings | Sorted, unique labels. |

## Decision types

| Type | Meaning |
| --- | --- |
| `none` | No decision is available. |
| `turn` | Normal turn action surface: pass, play, order, leader. |
| `post_play` | The player must close the turn, usually with `end_turn`. |
| `effect_card` | A pending card-target effect is waiting for `choose_target`. |
| `place_row` | A pending row placement is waiting for `choose_row`. |

## Action label prefixes

Supported prefixes:

```text
pass
end_turn
discard_hand
play_hand
play_special
mulligan
use_order
use_leader
choose_target
choose_row
```

Labels may include entity signatures:

```text
eid=<runtime entity id>:cid=<definition id>:name=<card name>
```

Entity ids are stable only within one trace run. Cross-engine legal-surface comparisons should ignore entity ids if allocation differs.

## Diffing

Use:

```bash
python3 tools/golden/legal_surface_diff.py left.json right.json --summary
```

The summary groups actions by prefix at each step, which makes target-generation regressions easier to spot than a raw JSON diff.
