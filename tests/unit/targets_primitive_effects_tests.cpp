#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/primitive_effects.hpp"
#include "gwent/engine/targets.hpp"

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

void test_selector_filters_enemy_units_and_orders_by_strength() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition weak = make_unit_definition("weak", "Weak", 2);
    CardDefinition strong = make_unit_definition("strong", "Strong", 8);
    CardDefinition ally = make_unit_definition("ally", "Ally", 5);
    CardDefinition strat = make_stratagem_definition("strat", "Tactical Advantage");

    const EntityId enemy_weak = state.add_card(weak, kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId enemy_strong = state.add_card(strong, kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});
    const EntityId ally_id = state.add_card(ally, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId strat_id = state.add_card(strat, kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});

    TargetSelector selector;
    selector.side = RelativeSide::Opponent;
    selector.zone_scope = ZoneScope::BattleRows;
    selector.card_kind = TargetCardKind::HasPower;
    selector.ordering = TargetOrdering::StrongestFirst;

    const auto selected = select_card_targets(state, TargetContext{kPlayerZero}, selector);
    assert(selected.size() == 2);
    assert(selected[0] == enemy_strong);
    assert(selected[1] == enemy_weak);
    assert(std::find(selected.begin(), selected.end(), ally_id) == selected.end());
    assert(std::find(selected.begin(), selected.end(), strat_id) == selected.end());

    TargetSelector order_selector;
    order_selector.side = RelativeSide::Actor;
    order_selector.zone_scope = ZoneScope::BattleRows;
    order_selector.card_kind = TargetCardKind::OrderSource;
    const auto orders = select_card_targets(state, TargetContext{kPlayerZero}, order_selector);
    assert(orders.size() == 1);
    assert(orders[0] == strat_id);
}

void test_action_card_target_validation() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition source_def = make_unit_definition("source", "Source", 4);
    CardDefinition enemy_def = make_unit_definition("enemy", "Enemy", 5);
    CardDefinition ally_def = make_unit_definition("ally", "Ally", 5);
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId enemy = state.add_card(enemy_def, kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId ally = state.add_card(ally_def, kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});

    TargetSelector enemy_unit;
    enemy_unit.side = RelativeSide::Opponent;
    enemy_unit.zone_scope = ZoneScope::BattleRows;
    enemy_unit.card_kind = TargetCardKind::HasPower;

    assert(action_card_target_is_valid(state, TargetContext{kPlayerZero, source, ActionTarget::card(enemy)}, enemy_unit));
    assert(!action_card_target_is_valid(state, TargetContext{kPlayerZero, source, ActionTarget::card(ally)}, enemy_unit));
    assert(!action_card_target_is_valid(state, TargetContext{kPlayerZero, source, ActionTarget::row(kPlayerOne, Zone::Melee)}, enemy_unit));
}

void test_primitive_damage_effect_uses_typed_target() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition source_def = make_unit_definition("u_damage", "Damage Deploy", 4);
    source_def.effect_ids.push_back("damage_clicked_enemy_by_3");
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    CardDefinition enemy_def = make_unit_definition("enemy", "Enemy", 5);
    const EntityId enemy = state.add_card(enemy_def, kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    TargetSelector target;
    target.side = RelativeSide::Opponent;
    target.zone_scope = ZoneScope::BattleRows;
    target.card_kind = TargetCardKind::HasPower;
    target.require_not_immune = true;
    target.max_targets = 1;

    EffectRegistry registry;
    register_primitive_effect(registry, "damage_clicked_enemy_by_3", PrimitiveEffectSpec::damage(target, 3));

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert(state.find_card(enemy)->state.power == 2);
    assert(state.board_score(kPlayerOne) == 2);
    assert(count_events(result.events, "card_damaged") == 1);
}

