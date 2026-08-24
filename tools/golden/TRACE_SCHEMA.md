# Golden Trace Schema

Schema id: `gwent-golden-trace-v1`

This file documents the stable JSON artifact emitted by `gwent_trace_runner`. The trace is intended to be a regression asset: deterministic runs should produce byte-equivalent JSON for the same script, seed, deck mode, effects mode, and starting player.

## Top-level document

```json
{
  "schema_version": "gwent-golden-trace-v1",
  "engine": "cpp",
  "scenario": "trace_smoke",
  "seed": 0,
  "starting_player_id": 0,
  "steps": []
}
```

Required fields:

| Field | Type | Contract |
| --- | --- | --- |
| `schema_version` | string | Must be `gwent-golden-trace-v1`. |
| `engine` | string | Producer name, currently `cpp` for the C++ runner. |
| `scenario` | string | Human-readable scenario id. |
| `seed` | uint64-compatible integer | FastRng/setup seed. |
| `starting_player_id` | integer | Match starting player. |
| `steps` | array | Ordered trace steps; step 0 is always the initial state. |

## Step object

```json
{
  "step_index": 0,
  "label": "initial",
  "checksum": "3d10c296e878e8cc",
  "action": null,
  "result": null,
  "legal_surface": {},
  "state": {}
}
```

Required fields:

| Field | Type | Contract |
| --- | --- | --- |
| `step_index` | integer | Must equal the zero-based index in `steps`. |
| `label` | string | `initial` or the normalized action-script line that produced the step. |
| `checksum` | string | 16 lowercase hex chars. It is an FNV-1a fingerprint of the stable compact state JSON. |
| `action` | object or null | Parsed action that was submitted to Kernel. Null on the initial step. |
| `result` | object or null | Kernel result for the action. Null on the initial step. |
| `legal_surface` | object | Optional; present when `--include-legal-surface` is used. See `LEGAL_SURFACE_SCHEMA.md`. |
| `state` | object | Full state snapshot after the action resolves. |

## Action object

```json
{
  "type": "PLAY_CARD",
  "player_id": 0,
  "source_entity_id": 3,
  "target": {
    "kind": "ROW",
    "side": 0,
    "zone": "MELEE",
    "entity_id": -1
  }
}
```

`source_entity_id` is `-1` for source-less actions such as pass. `target.entity_id` is `-1` when the target is not a card entity.

## Result object

```json
{
  "applied": true,
  "status": "OK",
  "message": "",
  "events": []
}
```

If `applied` is false, the trace runner stops after appending that failed step. This makes the failing action visible in the artifact.

## State snapshot contract

State snapshots include match fields, `pending_choice`, two player objects, zone arrays, including `leader`, and optionally listeners. Entity arrays preserve the actual zone-vector order used by the engine.

Important stability rules:

1. Zone arrays preserve gameplay order and must not be sorted by card id/name.
2. `checksum` intentionally depends on entity ids. Cross-engine comparison tools may ignore entity ids and checksums when allocation differs.
3. `legal_surface.actions` must be sorted and unique to keep diffs deterministic.
4. New fields may be appended, but existing field names and meanings should not change within `gwent-golden-trace-v1`.

## Validators and comparison tools

```bash
python3 tools/golden/validate_trace_schema.py --trace build/golden/cpp/trace_smoke.json
python3 tools/golden/compare_trace.py left.json right.json
python3 tools/golden/legal_surface_diff.py left.json right.json --summary
python3 tools/golden/seed_sweep.py --runner build/gwent_trace_runner --script tools/golden/cases/trace_smoke.trace --seeds 0:8
```
