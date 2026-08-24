#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/effect_registry.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/task_queue.hpp"

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

CardDefinition make_leader_definition(std::string id = "leader") {
    CardDefinition leader;
    leader.id = std::move(id);
    leader.name = "Leader";
    leader.card_type = CardType::Leader;
    return leader;
}

std::size_t event_index(const std::vector<EventRecord>& events, const std::string& name) {
    const auto it = std::find_if(events.begin(), events.end(), [&](const EventRecord& event) {
        return event.name == name;
    });
    assert(it != events.end());
    return static_cast<std::size_t>(std::distance(events.begin(), it));
}

void test_task_queue_is_fifo() {
    TaskQueue queue;
    queue.push_back(Task::pass(kPlayerZero));
    queue.push_back(Task::boost_card(kPlayerZero, 10, 2));

    auto first = queue.pop_front();
    auto second = queue.pop_front();
    auto third = queue.pop_front();

    assert(first.has_value());
    assert(second.has_value());
    assert(!third.has_value());
    assert(first->type == TaskType::Pass);
    assert(second->type == TaskType::BoostCard);
    assert(queue.empty());
}

void test_kernel_play_card_resolves_effect_then_waits_for_end_turn() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition unit = make_unit_definition("u_boost", "Boosting Unit", 5);
    unit.effect_ids.push_back("boost_self_by_2");
    const EntityId hand_card = state.add_card(unit, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    EffectRegistry registry;
    registry.register_handler("boost_self_by_2", [](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, 2));
    });

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, hand_card, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert(state.current_player_id == kPlayerZero);
    assert((state.location_of(hand_card) == Location{kPlayerZero, Zone::Melee, 0}));
    const RuntimeCard* card = state.find_card(hand_card);
    assert(card != nullptr);
    assert(card->state.power == 7);
    assert(state.board_score(kPlayerZero) == 7);

    event_index(result.events, "card_boosted");
    assert(result.next_decision.has_value());
    assert(result.next_decision->player_id == kPlayerZero);
    assert(std::find(result.next_decision->legal_actions.begin(), result.next_decision->legal_actions.end(), Action::end_turn(kPlayerZero)) != result.next_decision->legal_actions.end());

    KernelResult ended = apply_action_with_kernel(state, Action::end_turn(kPlayerZero), registry);
    assert(ended);
    assert(state.current_player_id == kPlayerOne);
    event_index(ended.events, "turn_advanced");
}

void test_kernel_effect_can_damage_and_destroy() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition source_def = make_unit_definition("u_damage", "Damage Unit", 4);
    source_def.effect_ids.push_back("damage_target_by_6");
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    CardDefinition target_def = make_unit_definition("enemy", "Enemy", 5);
    const EntityId target = state.add_card(target_def, kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    registry.register_handler("damage_target_by_6", [target](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::damage_card(call.actor_id, target, 6));
    });

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert((state.location_of(source) == Location{kPlayerZero, Zone::Melee, 0}));
    assert((state.location_of(target) == Location{kPlayerOne, Zone::Cemetery, 0}));
    assert(state.board_score(kPlayerOne) == 0);
    event_index(result.events, "card_damaged");
    event_index(result.events, "card_destroyed");
}

void test_kernel_stratagem_is_order_entity_and_banishes_after_use() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition stratagem = make_stratagem_definition("strat", "Tactical Advantage");
    const EntityId source = state.add_card(stratagem, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    KernelResult result = apply_action_with_kernel(state, Action::use_order(kPlayerZero, source));

    assert(result);
    assert((state.location_of(source) == Location{kPlayerZero, Zone::Banished, 0}));
    assert(state.board_score(kPlayerZero) == 0);
    assert(state.current_player_id == kPlayerZero);
    event_index(result.events, "stratagem_consumed");
}

void test_kernel_leader_effect_can_enqueue_tasks() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition leader = make_leader_definition("leader_boost");
    leader.effect_ids.push_back("leader_boost_unit");
    const EntityId leader_id = state.add_card(leader, kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    state.add_card(make_unit_definition("leader_followup", "Leader Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    CardDefinition unit = make_unit_definition("ally", "Ally", 3);
    const EntityId ally = state.add_card(unit, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    EffectRegistry registry;
    registry.register_handler("leader_boost_unit", [ally](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::boost_card(call.actor_id, ally, 4));
    });

    KernelResult result = apply_action_with_kernel(state, Action::use_leader(kPlayerZero, leader_id), registry);

    assert(result);
    assert(state.player(kPlayerZero).leader_used);
    assert(state.find_card(ally)->state.power == 7);
    assert(state.current_player_id == kPlayerZero);
    assert(state.turn_contexts[0].used_intra_turn_action);
    event_index(result.events, "leader_used");
    event_index(result.events, "card_boosted");
}

void test_intra_turn_action_without_hand_is_rejected_without_mutation() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId leader = state.add_card(make_leader_definition("no_hand_leader"), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});

    KernelResult result = apply_action_with_kernel(state, Action::use_leader(kPlayerZero, leader));
    assert(!result);
    assert(result.status == ActionStatus::IllegalAction);
    assert(!state.player(kPlayerZero).leader_used);
    assert(state.current_player_id == kPlayerZero);
}

