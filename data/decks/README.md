# Deck Manifests

Deck manifests are lightweight compositions over the supported card library.
They reference card ids from `data/cards/supported_cards.json` and do not own
card text, static card identity, effect ids, or rules.

`deck_a.json` remains the default training/development deck. `deck_b.json` is
the supported White Frost tournament deck. The RL environment and collector
select them independently for player 0 and player 1 with deck ids A/B; Python
training configs use `collector.player0_deck` and `collector.player1_deck`.
Adding another deck should not require duplicating card definitions or effect
handlers.

Every deck manifest must declare `"card_set": "supported_cards"`. Its leader,
stratagem, and every main-deck card id must exist in
`data/cards/supported_cards.json`. Reference-only ids from
`data/cards/reference/all_cards.json` are deliberately invalid here: a card
must first receive Core behavior and tests before a runtime deck can use it.

Run `python tools/codegen/validate_card_data.py` after adding or changing any
deck manifest.
