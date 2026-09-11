# Gwent Core HTTP API Contract

This is the versioned boundary between the Web product and `tools/server/human_vs_ai.py`.

## Ownership

Core owns authoritative state, game rules, legal actions, AI inference and checkpoint loading. In `human_vs_ai` mode Core also owns automatic P1 turns; in `manual_test` mode it never auto-steps either player. Web owns presentation, UX, forwarding the user's selected legal option, and product-level errors.

The Product layer may understand the **meaning** of Core decisions, but it must never reconstruct legality from display strings or duplicate card rules.

## Contract version

Every Core response used by the Product layer carries:

```json
{"api_version": 1}
```

`api_version` is independent from the RL observation schema/action-grammar versions. The FastAPI BFF rejects unsupported Core API versions before payloads reach React.

## Player convention

- `human_vs_ai`: P0 = human, P1 = AI.
- `manual_test`: P0 and P1 are both controlled manually from the same browser.

## `GET /state`

Important fields:

- `api_version`
- `match_id`（每次新开局随机生成）与单调递增的 `revision`
- `summary`
- `objects`
- `actions`
- `last_human_action`
- `last_ai_actions`
- `checkpoint`, `checkpoint_update`, `device`
- `decks: {p0, p1}`
- `mode`

Every `actions[]` item carries the same structured target fields. `-1` means the field is not applicable:

```json
{
  "index": 17,
  "kind_id": 11,
  "kind": "choose_insert_position",
  "label": "选择位置 · 2",
  "card_id": -1,
  "source": "-",
  "target": "P0 Melee/Melee @ position 2",
  "hand_slot": -1,
  "stable_hash": "18446744073709551615",
  "source_object_index": -1,
  "target_object_index": -1,
  "target_side": 0,
  "target_zone": 3,
  "target_row": 0,
  "insert_position": 2
}
```

`stable_hash` is a decimal string rather than a JSON number because the Core value is `uint64`; JavaScript cannot represent all `uint64` values losslessly as `number`.

`source` / `target` / `label` are presentation text only. React must never parse them to recover entity, side, row or insertion metadata.

`index` is the only legal-action value submitted to `/step`. Structured target
fields are presentation metadata; Web must not recreate legality from them.
The product also submits the `match_id` and `expected_revision` from the same
state response for optimistic concurrency.

Each public `objects[]` item contains the live object state plus static card
metadata:

```json
{
  "object_index": 0,
  "entity_id": 123,
  "card_id": 203099,
  "name": "雷吉斯：重生",
  "type": "unit",
  "ability_text": "部署：汲食 1 个敌军单位 3 点。",
  "owner": 0,
  "controller": 0,
  "zone": 3,
  "zone_name": "Melee",
  "row": 0,
  "row_name": "Melee",
  "slot": 0,
  "power": 1,
  "armor": 0,
  "status": []
}
```

`ability_text` is sourced from `data/cards/supported_cards.json`. It is
display-only metadata; the Web layer must not parse it to infer rules or legal
actions. It may be an empty string for generated tokens or cards without
registered text.

## `POST /new`

```json
{
  "seed": 123,
  "starting_player_id": -1,
  "player0_deck_id": 0,
  "player1_deck_id": 1,
  "mode": "human_vs_ai"
}
```

The BFF validates that deck ids are non-negative but deliberately does **not** duplicate the Core's supported-deck catalog. Core remains authoritative for whether a particular deck id exists.

## `POST /step`

```json
{
  "option_index": 17,
  "match_id": "0123456789abcdef0123456789abcdef",
  "expected_revision": 4
}
```

The index must come from the **latest** `actions` array. Core checks the match
identity and revision in its mutex before it checks action legality:

- a different match or revision returns `409 {"code":"stale_state", ...}`;
- a matching state with an illegal index returns `422 {"code":"invalid_action", ...}`;
- a successful real step increments `revision` exactly once before returning
  the next `GameState`.

`match_id` and `expected_revision` are temporarily optional for direct legacy
local callers, but the BFF and React always send both. Supplying only one is
treated as a stale request.

