#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>

#include "gwent/cards/deck_a.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/legal_actions.hpp"

using namespace gwent;

namespace {

GameState make_running_state(PlayerId current = kPlayerZero) {
    GameState state;
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.round_no = 1;
    state.turn_no = 1;
    state.current_player_id = current;
    state.starting_player_id = current;
    state.round_starting_player_id = current;
    return state;
}

CardDefinition make_wild_hunt_special(std::string id = "wh_special") {
    CardDefinition definition = make_special_definition(std::move(id), "Wild Hunt Special", Faction::Monsters);
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.provision = 4;
    definition.categories.push_back("狂猎");
    definition.main_category = "狂猎";
    return definition;
}

std::size_t count_events(const std::vector<EventRecord>& events, std::string_view name) {
    std::size_t count = 0;
    for (const EventRecord& event : events) {
        if (event.name == name) {
            ++count;
        }
    }
    return count;
}

void test_naglfar_reveals_two_gold_candidates_and_plays_selected_card() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId naglfar = state.add_card(deck_a::make_naglfar_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId ozzrel = state.add_card(deck_a::make_ozzrel_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    const EntityId queen = state.add_card(deck_a::make_queen_of_the_night_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 1});
    state.add_card(deck_a::make_fleder_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 2});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, naglfar), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->legal_card_targets.size() == 2);
    assert(state.pending_choice->legal_card_targets[0] == ozzrel);
    assert(state.pending_choice->legal_card_targets[1] == queen);
    assert((state.location_of(naglfar) == Location{kPlayerZero, Zone::Stay, 0}));

    KernelResult chosen = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, naglfar, ozzrel), registry);
    assert(chosen);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(state.pending_choice->source_entity_id == ozzrel);
    assert((state.location_of(naglfar) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert((state.location_of(ozzrel) == Location{kPlayerZero, Zone::Stay, 0}));
    assert((state.location_of(queen) == Location{kPlayerZero, Zone::Deck, 0}));
    assert(state.current_player_id == kPlayerZero);
    assert(count_events(chosen.events, "supported:play_from_deck_enqueued") == 1);

    KernelResult row = apply_action_with_kernel(state, Action::choose_row_target(kPlayerZero, ozzrel, kPlayerZero, Zone::Ranged), registry);
    assert(row);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::InsertPosition);
    assert(state.pending_choice->source_entity_id == ozzrel);

    KernelResult inserted = apply_action_with_kernel(
        state,
        Action::choose_insert_position(kPlayerZero, ozzrel, kPlayerZero, Zone::Ranged, 0),
        registry
    );
    assert(inserted);
    assert(!state.pending_choice.has_value());
    assert((state.location_of(ozzrel) == Location{kPlayerZero, Zone::Ranged, 0}));
    assert(state.current_player_id == kPlayerZero);
}

void test_geels_non_devotion_plays_wild_hunt_special_from_deck() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId geels = state.add_card(deck_a::make_geels_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId wild_hunt_special = state.add_card(make_wild_hunt_special(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    state.add_card(make_unit_definition("neutral_card", "Neutral Card", 1, Faction::Neutral), kPlayerZero, Location{kPlayerZero, Zone::Deck, 1});
    const EntityId wild_hunt_unit = state.add_card(deck_a::make_aen_elle_conqueror_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 2});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, geels, kPlayerZero, Zone::Melee, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->legal_card_targets.size() == 1);
    assert(state.pending_choice->legal_card_targets[0] == wild_hunt_special);

    KernelResult chosen = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, geels, wild_hunt_special), registry);
    assert(chosen);
    assert((state.location_of(geels) == Location{kPlayerZero, Zone::Melee, 0}));
    assert((state.location_of(wild_hunt_special) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert((state.location_of(wild_hunt_unit) == Location{kPlayerZero, Zone::Deck, 1}));
    assert(state.current_player_id == kPlayerZero);
}

void test_geels_devotion_can_play_any_wild_hunt_card_from_deck() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId geels = state.add_card(deck_a::make_geels_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId wild_hunt_unit = state.add_card(deck_a::make_aen_elle_conqueror_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, geels, kPlayerZero, Zone::Melee, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->legal_card_targets.size() == 1);
    assert(state.pending_choice->legal_card_targets[0] == wild_hunt_unit);

    KernelResult chosen = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, geels, wild_hunt_unit), registry);
    assert(chosen);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(state.pending_choice->source_entity_id == wild_hunt_unit);
    assert((state.location_of(geels) == Location{kPlayerZero, Zone::Melee, 0}));
    assert((state.location_of(wild_hunt_unit) == Location{kPlayerZero, Zone::Stay, 0}));
    assert(state.current_player_id == kPlayerZero);

    KernelResult row = apply_action_with_kernel(state, Action::choose_row_target(kPlayerZero, wild_hunt_unit, kPlayerZero, Zone::Ranged), registry);
    assert(row);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::InsertPosition);

    KernelResult inserted = apply_action_with_kernel(
        state,
        Action::choose_insert_position(kPlayerZero, wild_hunt_unit, kPlayerZero, Zone::Ranged, 0),
        registry
    );
    assert(inserted);
    assert(!state.pending_choice.has_value());
    assert((state.location_of(wild_hunt_unit) == Location{kPlayerZero, Zone::Ranged, 0}));
    assert(state.current_player_id == kPlayerZero);
}


void test_play_from_deck_row_choice_excludes_full_rows() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId naglfar = state.add_card(deck_a::make_naglfar_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId ozzrel = state.add_card(deck_a::make_ozzrel_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    state.add_card(deck_a::make_queen_of_the_night_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 1});

    for (int i = 0; i < 9; ++i) {
        state.add_card(
            make_unit_definition("full_row_" + std::to_string(i), "Full Row", 1),
            kPlayerZero,
            Location{kPlayerZero, Zone::Ranged, static_cast<std::size_t>(i)}
        );
    }

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    const KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, naglfar), registry);
    assert(played && state.pending_choice.has_value());

    const KernelResult chosen = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, naglfar, ozzrel), registry);
    assert(chosen && state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(state.pending_choice->legal_row_targets.size() == 1);
    assert(state.pending_choice->legal_row_targets[0].zone == Zone::Melee);

    const auto actions = legal_pending_choice_actions(state);
    assert(actions.size() == 1);
    assert(actions[0].target.zone == Zone::Melee);
}

}  // namespace

int main() {
    test_naglfar_reveals_two_gold_candidates_and_plays_selected_card();
    test_geels_non_devotion_plays_wild_hunt_special_from_deck();
    test_geels_devotion_can_play_any_wild_hunt_card_from_deck();
    test_play_from_deck_row_choice_excludes_full_rows();
    std::cout << "gwent_deck_a_deck_play_tests: OK\n";
    return 0;
}
