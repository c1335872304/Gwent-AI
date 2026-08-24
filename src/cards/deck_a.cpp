#include "gwent/cards/deck_a.hpp"

#include "gwent/generated/supported_card_data.hpp"

namespace gwent {
namespace deck_a {

DeckSpec make_deck_spec() {
    return generated::make_deck_a_deck_spec();
}

void register_basic_deck_a_effects(EffectRegistry& registry) {
    supported_cards::register_basic_supported_card_effects(registry);
}

void register_deck_a_effects(EffectRegistry& registry) {
    supported_cards::register_supported_card_effects(registry);
}

}  // namespace deck_a
}  // namespace gwent
