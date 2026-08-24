# Card data manifests

This directory separates reference data from cards that the Core runtime truly
supports.

- `reference/all_cards.json` is a normalized, reference-only catalog used to
  search and plan future card work. It is not a runtime input and carries no
  effect bindings.
- `supported_cards.json` contains only cards whose runtime behavior is wired
  into Core. It is the only card set deck manifests may reference.

Deck lists live under `data/decks/`. A deck references supported card ids and
copy counts; it does not own card text, card identity, or effect bindings.

The engine does **not** parse these JSON files on the hot path. Instead, manifests are compiled into checked-in C++ files with:

```bash
python3 tools/codegen/generate_card_data.py
```

Use `--check` in CI to verify that generated sources are in sync with the JSON manifest:

```bash
python3 tools/codegen/generate_card_data.py --check
```

## Boundary

Card JSON owns static card data:

- card id and display name;
- card type, faction, base power, armor, provisions, color, rarity;
- categories/tags;
- effect id bindings;
- target selector metadata.

Deck JSON owns lightweight deck composition:

- leader card id;
- stratagem card id;
- main-deck card ids and counts.

C++ effect handlers still own game behavior:

- branching deploy/order/special logic;
- pending choices;
- listeners/triggers;
- random effects;
- cross-zone movement and nested card play.

This keeps card data easy to audit while avoiding a runtime JSON rules interpreter.

## Reference catalog

The catalog was normalized from the external
`gwent_cards_deduplicated.zip` artifact. The archive is an import source, not a
checked-in runtime asset. It did not state its upstream source, game version, or
redistribution license; that limitation is recorded in the catalog provenance
and must be resolved before treating the data as authoritative or redistributable.

Re-import explicitly when the external artifact changes:

```bash
python tools/codegen/import_reference_cards.py /path/to/gwent_cards_deduplicated.zip
```

Validate the boundary between the reference catalog, supported cards, and all
deck manifests with:

```bash
python tools/codegen/validate_card_data.py
```

The validator requires every non-token supported id to exist in the reference
catalog, but not the reverse. Runtime-generated tokens may be absent because
the supplied archive omits them (for example, Bat `132313` is only referenced
by related-card ids). Presence in the reference catalog never means a card is
implemented, legal in a deck, or safe for training.
