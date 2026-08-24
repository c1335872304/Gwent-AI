#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

#include "gwent/core/card_definition.hpp"
#include "gwent/game/setup.hpp"

using namespace gwent;

namespace {

CardDefinition make_leader(std::string id = "leader") {
    CardDefinition leader;
    leader.id = std::move(id);
    leader.name = "Leader";
    leader.card_type = CardType::Leader;
    return leader;
}

DeckSpec make_deck(std::string prefix, int copies) {
    DeckSpec deck;
    deck.leader = make_leader(prefix + "_leader");
    deck.stratagem = make_stratagem_definition(prefix + "_stratagem", prefix + " Stratagem");
    deck.cards.push_back(DeckCardSpec{make_unit_definition(prefix + "_soldier", prefix + " Soldier", 4), copies});
    return deck;
}

void add_deck_cards(GameState& state, PlayerId player_id, int count, std::string prefix) {
    for (int i = 0; i < count; ++i) {
        auto unit = make_unit_definition(prefix + "_deck_" + std::to_string(i), "Deck Unit", 1);
        state.add_card(unit, player_id, Location{player_id, Zone::Deck, state.player(player_id).deck.size()});
    }
}

void add_hand_cards(GameState& state, PlayerId player_id, int count, std::string prefix) {
    for (int i = 0; i < count; ++i) {
        auto unit = make_unit_definition(prefix + "_hand_" + std::to_string(i), "Hand Unit", 1);
        state.add_card(unit, player_id, Location{player_id, Zone::Hand, state.player(player_id).hand.size()});
    }
}

void test_opening_setup_places_stratagem_for_starting_player_only() {
    GameState state;
    MatchSetupConfig config;
    config.starting_player_id = kPlayerZero;
    config.shuffle_decks = false;

    const OpeningSetupResult result = setup_standard_match(state, make_deck("p0", 12), make_deck("p1", 12), config);

    assert(result.starting_player_id == kPlayerZero);
    assert(result.stratagem_owner == kPlayerZero);
    assert(result.stratagem_entity_id != kInvalidEntityId);
    assert((result.stratagem_location == Location{kPlayerZero, Zone::Melee, 0}));

    assert(state.status == MatchStatus::Running);
    assert(state.phase == MatchPhase::Playing);
    assert(state.round_no == 1);
    assert(state.turn_no == 1);
    assert(state.current_player_id == kPlayerZero);
    assert(state.starting_player_id == kPlayerZero);
    assert(state.round_starting_player_id == kPlayerZero);

    assert(state.player(kPlayerZero).hand.size() == 10);
    assert(state.player(kPlayerOne).hand.size() == 10);
    assert(state.player(kPlayerZero).deck.size() == 2);
    assert(state.player(kPlayerOne).deck.size() == 2);

    assert(result.players[kPlayerZero].mulligan_limit == 3);
    assert(result.players[kPlayerOne].mulligan_limit == 2);
    assert(state.player(kPlayerZero).mulligans_available == 3);
    assert(state.player(kPlayerOne).mulligans_available == 2);

    const RuntimeCard* stratagem = state.find_card(result.stratagem_entity_id);
    assert(stratagem != nullptr);
    assert(stratagem->definition->card_type == CardType::Stratagem);
    assert(!stratagem->is_unit_card());
    assert(!stratagem->has_power());
    assert(state.player(kPlayerZero).row(Zone::Melee).size() == 1);
    assert(state.player(kPlayerOne).row(Zone::Melee).empty());
    assert(state.board_score(kPlayerZero) == 0);
    assert(state.board_score(kPlayerOne) == 0);
}

void test_opening_setup_respects_second_player_as_starter() {
    GameState state;
    MatchSetupConfig config;
    config.starting_player_id = kPlayerOne;
    config.shuffle_decks = false;

    const OpeningSetupResult result = setup_standard_match(state, make_deck("p0", 10), make_deck("p1", 10), config);

    assert(result.starting_player_id == kPlayerOne);
    assert(result.stratagem_owner == kPlayerOne);
    assert((result.stratagem_location == Location{kPlayerOne, Zone::Melee, 0}));
    assert(result.players[kPlayerZero].mulligan_limit == 2);
    assert(result.players[kPlayerOne].mulligan_limit == 3);
    assert(state.player(kPlayerZero).row(Zone::Melee).empty());
    assert(state.player(kPlayerOne).row(Zone::Melee).size() == 1);
    assert(state.current_player_id == kPlayerOne);
}

void test_round_two_draw_three_and_base_two_mulligans() {
    GameState state = make_empty_standard_state();
    add_deck_cards(state, kPlayerZero, 3, "p0");
    add_deck_cards(state, kPlayerOne, 5, "p1");

    const RoundPreparationResult result = prepare_standard_round_hand(state, 2, kPlayerOne);

    assert(result.round_no == 2);
    assert(result.round_starting_player_id == kPlayerOne);
    assert(result.players[kPlayerZero].actual_draw_count == 3);
    assert(result.players[kPlayerOne].actual_draw_count == 3);
    assert(result.players[kPlayerZero].mulligan_limit == 2);
    assert(result.players[kPlayerOne].mulligan_limit == 2);
    assert(state.player(kPlayerZero).mulligans_available == 2);
    assert(state.player(kPlayerOne).mulligans_available == 2);
    assert(state.player(kPlayerZero).hand.size() == 3);
    assert(state.player(kPlayerOne).hand.size() == 3);
    assert(state.current_player_id == kPlayerOne);
}

void test_round_two_missing_draws_become_extra_mulligans() {
    GameState state = make_empty_standard_state();
    add_deck_cards(state, kPlayerZero, 1, "p0");
    add_deck_cards(state, kPlayerOne, 0, "p1");

    const RoundPreparationResult result = prepare_standard_round_hand(state, 2, kPlayerZero);

    assert(result.players[kPlayerZero].actual_draw_count == 1);
    assert(result.players[kPlayerZero].missing_draw_count == 2);
    assert(result.players[kPlayerZero].mulligan_limit == 4);
    assert(state.player(kPlayerZero).mulligans_available == 4);

    assert(result.players[kPlayerOne].actual_draw_count == 0);
    assert(result.players[kPlayerOne].missing_draw_count == 3);
    assert(result.players[kPlayerOne].mulligan_limit == 5);
    assert(state.player(kPlayerOne).mulligans_available == 5);
}

void test_round_two_hand_limit_shortfall_also_adds_mulligans() {
    GameState state = make_empty_standard_state();
    add_hand_cards(state, kPlayerZero, 9, "p0");
    add_deck_cards(state, kPlayerZero, 5, "p0");
    add_deck_cards(state, kPlayerOne, 3, "p1");

    const RoundPreparationResult result = prepare_standard_round_hand(state, 3, kPlayerZero);

    assert(result.players[kPlayerZero].actual_draw_count == 1);
    assert(result.players[kPlayerZero].missing_draw_count == 2);
    assert(result.players[kPlayerZero].mulligan_limit == 4);
    assert(state.player(kPlayerZero).hand.size() == 10);

    assert(result.players[kPlayerOne].actual_draw_count == 3);
    assert(result.players[kPlayerOne].missing_draw_count == 0);
    assert(result.players[kPlayerOne].mulligan_limit == 2);
}

void test_setup_rejects_non_stratagem_tactic_card() {
    GameState state;
    DeckSpec deck0 = make_deck("p0", 10);
    DeckSpec deck1 = make_deck("p1", 10);
    deck0.stratagem.card_type = CardType::Unit;

    bool threw = false;
    try {
        [[maybe_unused]] const OpeningSetupResult result = setup_standard_match(state, deck0, deck1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main() {
    test_opening_setup_places_stratagem_for_starting_player_only();
    test_opening_setup_respects_second_player_as_starter();
    test_round_two_draw_three_and_base_two_mulligans();
    test_round_two_missing_draws_become_extra_mulligans();
    test_round_two_hand_limit_shortfall_also_adds_mulligans();
    test_setup_rejects_non_stratagem_tactic_card();

    std::cout << "gwent_setup_tests: OK\n";
    return 0;
}
