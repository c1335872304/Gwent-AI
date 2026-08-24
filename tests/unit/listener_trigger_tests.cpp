#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/effect_registry.hpp"
#include "gwent/engine/kernel.hpp"

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

ListenerId add_test_listener(
    GameState& state,
    std::string event,
    std::string handler,
    EntityId source,
    bool once = false,
    std::unordered_map<std::string, std::string> filters = {},
    bool disabled_by_lock = true
) {
    RuntimeListener listener;
    listener.listener_id = ++state.next_listener_id;
    listener.event = std::move(event);
    listener.handler = std::move(handler);
    listener.source_id = source;
    listener.once = once;
    listener.require_source = true;
    listener.source_must_be_on_board = true;
    listener.disabled_by_lock = disabled_by_lock;
    listener.filters = std::move(filters);
    const ListenerId id = listener.listener_id;
    state.listeners[id] = std::move(listener);
    return id;
}

std::size_t count_events(const std::vector<EventRecord>& events, const std::string& name) {
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const EventRecord& event) {
        return event.name == name;
    }));
}

void test_card_played_listener_fires_and_once_listener_is_removed() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId watcher = state.add_card(make_unit_definition("watcher", "Watcher", 3), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId hand_card = state.add_card(make_unit_definition("played", "Played", 4), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const ListenerId listener_id = add_test_listener(state, "card_played", "listener.boost_self", watcher, true);

    EffectRegistry registry;
    registry.register_handler("listener.boost_self", [](KernelContext& context, const EffectCall& call) {
        assert(call.event_name == "card_played");
        assert(call.event_source_entity_id != kInvalidEntityId);
        assert(call.listener_id.has_value());
        context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, 2, call.event_source_entity_id));
    });

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, hand_card, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert(state.find_card(watcher)->state.power == 5);
    assert(state.listeners.find(listener_id) == state.listeners.end());
    assert(count_events(result.events, "trigger:effect") == 1);
    assert(count_events(result.events, "listener_removed") == 1);
}

void test_turn_end_and_turn_start_listeners_fire_around_turn_transition() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId watcher = state.add_card(make_unit_definition("watcher", "Watcher", 3), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    add_test_listener(state, "turn_end", "listener.turn_end_boost", watcher, false);
    add_test_listener(state, "turn_start", "listener.turn_start_boost", watcher, false);

    EffectRegistry registry;
    registry.register_handler("listener.turn_end_boost", [](KernelContext& context, const EffectCall& call) {
        assert(call.event_name == "turn_end");
        assert(call.actor_id == kPlayerZero);
        context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, 1));
    });
    registry.register_handler("listener.turn_start_boost", [](KernelContext& context, const EffectCall& call) {
        assert(call.event_name == "turn_start");
        assert(call.actor_id == kPlayerOne);
        context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, 1));
    });

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);

    assert(result);
    assert(state.current_player_id == kPlayerOne);
    assert(state.find_card(watcher)->state.power == 5);
    assert(count_events(result.events, "trigger:effect") == 2);
}

void test_status_added_listener_gets_status_filter_and_event_target() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition source_def = make_unit_definition("bleeder", "Bleeder", 4);
    source_def.effect_ids.push_back("deploy:add_bleeding");
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId watcher = state.add_card(make_unit_definition("watcher", "Watcher", 2), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    add_test_listener(state, "status_added", "listener.status_seen", watcher, false, {{"status", "bleeding"}, {"target_id", std::to_string(enemy)}});

    EffectRegistry registry;
    registry.register_handler("add_bleeding", [enemy](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::add_status_card(call.actor_id, enemy, CardStatus::Bleeding, 3, call.source_entity_id));
    });
    registry.register_handler("listener.status_seen", [enemy](KernelContext& context, const EffectCall& call) {
        assert(call.event_name == "status_added");
        assert(call.event_target_entity_id == enemy);
        assert(call.reason == "bleeding");
        assert(call.amount == 3);
        context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, call.amount));
    });

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert(state.find_card(enemy)->state.bleeding == 3);
    assert(state.find_card(watcher)->state.power == 5);
    assert(count_events(result.events, "trigger:effect") == 1);
}