void test_primitive_boost_allies_does_not_touch_stratagem() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition source_def = make_unit_definition("u_boost", "Boost Deploy", 4);
    source_def.effect_ids.push_back("boost_all_allied_units_by_2");
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    CardDefinition ally_def = make_unit_definition("ally", "Ally", 3);
    const EntityId ally = state.add_card(ally_def, kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    CardDefinition strat = make_stratagem_definition("strat", "Tactical Advantage");
    const EntityId strat_id = state.add_card(strat, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    TargetSelector allies;
    allies.side = RelativeSide::Actor;
    allies.zone_scope = ZoneScope::BattleRows;
    allies.card_kind = TargetCardKind::HasPower;

    EffectRegistry registry;
    register_primitive_effect(registry, "boost_all_allied_units_by_2", PrimitiveEffectSpec::boost(allies, 2));

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert(state.find_card(source)->state.power == 6);
    assert(state.find_card(ally)->state.power == 5);
    assert(state.find_card(strat_id)->state.power == 0);
    assert(state.board_score(kPlayerZero) == 11);
    assert(count_events(result.events, "card_boosted") == 2);
}

void test_primitive_spawn_effect_resolves_relative_row() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition source_def = make_unit_definition("u_spawn", "Spawn Deploy", 4);
    source_def.effect_ids.push_back("spawn_drone_enemy_ranged");
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    CardDefinition drone = make_unit_definition("drone", "Drone", 1);
    drone.is_token = true;

    RowDestinationSpec destination;
    destination.side = RelativeSide::Opponent;
    destination.row = Zone::Ranged;

    EffectRegistry registry;
    register_primitive_effect(registry, "spawn_drone_enemy_ranged", PrimitiveEffectSpec::spawn(drone, destination));

    KernelResult result = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    );

    assert(result);
    assert(state.player(kPlayerOne).row(Zone::Ranged).size() == 1);
    const EntityId spawned = state.player(kPlayerOne).row(Zone::Ranged).front();
    assert(state.find_card(spawned)->definition->id == "drone");
    assert(state.find_card(spawned)->owner_id == kPlayerZero);
    assert(state.find_card(spawned)->controller_id == kPlayerOne);
    assert(state.board_score(kPlayerOne) == 1);
    assert(count_events(result.events, "card_spawned") == 1);
}

void test_normal_any_selector_excludes_leader_unless_explicitly_requested() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId leader = state.add_card(
        make_leader_definition("target_leader", "Target Leader", Faction::Monsters),
        kPlayerZero,
        Location{kPlayerZero, Zone::Leader, 0}
    );
    const EntityId unit = state.add_card(
        make_unit_definition("target_unit", "Target Unit", 4),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );

    TargetSelector normal;
    normal.side = RelativeSide::Any;
    normal.zone_scope = ZoneScope::Any;
    normal.card_kind = TargetCardKind::Any;
    const auto normal_targets = select_card_targets(state, TargetContext{kPlayerZero}, normal);
    assert(std::find(normal_targets.begin(), normal_targets.end(), unit) != normal_targets.end());
    assert(std::find(normal_targets.begin(), normal_targets.end(), leader) == normal_targets.end());

    TargetSelector explicit_leader;
    explicit_leader.side = RelativeSide::Actor;
    explicit_leader.zone_scope = ZoneScope::Leader;
    explicit_leader.card_kind = TargetCardKind::Leader;
    const auto leader_targets = select_card_targets(state, TargetContext{kPlayerZero}, explicit_leader);
    assert(leader_targets.size() == 1);
    assert(leader_targets.front() == leader);
}

}  // namespace

int main() {
    test_selector_filters_enemy_units_and_orders_by_strength();
    test_action_card_target_validation();
    test_primitive_damage_effect_uses_typed_target();
    test_primitive_boost_allies_does_not_touch_stratagem();
    test_primitive_spawn_effect_resolves_relative_row();
    test_normal_any_selector_excludes_leader_unless_explicitly_requested();

    std::cout << "gwent_targets_primitive_effects_tests: OK\n";
    return 0;
}
