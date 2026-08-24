#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/effect_registry.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/engine/targets.hpp"
#include "gwent/engine/task.hpp"

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

std::size_t event_index(const std::vector<EventRecord>& events, const std::string& name) {
    const auto it = std::find_if(events.begin(), events.end(), [&](const EventRecord& event) {
        return event.name == name;
    });
    assert(it != events.end());
    return static_cast<std::size_t>(std::distance(events.begin(), it));
}

TargetSelector enemy_unit_selector() {
    TargetSelector selector;
    selector.side = RelativeSide::Opponent;
    selector.zone_scope = ZoneScope::BattleRows;
    selector.card_kind = TargetCardKind::HasPower;
    selector.require_not_immune = true;
    selector.max_targets = 1;
    return selector;
}

void test_special_card_uses_no_row_and_goes_to_cemetery_after_resolution() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition ally_def = make_unit_definition("ally", "Ally", 4);
    const EntityId ally = state.add_card(ally_def, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    CardDefinition special = make_special_definition("special_boost", "Special Boost");
    special.effect_ids.push_back("special:boost_ally");
    const EntityId special_id = state.add_card(special, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    const std::vector<Action> actions = legal_play_card_actions(state, kPlayerZero);
    assert(std::find(actions.begin(), actions.end(), Action::play_card(kPlayerZero, special_id)) != actions.end());
    assert(std::find(actions.begin(), actions.end(), Action::play_card(kPlayerZero, special_id, kPlayerZero, Zone::Melee, 0)) == actions.end());

    EffectRegistry registry;
    registry.register_handler("boost_ally", [ally](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::boost_card(call.actor_id, ally, 2, call.source_entity_id));
    });

    KernelResult result = apply_action_with_kernel(state, Action::play_card(kPlayerZero, special_id), registry);

    assert(result);
    assert((state.location_of(special_id) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert(state.player(kPlayerZero).row(Zone::Melee).size() == 1);
    assert(state.board_score(kPlayerZero) == 6);
    assert(state.find_card(ally)->state.power == 6);
    assert(state.current_player_id == kPlayerZero);

    const std::size_t played_i = event_index(result.events, "special_played");
    const std::size_t boost_i = event_index(result.events, "card_boosted");
    const std::size_t grave_i = event_index(result.events, "special_moved_to_cemetery");
    assert(played_i < boost_i);
    assert(boost_i < grave_i);
}

void test_special_pending_choice_resumes_then_moves_special_to_cemetery() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition special = make_special_definition("special_bleed", "Special Bleed");
    special.effect_ids.push_back("special:bleed_enemy");
    const EntityId special_id = state.add_card(special, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    CardDefinition enemy_def = make_unit_definition("enemy", "Enemy", 5);
    const EntityId enemy = state.add_card(enemy_def, kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    registry.register_handler("bleed_enemy", [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        const TargetContext target_context{call.actor_id, call.source_entity_id, call.target};
        if (call.target.kind != ActionTargetKind::Card) {
            std::vector<EntityId> legal_targets = select_card_targets(context.state, target_context, selector);
            context.request_card_choice(call.actor_id, call.source_entity_id, call.effect_id, "选择 1 个敌军单位重伤 2", std::move(legal_targets));
            return;
        }
        assert(action_card_target_is_valid(context.state, target_context, selector));
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 2, call.source_entity_id));
    });

    KernelResult first = apply_action_with_kernel(state, Action::play_card(kPlayerZero, special_id), registry);
    assert(first);
    assert(state.pending_choice.has_value());
    assert((state.location_of(special_id) == Location{kPlayerZero, Zone::Stay, 0}));
    assert(first.next_decision.has_value());
    assert(first.next_decision->player_id == kPlayerZero);

    KernelResult second = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, special_id, enemy), registry);
    assert(second);
    assert(!state.pending_choice.has_value());
    assert((state.location_of(special_id) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert(state.find_card(enemy)->state.bleeding == 2);
    assert(state.current_player_id == kPlayerZero);
    event_index(second.events, "status_added");
    event_index(second.events, "special_moved_to_cemetery");
}

void test_summon_banish_discard_consume_and_drain_tasks() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition source_def = make_unit_definition("source", "Source", 3);
    source_def.effect_ids.push_back("deploy:do_primitives");
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    CardDefinition summon_def = make_unit_definition("summoned", "Summoned", 2);
    const EntityId summoned = state.add_card(summon_def, kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});

    CardDefinition discard_def = make_unit_definition("discard", "Discard", 1);
    const EntityId discarded = state.add_card(discard_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});

    CardDefinition banish_def = make_unit_definition("banish", "Banish", 1);
    const EntityId banished = state.add_card(banish_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 2});

    CardDefinition food_def = make_unit_definition("food", "Food", 4);
    const EntityId food = state.add_card(food_def, kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});

    CardDefinition enemy_def = make_unit_definition("enemy", "Enemy", 5);
    const EntityId enemy = state.add_card(enemy_def, kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    registry.register_handler("do_primitives", [summoned, discarded, banished, food, enemy](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::summon_card(call.actor_id, summoned, Location{call.actor_id, Zone::Melee, 99}, call.source_entity_id));
        context.enqueue(Task::discard_card(call.actor_id, discarded, call.source_entity_id));
        context.enqueue(Task::banish_card(call.actor_id, banished, call.source_entity_id));
        context.enqueue(Task::consume_card(call.actor_id, call.source_entity_id, food));
        context.enqueue(Task::drain_card(call.actor_id, call.source_entity_id, enemy, 3));
    });

    KernelResult result = apply_action_with_kernel(state, Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0), registry);

    assert(result);
    assert((state.location_of(summoned) == Location{kPlayerZero, Zone::Melee, 1}));
    assert((state.location_of(discarded) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert((state.location_of(banished) == Location{kPlayerZero, Zone::Banished, 0}));
    assert((state.location_of(food) == Location{kPlayerZero, Zone::Cemetery, 1}));
    assert((state.location_of(enemy) == Location{kPlayerOne, Zone::Melee, 0}));

    const RuntimeCard* source_card = state.find_card(source);
    assert(source_card != nullptr);
    assert(source_card->state.power == 10);  // base 3 + consume 4 + drain 3
    assert(state.find_card(enemy)->state.power == 2);

    event_index(result.events, "card_summoned");
    event_index(result.events, "card_discarded");
    event_index(result.events, "card_banished");
    event_index(result.events, "card_consumed");
    event_index(result.events, "card_drained");
}

}  // namespace

int main() {
    test_special_card_uses_no_row_and_goes_to_cemetery_after_resolution();
    test_special_pending_choice_resumes_then_moves_special_to_cemetery();
    test_summon_banish_discard_consume_and_drain_tasks();

    std::cout << "gwent_special_primitives_tests: OK\n";
    return 0;
}