void test_runtime_listeners_fire_in_listener_id_order() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId watcher_a = state.add_card(make_unit_definition("watcher_a", "Watcher A", 2), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId watcher_b = state.add_card(make_unit_definition("watcher_b", "Watcher B", 2), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    const EntityId played = state.add_card(make_unit_definition("ordered_play", "Played", 3), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    RuntimeListener high;
    high.listener_id = 20;
    high.event = "card_played";
    high.handler = "listener.order.20";
    high.source_id = watcher_b;
    high.once = false;
    state.listeners[20] = high;

    RuntimeListener low;
    low.listener_id = 10;
    low.event = "card_played";
    low.handler = "listener.order.10";
    low.source_id = watcher_a;
    low.once = false;
    state.listeners[10] = low;
    state.next_listener_id = 20;

    EffectRegistry registry;
    registry.register_handler("listener.order.10", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::PlayCard, "listener_order_10", call.actor_id, call.source_entity_id});
    });
    registry.register_handler("listener.order.20", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::PlayCard, "listener_order_20", call.actor_id, call.source_entity_id});
    });

    KernelResult result = apply_action_with_kernel(state, Action::play_card(kPlayerZero, played, kPlayerZero, Zone::Melee, 0), registry);
    assert(result);

    auto index_of = [&](const std::string& name) {
        const auto it = std::find_if(result.events.begin(), result.events.end(), [&](const EventRecord& event) { return event.name == name; });
        assert(it != result.events.end());
        return std::distance(result.events.begin(), it);
    };
    assert(index_of("listener_order_10") < index_of("listener_order_20"));
}

void test_card_destroyed_listener_fires_and_missing_source_listener_is_ignored() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition source_def = make_unit_definition("destroyer", "Destroyer", 4);
    source_def.effect_ids.push_back("deploy:destroy_enemy");
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId watcher = state.add_card(make_unit_definition("watcher", "Watcher", 2), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId dead_watcher = state.add_card(make_unit_definition("dead_watcher", "Dead Watcher", 2), kPlayerZero, Location{kPlayerZero, Zone::Cemetery, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 1), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    add_test_listener(state, "card_destroyed", "listener.destroy_seen", watcher, false);
    add_test_listener(state, "card_destroyed", "listener.should_not_fire", dead_watcher, false);

    EffectRegistry registry;
    registry.register_handler("destroy_enemy", [enemy](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::damage_card(call.actor_id, enemy, 3, call.source_entity_id));
    });
    registry.register_handler("listener.destroy_seen", [enemy](KernelContext& context, const EffectCall& call) {
        assert(call.event_name == "card_destroyed");
        assert(call.event_target_entity_id == enemy);
        context.enqueue(Task::add_armor_card(call.actor_id, call.source_entity_id, 2));
    });
    registry.register_handler("listener.should_not_fire", [](KernelContext&, const EffectCall&) {
        assert(false && "cemetery listener source must not fire");
    });

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert((state.location_of(enemy) == Location{kPlayerOne, Zone::Cemetery, 0}));
    assert(state.find_card(watcher)->state.armor == 2);
    assert(count_events(result.events, "trigger:effect") == 1);
}

void test_trigger_dispatch_resumes_after_choice_and_reaches_definition_triggers() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId watcher_a = state.add_card(make_unit_definition("resume_a", "Resume A", 2), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId watcher_b = state.add_card(make_unit_definition("resume_b", "Resume B", 2), kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    const EntityId watcher_c = state.add_card(make_unit_definition("resume_c", "Resume C", 2), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    CardDefinition definition_watcher = make_unit_definition("resume_definition", "Resume Definition", 2);
    definition_watcher.effect_ids.push_back("card_played:definition.after_choice");
    state.add_card(definition_watcher, kPlayerZero, Location{kPlayerZero, Zone::Ranged, 1});
    const EntityId enemy = state.add_card(make_unit_definition("resume_enemy", "Resume Enemy", 4), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId played = state.add_card(make_unit_definition("resume_played", "Resume Played", 3), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    add_test_listener(state, "card_played", "listener.resume.a", watcher_a, false);
    add_test_listener(state, "card_played", "listener.resume.b", watcher_b, false);
    add_test_listener(state, "card_played", "listener.resume.c", watcher_c, false);

    EffectRegistry registry;
    registry.register_handler("listener.resume.a", [enemy](KernelContext& context, const EffectCall& call) {
        context.request_card_choice(call.actor_id, call.source_entity_id, "listener.resume.a.choice", "choose", {enemy});
    });
    registry.register_handler("listener.resume.a.choice", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::PlayCard, "listener_resume_choice_done", call.actor_id, call.source_entity_id});
    });
    registry.register_handler("listener.resume.b", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::PlayCard, "listener_resume_b", call.actor_id, call.source_entity_id});
    });
    registry.register_handler("listener.resume.c", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::PlayCard, "listener_resume_c", call.actor_id, call.source_entity_id});
    });
    registry.register_handler("definition.after_choice", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::PlayCard, "definition_resume_d", call.actor_id, call.source_entity_id});
    });

    KernelResult first = apply_action_with_kernel(state, Action::play_card(kPlayerZero, played, kPlayerZero, Zone::Melee, 0), registry);
    assert(first);
    assert(state.pending_choice.has_value());
    assert(count_events(first.events, "listener_resume_b") == 0);
    assert(count_events(first.events, "listener_resume_c") == 0);
    assert(count_events(first.events, "definition_resume_d") == 0);

    KernelResult resumed = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, watcher_a, enemy), registry);
    assert(resumed);
    assert(!state.pending_choice.has_value());
    assert(count_events(resumed.events, "listener_resume_choice_done") == 1);
    assert(count_events(resumed.events, "listener_resume_b") == 1);
    assert(count_events(resumed.events, "listener_resume_c") == 1);
    assert(count_events(resumed.events, "definition_resume_d") == 1);
}

