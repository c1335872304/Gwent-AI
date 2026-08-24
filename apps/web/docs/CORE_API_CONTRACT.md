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

`index` is the only value submitted to `/step`. Structured target fields are presentation metadata; Web must not recreate legality from them.

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
{"option_index": 17}
```

The index must come from the **latest** `actions` array. After each step, every previous option index is stale.

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


## Game modes

`mode` is returned in `GameState` and can be supplied to `POST /new`:

- `human_vs_ai` (default): P0 is controlled by the browser and P1 is automatically stepped by the checkpoint policy. P1 hand identities are not returned.
- `manual_test`: no policy actions are executed. The browser submits the legal action for whichever player is currently the actor, and both hands are exposed for local card/rule debugging.

`POST /step` still accepts only `option_index`; legality remains authoritative in Core in both modes.
