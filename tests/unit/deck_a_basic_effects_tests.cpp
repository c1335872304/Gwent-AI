#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "gwent/cards/deck_a.hpp"
#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
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
    state.starting_player_id = current;
    state.round_starting_player_id = current;
    state.current_player_id = current;
    return state;
}

std::size_t count_events(const std::vector<EventRecord>& events, const std::string& name) {
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const EventRecord& event) {
        return event.name == name;
    }));
}

void test_armorer_workshop_orders_hand_unit_then_banishes_stratagem() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId stratagem = state.add_card(deck_a::make_armorer_workshop_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId hand_unit = state.add_card(make_unit_definition("hand_vampire", "Hand Vampire", 4, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelConfig config;
    config.order_action_consumes_turn = false;
    KernelResult result = apply_action_with_kernel(
        state,
        Action::use_order(kPlayerZero, stratagem, ActionTarget::card(hand_unit)),
        registry,
        config
    );

    assert(result);
    const RuntimeCard* boosted = state.find_card(hand_unit);
    assert(boosted != nullptr);
    assert(boosted->state.power == 7);
    assert(boosted->state.armor == 2);
    assert((state.location_of(hand_unit) == Location{kPlayerZero, Zone::Hand, 0}));
    assert((state.location_of(stratagem) == Location{kPlayerZero, Zone::Banished, 0}));
    assert(state.board_score(kPlayerZero) == 0);
    assert(count_events(result.events, "card_boosted") == 1);
    assert(count_events(result.events, "armor_added") == 1);
    assert(count_events(result.events, "stratagem_consumed") == 1);
}

void test_giant_centipede_gets_armor_equal_to_remaining_hand_count() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId centipede = state.add_card(deck_a::make_giant_centipede_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("filler_a", "Filler A", 1, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    state.add_card(make_unit_definition("filler_b", "Filler B", 1, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Hand, 2});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, centipede, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    const RuntimeCard* card = state.find_card(centipede);
    assert(card != nullptr);
    assert(card->state.power == 16);
    assert(card->state.armor == 2);
    assert((state.location_of(centipede) == Location{kPlayerZero, Zone::Melee, 0}));
    assert(state.board_score(kPlayerZero) == 16);
    assert(count_events(result.events, "armor_added") == 1);
}

void test_giant_centipede_destroys_itself_when_hand_is_empty_after_play() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId centipede = state.add_card(deck_a::make_giant_centipede_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, centipede, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert((state.location_of(centipede) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert(state.board_score(kPlayerZero) == 0);
    assert(count_events(result.events, "card_destroyed") == 1);
}

void test_katakan_order_spawns_ekimmara_on_same_row() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId katakan = state.add_card(deck_a::make_katakan_definition(), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.add_card(make_unit_definition("katakan_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelConfig config;
    config.order_action_consumes_turn = false;
    KernelResult result = apply_action_with_kernel(state, Action::use_order(kPlayerZero, katakan), registry, config);

    assert(result);
    assert(state.player(kPlayerZero).row(Zone::Ranged).size() == 2);
    const EntityId spawned = state.player(kPlayerZero).row(Zone::Ranged).back();
    const RuntimeCard* ekimmara = state.find_card(spawned);
    assert(ekimmara != nullptr);
    assert(ekimmara->definition->id == std::string(deck_a::kEkimmaraId));
    assert(ekimmara->definition->card_type == CardType::Unit);
    assert(ekimmara->state.power == 3);
    assert(ekimmara->owner_id == kPlayerZero);
    assert(ekimmara->controller_id == kPlayerZero);
    assert(state.board_score(kPlayerZero) == 10);
    assert(count_events(result.events, "card_spawned") == 1);
    assert(!can_use_order_source(state, kPlayerZero, katakan));
}

void test_garkain_and_bruxa_orders_apply_bleeding_to_enemy_unit() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId garkain = state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.add_card(make_unit_definition("orders_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy_a = state.add_card(make_unit_definition("enemy_a", "Enemy A", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId enemy_b = state.add_card(make_unit_definition("enemy_b", "Enemy B", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelConfig config;
    config.order_action_consumes_turn = false;

    KernelResult first = apply_action_with_kernel(
        state,
        Action::use_order(kPlayerZero, garkain, ActionTarget::card(enemy_a)),
        registry,
        config
    );
    assert(first);
    assert(state.find_card(enemy_a)->state.bleeding == 2);
    assert(count_events(first.events, "status_added") == 1);
    assert(!can_use_order_source(state, kPlayerZero, garkain));

    KernelResult second = apply_action_with_kernel(
        state,
        Action::use_order(kPlayerZero, bruxa, ActionTarget::card(enemy_b)),
        registry,
        config
    );
    assert(second);
    assert(state.find_card(enemy_b)->state.bleeding == 3);
    assert(count_events(second.events, "status_added") == 1);
    assert(!can_use_order_source(state, kPlayerZero, bruxa));
}

void test_order_effects_do_not_fire_during_deploy() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, bruxa, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert(state.find_card(enemy)->state.bleeding == 0);
    assert(count_events(result.events, "status_added") == 0);
}

void test_invalid_direct_order_target_is_rejected_before_effect_resolution() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId ally = state.add_card(make_unit_definition("ally", "Ally", 5, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.add_card(make_unit_definition("invalid_target_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelConfig config;
    config.order_action_consumes_turn = false;
    KernelResult result = apply_action_with_kernel(
        state,
        Action::use_order(kPlayerZero, bruxa, ActionTarget::card(ally)),
        registry,
        config
    );

    assert(!result);
    assert(result.status == ActionStatus::InvalidTarget);
    assert(state.find_card(enemy)->state.bleeding == 0);
    assert(state.find_card(ally)->state.bleeding == 0);
    assert(can_use_order_source(state, kPlayerZero, bruxa));
}

}  // namespace

int main() {
    test_armorer_workshop_orders_hand_unit_then_banishes_stratagem();
    test_giant_centipede_gets_armor_equal_to_remaining_hand_count();
    test_giant_centipede_destroys_itself_when_hand_is_empty_after_play();
    test_katakan_order_spawns_ekimmara_on_same_row();
    test_garkain_and_bruxa_orders_apply_bleeding_to_enemy_unit();
    test_order_effects_do_not_fire_during_deploy();
    test_invalid_direct_order_target_is_rejected_before_effect_resolution();

    std::cout << "gwent_deck_a_basic_effects_tests: OK\n";
    return 0;
}
