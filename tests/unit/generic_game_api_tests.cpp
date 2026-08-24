#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>

#include "gwent/api/core.hpp"
#include "gwent/cards/deck_a.hpp"
#include "gwent/cards/supported_cards.hpp"
#include "gwent/generated/supported_card_data.hpp"

using namespace gwent;

namespace {

EffectRegistry make_supported_effects() {
    EffectRegistry effects;
    supported_cards::register_supported_card_effects(effects);
    return effects;
}

void test_generic_game_uses_each_players_deck_spec() {
    DeckSpec deck0 = deck_a::make_deck_spec();
    DeckSpec deck1 = deck_a::make_deck_spec();
    assert(deck1.cards.size() > 1);
    std::rotate(deck1.cards.begin(), deck1.cards.begin() + 1, deck1.cards.end());

    api::MatchConfig config;
    config.seed = 17;
    config.starting_player_id = kPlayerZero;
    config.shuffle_decks = false;

    api::Game game = api::Game::create(
        api::MatchSpec{deck0, deck1},
        make_supported_effects(),
        config
    );

    const auto& player0 = game.state().player(kPlayerZero);
    const auto& player1 = game.state().player(kPlayerOne);
    assert(!player0.starting_deck.empty());
    assert(!player1.starting_deck.empty());
    assert(player0.starting_deck.front() == deck0.cards.front().definition.id);
    assert(player1.starting_deck.front() == deck1.cards.front().definition.id);
    assert(player0.starting_deck.front() != player1.starting_deck.front());
    assert(game.validate().ok());
    assert(!game.legal_actions().empty());
}

void test_deck_a_wrapper_matches_generic_game() {
    api::MatchConfig config;
    config.seed = 424242;
    config.starting_player_id = kPlayerOne;
    config.shuffle_decks = true;

    const DeckSpec deck = deck_a::make_deck_spec();
    api::Game generic = api::Game::create(
        api::MatchSpec{deck, deck},
        make_supported_effects(),
        config
    );
    api::DeckAGame compatible = api::DeckAGame::create(config);

    assert(generic.snapshot_json() == compatible.snapshot_json());
    assert(generic.checksum() == compatible.checksum());
    assert(generic.legal_actions() == compatible.legal_actions());

    const Action action = generic.legal_actions().front();
    assert(generic.apply(action));
    assert(compatible.apply(action));
    assert(generic.checksum() == compatible.checksum());
}


void test_generic_game_catalog_contains_supported_runtime_choice_definitions() {
    api::MatchConfig config;
    config.seed = 7;
    config.starting_player_id = kPlayerZero;
    config.shuffle_decks = false;

    api::Game game = api::Game::create(
        api::MatchSpec{generated::make_deck_b_deck_spec(), generated::make_deck_a_deck_spec()},
        make_supported_effects(),
        config
    );

    const auto& catalog = game.state().card_catalog;
    assert(catalog != nullptr);
    assert(catalog->id_of(deck_a::kRedRidersId).has_value());
    assert(catalog->id_of(deck_a::kRedRidersLongFrostId).has_value());
    assert(catalog->id_of(deck_a::kRedRidersReplayId).has_value());
    assert(catalog->id_of(deck_a::kRedRidersBothRowsId).has_value());
}

void test_deck_b_matchups_finish() {
    const DeckSpec deck_a_spec = generated::make_deck_a_deck_spec();
    const DeckSpec deck_b_spec = generated::make_deck_b_deck_spec();
    const std::array<api::MatchSpec, 3> matchups{{
        {deck_a_spec, deck_b_spec},
        {deck_b_spec, deck_a_spec},
        {deck_b_spec, deck_b_spec},
    }};

    for (std::size_t matchup = 0; matchup < matchups.size(); ++matchup) {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            api::MatchConfig config;
            config.seed = 1000 * matchup + seed;
            config.shuffle_decks = true;
            config.kernel_config.invariant_policy = InvariantPolicy::AfterApply;
            api::Game game = api::Game::create(matchups[matchup], make_supported_effects(), config);

            int decisions = 0;
            while (!game.is_finished() && decisions++ < 1000) {
                const auto actions = game.legal_actions();
                assert(!actions.empty());
                const auto non_pass = std::find_if(actions.begin(), actions.end(), [](const Action& action) {
                    return action.type != ActionType::Pass;
                });
                const Action& action = non_pass != actions.end() ? *non_pass : actions.front();
                const auto result = game.apply(action);
                assert(result.applied);
            }
            assert(game.is_finished());
            assert(game.validate().ok());
        }
    }
}


void test_runtime_supported_catalog_is_complete() {
    api::MatchConfig config;
    config.seed = 99;
    config.starting_player_id = kPlayerZero;
    config.shuffle_decks = false;
    const auto game = api::Game::create(
        api::MatchSpec{generated::make_deck_b_deck_spec(), generated::make_deck_a_deck_spec()},
        make_supported_effects(),
        config
    );
    const auto& state = game.state();
    assert(state.card_catalog != nullptr);
    for (const auto& definition : gwent::supported_cards::make_all_definitions()) {
        assert(state.card_catalog->id_of(definition.id).has_value());
    }
}

void test_oberon_out_of_deck_bronze_wild_hunt_definitions_exist() {
    api::MatchConfig config;
    config.seed = 100;
    config.starting_player_id = kPlayerZero;
    config.shuffle_decks = false;
    const auto game = api::Game::create(
        api::MatchSpec{generated::make_deck_b_deck_spec(), generated::make_deck_a_deck_spec()},
        make_supported_effects(),
        config
    );
    const auto& state = game.state();
    assert(state.card_catalog != nullptr);
    for (const char* id : {"132310", "132402", "203161"}) {
        assert(state.card_catalog->id_of(id).has_value());
        const auto definition_id = state.card_catalog->id_of(id);
        assert(definition_id.has_value());
        const auto& definition = state.card_catalog->at(*definition_id);
        assert(definition.card_type == gwent::CardType::Unit);
    }
}

}  // namespace

int main() {
    test_generic_game_uses_each_players_deck_spec();
    test_deck_a_wrapper_matches_generic_game();
    test_generic_game_catalog_contains_supported_runtime_choice_definitions();
    test_deck_b_matchups_finish();
    test_runtime_supported_catalog_is_complete();
    test_oberon_out_of_deck_bronze_wild_hunt_definitions_exist();
    std::cout << "generic_game_api_tests: ok\n";
    return 0;
}