void test_task_limit_failure_rolls_back_partial_action() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId hand_card = state.add_card(make_unit_definition("rollback_unit", "Rollback Unit", 5), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    KernelConfig config;
    config.max_tasks_per_action = 1;  // PlayCard mutates, then ResolveDeploy would be task #2.

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, hand_card, kPlayerZero, Zone::Melee, 0),
        config
    );

    assert(!result);
    assert(result.status == ActionStatus::TaskLimitExceeded);
    assert((state.location_of(hand_card) == Location{kPlayerZero, Zone::Hand, 0}));
    assert(state.player(kPlayerZero).row(Zone::Melee).empty());
    assert(!state.turn_contexts[0].consumed_hand_card);
    assert(event_index(result.events, "action_rolled_back") < result.events.size());
}

void test_end_turn_after_final_hand_card_becomes_hand_exhausted_pass() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition leader = make_leader_definition("followup_leader");
    const EntityId leader_id = state.add_card(leader, kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    const EntityId final_card = state.add_card(
        make_unit_definition("final_card", "Final Card", 5),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );

    KernelResult played = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, final_card, kPlayerZero, Zone::Melee, 0)
    );
    assert(played);
    assert(state.player(kPlayerZero).hand.empty());
    assert(!state.player(kPlayerZero).passed);
    assert(state.player(kPlayerZero).pass_reason == PassReason::None);
    assert(state.current_player_id == kPlayerZero);

    // Emptying the hand does not immediately pass: post-play leader/order
    // actions remain available until the player explicitly closes the window.
    assert(played.next_decision.has_value());
    const auto& actions = played.next_decision->legal_actions;
    assert(std::find(actions.begin(), actions.end(), Action::end_turn(kPlayerZero)) != actions.end());
    assert(std::find(actions.begin(), actions.end(), Action::use_leader(kPlayerZero, leader_id)) != actions.end());

    KernelResult ended = apply_action_with_kernel(state, Action::end_turn(kPlayerZero));
    assert(ended);
    assert(state.player(kPlayerZero).passed);
    assert(state.player(kPlayerZero).pass_reason == PassReason::HandExhausted);
    assert(state.current_player_id == kPlayerOne);
    event_index(ended.events, "hand_exhausted_pass");
}

void test_end_turn_with_cards_remaining_is_not_pass() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId first = state.add_card(
        make_unit_definition("first", "First", 3),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );
    state.add_card(
        make_unit_definition("remaining", "Remaining", 4),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 1}
    );

    KernelResult played = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, first, kPlayerZero, Zone::Melee, 0)
    );
    assert(played);
    assert(state.player(kPlayerZero).hand.size() == 1);

    KernelResult ended = apply_action_with_kernel(state, Action::end_turn(kPlayerZero));
    assert(ended);
    assert(!state.player(kPlayerZero).passed);
    assert(state.player(kPlayerZero).pass_reason == PassReason::None);
    assert(state.current_player_id == kPlayerOne);
    event_index(ended.events, "end_turn");
}

void test_kernel_mulligan_keeps_reducer_shape() {
    GameState state = make_running_state(kPlayerZero);
    state.current_player_id = kPlayerZero;
    state.round_starting_player_id = kPlayerZero;
    state.mulligan_player_id = kPlayerZero;
    state.player(kPlayerZero).mulligans_available = 1;

    CardDefinition hand_def = make_unit_definition("hand", "Hand", 1);
    CardDefinition deck_def = make_unit_definition("deck", "Deck", 2);
    const EntityId hand_card = state.add_card(hand_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId deck_card = state.add_card(deck_def, kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});

    KernelResult result = apply_action_with_kernel(state, Action::mulligan(kPlayerZero, hand_card));

    assert(result);
    assert(state.player(kPlayerZero).mulligans_available == 0);
    assert((state.location_of(deck_card) == Location{kPlayerZero, Zone::Hand, 0}));
    assert((state.location_of(hand_card) == Location{kPlayerZero, Zone::Deck, 0}));
    assert(state.current_player_id == kPlayerZero);
    event_index(result.events, "mulligan");
}

}  // namespace

int main() {
    test_task_queue_is_fifo();
    test_kernel_play_card_resolves_effect_then_waits_for_end_turn();
    test_kernel_effect_can_damage_and_destroy();
    test_kernel_stratagem_is_order_entity_and_banishes_after_use();
    test_kernel_leader_effect_can_enqueue_tasks();
    test_intra_turn_action_without_hand_is_rejected_without_mutation();
    test_task_limit_failure_rolls_back_partial_action();
    test_end_turn_after_final_hand_card_becomes_hand_exhausted_pass();
    test_end_turn_with_cards_remaining_is_not_pass();
    test_kernel_mulligan_keeps_reducer_shape();

    std::cout << "gwent_kernel_tests: OK\n";
    return 0;
}
