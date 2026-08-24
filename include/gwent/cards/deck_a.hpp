#pragma once

#include "gwent/cards/supported_cards.hpp"
#include "gwent/game/setup.hpp"

namespace gwent {
namespace deck_a {

// Source-compatibility bridge for existing Deck A callers. New generic match
// code should use supported_cards directly and treat this namespace as a deck
// definition adapter only.
using namespace supported_cards;

[[nodiscard]] DeckSpec make_deck_spec();

void register_basic_deck_a_effects(EffectRegistry& registry);
void register_deck_a_effects(EffectRegistry& registry);

}  // namespace deck_a
}  // namespace gwent
