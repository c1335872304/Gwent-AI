#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/primitives.hpp"
#include "gwent/engine/reducer.hpp"
#include "gwent/engine/task.hpp"

using namespace gwent;

namespace {

GameState make_running_state(PlayerId current = kPlayerZero) {
    GameState state;
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.engine_phase = EnginePhase::ActionWindow;
    state.round_no = 1;
    state.turn_no = 1;
    state.starting_player_id = current;
    state.round_starting_player_id = current;
    state.current_player_id = current;
    state.match_config["standard_deck_game"] = "true";
    state.match_config["hand_limit"] = "10";
    state.match_config["between_round_draw"] = "3";
    state.match_config["round_mulligan_base"] = "2";
    return state;
}

void test_typed_event_keeps_string_protocol() {
    EventRecord damaged{TaskType::DamageCard, "card_damaged", kPlayerZero, 1, 2, 3, {}};
    assert(damaged.kind == EventKind::CardDamaged);
    assert(damaged.name == "card_damaged");
    assert(to_string(damaged.kind) == damaged.name);

    EventRecord custom{TaskType::Pass, "deck_a.custom_trace", kPlayerZero, 1};
    assert(custom.kind == EventKind::Unknown);
    assert(custom.name == "deck_a.custom_trace");

    assert(event_kind_from_name("status_added") == EventKind::StatusAdded);
    assert(event_kind_from_name("round_finished") == EventKind::RoundFinished);
    assert(event_kind_from_name("not_a_protocol_event") == EventKind::Unknown);
}

void test_counter_primitive_clamps_and_sets() {
    GameState state;
    const EntityId id = state.add_card(
        make_unit_definition("counter_target", "Counter Target", 4),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    RuntimeCard* card = state.find_card(id);
    assert(card != nullptr);
    card->state.cooldown = 2;
    card->state.countdown = 1;

    assert(primitive_modify_card_counter(state, id, CardCounterField::Cooldown, -1).value() == 1);
    assert(card->state.cooldown == 1);
    assert(primitive_modify_card_counter(state, id, CardCounterField::Cooldown, -99).value() == 0);
    assert(card->state.cooldown == 0);
    assert(primitive_modify_card_counter(state, id, CardCounterField::Countdown, 5, CardCounterMode::Set).value() == 5);
    assert(card->state.countdown == 5);
    assert(!primitive_modify_card_counter(state, 9999, CardCounterField::Timer, -1).has_value());
}

void test_counter_task_contract() {
    const Task task = Task::modify_card_counter(
        kPlayerZero,
        7,
        CardCounterField::Cooldown,
        -1,
        CardCounterMode::Add,
        7,
        TaskType::TickTurnCounters,
        "cooldown_ticked"
    );
    assert(task.type == TaskType::ModifyCardCounter);
    assert(task.target_entity_id == 7);
    assert(task.counter_field == CardCounterField::Cooldown);
    assert(task.counter_mode == CardCounterMode::Add);
    assert(task.amount == -1);
    assert(task.event_task_type == TaskType::TickTurnCounters);
    assert(task.reason == "cooldown_ticked");
}

void test_turn_counter_task_preserves_event_order_and_kind() {
    GameState state = make_running_state(kPlayerOne);
    const EntityId counter = state.add_card(
        make_unit_definition("v8_counter", "V8 Counter", 4),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    RuntimeCard* card = state.find_card(counter);
    assert(card != nullptr);
    card->state.timer = 2;
    card->state.cooldown = 1;

    const EntityId p1_hand = state.add_card(
        make_unit_definition("v8_p1", "V8 P1", 1),
        kPlayerOne,
        Location{kPlayerOne, Zone::Hand, 0}
    );
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerOne, p1_hand, kPlayerOne, Zone::Melee, 0)));
    const KernelResult result = apply_action_with_kernel(state, Action::end_turn(kPlayerOne));
    assert(result);
    assert(state.find_card(counter)->state.timer == 1);
    assert(state.find_card(counter)->state.cooldown == 0);

    auto timer = std::find_if(result.events.begin(), result.events.end(), [](const EventRecord& event) {
        return event.kind == EventKind::TimerTicked;
    });
    auto cooldown = std::find_if(result.events.begin(), result.events.end(), [](const EventRecord& event) {
        return event.kind == EventKind::CooldownTicked;
    });
    assert(timer != result.events.end());
    assert(cooldown != result.events.end());
    assert(timer < cooldown);
    assert(timer->name == "timer_ticked" && timer->amount == 1);
    assert(cooldown->name == "cooldown_ticked" && cooldown->amount == 0);
    assert(timer->task_type == TaskType::TickTurnCounters);
    assert(cooldown->task_type == TaskType::TickTurnCounters);
}

void test_legacy_reducer_api_version_is_explicit() {
    static_assert(GWENT_LEGACY_REDUCER_API_VERSION == 1);
}

}  // namespace

int main() {
    test_typed_event_keeps_string_protocol();
    test_counter_primitive_clamps_and_sets();
    test_counter_task_contract();
    test_turn_counter_task_preserves_event_order_and_kind();
    test_legacy_reducer_api_version_is_explicit();
    std::cout << "v8_finalization_tests passed\n";
    return 0;
}