void test_locked_sources_disable_normal_triggers_but_system_listener_can_ignore_lock() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId locked_runtime = state.add_card(make_unit_definition("locked_runtime", "Locked Runtime", 2), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.find_card(locked_runtime)->state.locked = true;

    CardDefinition locked_definition = make_unit_definition("locked_definition", "Locked Definition", 2);
    locked_definition.effect_ids.push_back("card_played:locked.definition.should_not_fire");
    const EntityId locked_def_id = state.add_card(locked_definition, kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.find_card(locked_def_id)->state.locked = true;

    const EntityId system_source = state.add_card(make_unit_definition("system_source", "System Source", 2), kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    state.find_card(system_source)->state.locked = true;
    const EntityId played = state.add_card(make_unit_definition("lock_test_played", "Played", 3), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    add_test_listener(state, "card_played", "locked.runtime.should_not_fire", locked_runtime, false);
    add_test_listener(state, "card_played", "system.ignore_lock", system_source, false, {}, false);

    EffectRegistry registry;
    registry.register_handler("locked.runtime.should_not_fire", [](KernelContext&, const EffectCall&) {
        assert(false && "locked runtime card text must not trigger");
    });
    registry.register_handler("locked.definition.should_not_fire", [](KernelContext&, const EffectCall&) {
        assert(false && "locked definition card text must not trigger");
    });
    registry.register_handler("system.ignore_lock", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::PlayCard, "system_ignore_lock_fired", call.actor_id, call.source_entity_id});
    });

    KernelResult result = apply_action_with_kernel(state, Action::play_card(kPlayerZero, played, kPlayerZero, Zone::Melee, 0), registry);
    assert(result);
    assert(count_events(result.events, "system_ignore_lock_fired") == 1);
}

void test_round_end_choice_resumes_finalize_and_next_round_transition() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId watcher = state.add_card(make_unit_definition("round_choice_watcher", "Round Choice Watcher", 2), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId enemy = state.add_card(make_unit_definition("round_choice_enemy", "Round Choice Enemy", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.player(kPlayerOne).passed = true;
    state.player(kPlayerOne).pass_reason = PassReason::Explicit;

    add_test_listener(state, "round_end", "round.choice.listener", watcher, true);

    EffectRegistry registry;
    registry.register_handler("round.choice.listener", [enemy](KernelContext& context, const EffectCall& call) {
        context.request_card_choice(call.actor_id, call.source_entity_id, "round.choice.resume", "round choice", {enemy});
    });
    registry.register_handler("round.choice.resume", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::FinalizeRoundEnd, "round_choice_resumed", call.actor_id, call.source_entity_id});
    });

    KernelResult first = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
    assert(first);
    assert(state.pending_choice.has_value());
    assert(state.round_no == 1);
    assert(state.player(kPlayerOne).round_wins == 1);

    KernelResult resumed = apply_action_with_kernel(
        state,
        Action::choose_card_target(kPlayerZero, watcher, enemy),
        registry
    );
    assert(resumed);
    assert(!state.pending_choice.has_value());
    assert(state.round_no == 2);
    assert(state.phase == MatchPhase::Playing);
    assert(state.status == MatchStatus::Running);
    assert(count_events(resumed.events, "round_choice_resumed") == 1);
}

}  // namespace

int main() {
    test_card_played_listener_fires_and_once_listener_is_removed();
    test_turn_end_and_turn_start_listeners_fire_around_turn_transition();
    test_status_added_listener_gets_status_filter_and_event_target();
    test_runtime_listeners_fire_in_listener_id_order();
    test_card_destroyed_listener_fires_and_missing_source_listener_is_ignored();
    test_trigger_dispatch_resumes_after_choice_and_reaches_definition_triggers();
    test_locked_sources_disable_normal_triggers_but_system_listener_can_ignore_lock();
    test_round_end_choice_resumes_finalize_and_next_round_transition();

    std::cout << "gwent_listener_trigger_tests: OK\n";
    return 0;
}
