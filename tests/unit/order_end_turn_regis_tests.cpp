#include <cassert>
#include <cstddef>

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

std::size_t count_events(const std::vector<EventRecord>& events, std::string_view name) {
    std::size_t count = 0;
    for (const EventRecord& event : events) {
        if (event.name == name) {
            ++count;
        }
    }
    return count;
}

bool has_action_type(const std::vector<Action>& actions, ActionType type) {
    for (const Action& action : actions) {
        if (action.type == type) {
            return true;
        }
    }
    return false;
}

void test_stratagem_order_is_removed_after_instruction_resolves() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId stratagem = state.add_card(deck_a::make_armorer_workshop_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId hand_unit = state.add_card(make_unit_definition("hand_vampire", "Hand Vampire", 4, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult order = apply_action_with_kernel(
        state,
        Action::use_order(kPlayerZero, stratagem, ActionTarget::card(hand_unit)),
        registry
    );

    assert(order);
    assert(state.current_player_id == kPlayerZero);
    assert((state.location_of(stratagem) == Location{kPlayerZero, Zone::Banished, 0}));
    assert(state.find_card(hand_unit)->state.power == 7);
    assert(state.find_card(hand_unit)->state.armor == 2);
    assert(!can_use_order_source(state, kPlayerZero, stratagem));
    assert(order.next_decision.has_value());
    assert(!has_action_type(order.next_decision->legal_actions, ActionType::EndTurn));
    assert(count_events(order.events, "order_waiting_for_end_turn") == 0);
    assert(count_events(order.events, "stratagem_consumed") == 1);
}

void test_regis_reborn_grows_from_hand_on_owners_turn_end_when_enemy_is_bleeding() {
    GameState state = make_running_state(kPlayerOne);
    const EntityId regis = state.add_card(deck_a::make_regis_reborn_definition(), kPlayerOne, Location{kPlayerOne, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 6, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.find_card(enemy)->state.bleeding = 2;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    const RuntimeCard* before = state.find_card(regis);
    assert(before->state.base_power == 1);
    assert(before->state.power == 1);

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerOne), registry);
    assert(result);

    const RuntimeCard* after = state.find_card(regis);
    assert(after->state.base_power == 2);
    assert(after->state.power == 2);
    assert(count_events(result.events, "regis_reborn_base_power_increased") == 1);
}

void test_regis_reborn_deploy_drains_enemy_target() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId regis = state.add_card(deck_a::make_regis_reborn_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 6, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(enemy)->state.armor = 2;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, regis, kPlayerZero, Zone::Melee, 0),
        registry
    );
    assert(played);
    assert(state.pending_choice.has_value());

    KernelResult target = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, regis, enemy), registry);
    assert(target);
    assert(state.find_card(enemy)->state.power == 3);
    assert(state.find_card(enemy)->state.armor == 2);
    assert(state.find_card(regis)->state.power == 4);
}

}  // namespace

int main() {
    test_stratagem_order_is_removed_after_instruction_resolves();
    test_regis_reborn_grows_from_hand_on_owners_turn_end_when_enemy_is_bleeding();
    test_regis_reborn_deploy_drains_enemy_target();
    return 0;
}
