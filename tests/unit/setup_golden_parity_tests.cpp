#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "gwent/cards/deck_a.hpp"
#include "gwent/game/setup.hpp"

using namespace gwent;

namespace {

std::vector<std::string> card_ids_in_zone(const GameState& state, const std::vector<EntityId>& zone) {
    std::vector<std::string> ids;
    ids.reserve(zone.size());
    for (EntityId entity_id : zone) {
        const RuntimeCard* card = state.find_card(entity_id);
        assert(card != nullptr);
        ids.push_back(card->definition->id);
    }
    return ids;
}

void test_deck_a_seed_zero_opening_matches_python_fast_rng_fixture() {
    MatchSetupConfig config;
    config.seed = 0;
    config.starting_player_id = kPlayerZero;
    config.shuffle_decks = true;

    GameState state;
    [[maybe_unused]] const OpeningSetupResult result = setup_standard_match(
        state,
        deck_a::make_deck_spec(),
        deck_a::make_deck_spec(),
        config
    );

    const std::vector<std::string> expected_p0_hand = {
        "202233", "202888", "202231", "202230", "201698",
        "202228", "200301", "203099", "132310", "202230",
    };
    const std::vector<std::string> expected_p1_hand = {
        "202230", "202229", "201698", "202231", "132310",
        "202230", "202228", "203284", "202233", "202229",
    };

    assert(card_ids_in_zone(state, state.player(kPlayerZero).hand) == expected_p0_hand);
    assert(card_ids_in_zone(state, state.player(kPlayerOne).hand) == expected_p1_hand);

    assert(state.player(kPlayerZero).row(Zone::Melee).size() == 1);
    const EntityId stratagem_id = state.player(kPlayerZero).row(Zone::Melee).front();
    const RuntimeCard* stratagem = state.find_card(stratagem_id);
    assert(stratagem != nullptr);
    assert(stratagem->definition->id == std::string(deck_a::kArmorerWorkshopId));
    assert(stratagem->definition->card_type == CardType::Stratagem);
    assert(!stratagem->has_power());
}

}  // namespace

int main() {
    test_deck_a_seed_zero_opening_matches_python_fast_rng_fixture();
    std::cout << "gwent_setup_golden_parity_tests: OK\n";
    return 0;
}