## `POST /preview/current-human-turn`

This is a read-only Strategy/Core endpoint used by the AI Teacher. It is
available only when `human_vs_ai` has a live P0 (human) decision. Core clones
the current environment, lets the promoted policy select one P0 root action
and only the Core-required choices that resolve it, then destroys the clone.
The real game is never stepped by this request.

Request body:

```json
{
  "match_id": "0123456789abcdef0123456789abcdef",
  "expected_revision": 4
}
```

Response:

```json
{
  "api_version": 1,
  "schema_version": "counterfactual-action-chain-v2",
  "base_match_id": "0123456789abcdef0123456789abcdef",
  "base_revision": 4,
  "base_state_signature": "64-char-sha256",
  "controlled_player": 0,
  "boundary": "one_root_action_with_required_choices",
  "root": {
    "decision_serial": 1,
    "kind": "play_card",
    "card_id": 202185,
    "source_object_index": 7
  },
  "status": "complete",
  "stopped_reason": "action_chain_resolved",
  "steps": [
    {"decision_serial": 1, "parent_decision_serial": null, "role": "root_action"},
    {"decision_serial": 2, "parent_decision_serial": 1, "role": "required_choice"}
  ],
  "start_summary": {},
  "end_summary": {}
}
```

Every `steps[]` entry preserves the normal structured action fields together
with `decision_serial`, `parent_decision_serial`, `role`, policy
`confidence`/`value`, and public `summary_before`/`summary_after`. Nested
choice steps additionally include `source_card_id`, `target_card_id` and
`source_zone`; these are authoritative relationship fields for explanations
such as “leader selects an enemy card”, not text parsed from a UI label.

The first and only `root_action` is the model's guidance to the human. A
`required_choice` belongs to that root and exists only while Core has a
pending resolution (for example `play_card → choose_row →
choose_insert_position`). Once Core returns to a free P0 `turn`, the trace
ends with `action_chain_resolved`; it never simulates a second voluntary card,
leader, pass, P1 turn, next round, or match result.

The trace is a product preview contract. It does not change the RL observation
schema, action grammar, reward contract, training parameters, or checkpoint
compatibility rules.

## Sequential position choice

Unit placement is not a fixed-grid UI contract. The Core owns the sequence:

```text
play_card → choose_row → choose_insert_position
```

For a row containing N units, the Core normally emits N+1 legal insertion actions. The Web renders exactly the insertion actions it receives and does not synthesize missing legal actions.

## Runtime validation

The BFF validates Core payloads with strict Pydantic models (`extra="forbid"`). Contract drift is treated as an upstream protocol error instead of being silently tolerated.

The frontend TypeScript contract uses required structured fields and contains no legacy regex/text fallback for target/entity/row/position extraction.

## Hidden-information boundary

In `human_vs_ai`, the public state strips P1 hidden hand identities before leaving the Core HTTP adapter; only the AI hand count is public. `manual_test` is an explicit local debugging mode and intentionally exposes both hands so either side can be operated.

The turn-preview response follows the same boundary: P0's current hand may
inform the P0 policy because it is already visible to the human player; P1's
hidden hand, deck order and unselected candidates never appear in the trace.


## Game modes

`mode` is returned in `GameState` and can be supplied to `POST /new`:

- `human_vs_ai` (default): P0 is controlled by the browser and P1 is automatically stepped by the checkpoint policy. P1 hand identities are not returned.
- `manual_test`: no policy actions are executed. The browser submits the legal action for whichever player is currently the actor, and both hands are exposed for local card/rule debugging.

`POST /step` accepts the Core-issued `option_index` plus the state identity
fields above; legality remains authoritative in Core in both modes.

## Preview lifecycle

The BFF caches a public Core trace by `(match_id, revision, schema_version)`
and the Teacher response by that key plus explanation level. Concurrent
identical preview requests share one in-flight request. Any successful `/new`
or `/step` invalidates older entries. React renders a Teacher response only
when its `base_match_id` and `base_revision` still equal its current
`GameState`; otherwise it discards it silently.
