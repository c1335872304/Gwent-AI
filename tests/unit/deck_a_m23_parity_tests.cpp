#include <cassert>
#include <iostream>
#include <string>

#include "gwent/cards/deck_a.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/row_effects.hpp"

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
    state.rng_state = 1;  // FastRng state chosen so randbelow(2) returns 1.
    return state;
}

RowEffect make_blood_moon(int trigger_player_id, int duration = 2) {
    RowEffect effect;
    effect.id = "blood_moon";
    effect.data["serial"] = "1";
    effect.data["duration"] = std::to_string(duration);
    effect.data["trigger_player_id"] = std::to_string(trigger_player_id);
    effect.data["bleeding_turns"] = "2";
    effect.data["damage"] = "2";
    return effect;
}

void test_blood_moon_applies_bleeding_and_decrements_duration_on_owner_turn_start() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId target = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.player(kPlayerOne).effects_on_row(Zone::Melee).push_back(make_blood_moon(kPlayerOne, 2));

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
    assert(result);
    assert(state.current_player_id == kPlayerOne);
    assert(state.find_card(target)->state.bleeding == 2);
    assert(state.player(kPlayerOne).effects_on_row(Zone::Melee).size() == 1);
    assert(state.player(kPlayerOne).effects_on_row(Zone::Melee).front().data.at("duration") == "1");
}

void test_blood_moon_damages_already_bleeding_unit_and_expires() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId target = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(target)->state.bleeding = 1;
    state.player(kPlayerOne).effects_on_row(Zone::Melee).push_back(make_blood_moon(kPlayerOne, 1));

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
    assert(result);
    assert(state.find_card(target)->state.power == 3);
    assert(state.find_card(target)->state.bleeding == 1);
    assert(state.player(kPlayerOne).effects_on_row(Zone::Melee).empty());
}

void test_blood_moon_two_damage_is_normal_damage_and_armor_absorbs_it() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId target = state.add_card(make_unit_definition("armored_enemy", "Armored Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(target)->state.bleeding = 1;
    state.find_card(target)->state.armor = 2;
    state.player(kPlayerOne).effects_on_row(Zone::Melee).push_back(make_blood_moon(kPlayerOne, 1));

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
    assert(result);
    // Blood Moon chose its 2-damage branch because the unit was already bleeding.
    // This is ordinary damage, not a Bleeding tick, so Armor absorbs it.
    assert(state.find_card(target)->state.power == 5);
    assert(state.find_card(target)->state.armor == 0);
    assert(state.find_card(target)->state.bleeding == 1);
}

void test_bleeding_tick_ignores_armor() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId target = state.add_card(make_unit_definition("bleeding_armored", "Bleeding Armored", 5, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.find_card(target)->state.bleeding = 1;
    state.find_card(target)->state.armor = 2;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
    assert(result);
    // Actual Bleeding trigger bypasses Armor.
    assert(state.find_card(target)->state.power == 4);
    assert(state.find_card(target)->state.armor == 2);
    assert(state.find_card(target)->state.bleeding == 0);
}

void test_blood_moon_addition_extends_existing_effect() {
    GameState state = make_running_state(kPlayerZero);
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "blood_moon", 2, kPlayerOne, {{"bleeding_turns", "2"}, {"damage", "2"}});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "blood_moon", 3, kPlayerOne, {{"bleeding_turns", "2"}, {"damage", "2"}});
    assert(state.player(kPlayerOne).effects_on_row(Zone::Melee).size() == 1);
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "blood_moon") == 5);
}

void test_different_weather_replaces_existing_weather() {
    GameState frost_over_blood = make_running_state(kPlayerZero);
    add_or_extend_row_effect(frost_over_blood, kPlayerOne, Zone::Melee, "blood_moon", 2, kPlayerOne, {{"damage", "2"}});
    add_or_extend_row_effect(frost_over_blood, kPlayerOne, Zone::Melee, "frost", 3, kPlayerOne, {{"damage", "2"}});
    assert(frost_over_blood.player(kPlayerOne).effects_on_row(Zone::Melee).size() == 1);
    assert(row_effect_duration(frost_over_blood, kPlayerOne, Zone::Melee, "blood_moon") == 0);
    assert(row_effect_duration(frost_over_blood, kPlayerOne, Zone::Melee, "frost") == 3);

    GameState blood_over_frost = make_running_state(kPlayerZero);
    add_or_extend_row_effect(blood_over_frost, kPlayerOne, Zone::Ranged, "frost", 2, kPlayerOne, {{"damage", "2"}});
    add_or_extend_row_effect(blood_over_frost, kPlayerOne, Zone::Ranged, "blood_moon", 4, kPlayerOne, {{"damage", "2"}});
    assert(blood_over_frost.player(kPlayerOne).effects_on_row(Zone::Ranged).size() == 1);
    assert(row_effect_duration(blood_over_frost, kPlayerOne, Zone::Ranged, "frost") == 0);
    assert(row_effect_duration(blood_over_frost, kPlayerOne, Zone::Ranged, "blood_moon") == 4);
}

void test_frost_damages_highest_unit_and_expires() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId strong = state.add_card(make_unit_definition("strong", "Strong", 7), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId weak = state.add_card(make_unit_definition("weak", "Weak", 3), kPlayerOne, Location{kPlayerOne, Zone::Melee, 1});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 1, kPlayerOne, {{"damage", "2"}});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::pass(kPlayerZero), registry));
    assert(state.find_card(strong)->state.power == 5);
    assert(state.find_card(weak)->state.power == 3);
    assert(state.player(kPlayerOne).effects_on_row(Zone::Melee).empty());
}

void test_frost_extends_existing_duration_and_uses_fast_rng_for_highest_tie() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId first = state.add_card(make_unit_definition("first", "First", 4), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});
    const EntityId second = state.add_card(make_unit_definition("second", "Second", 4), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 1});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Ranged, "frost", 1, kPlayerOne, {{"damage", "2"}});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Ranged, "frost", 2, kPlayerOne, {{"damage", "2"}});
    assert(state.player(kPlayerOne).effects_on_row(Zone::Ranged).size() == 1);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 3);

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::pass(kPlayerZero), registry));
    assert(state.find_card(first)->state.power == 4);
    assert(state.find_card(second)->state.power == 2);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 2);
}

void test_eredin_increases_frost_damage_only_with_dominance() {
    GameState state = make_running_state(kPlayerZero);
    state.add_card(deck_a::make_eredin_breacc_glas_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId target = state.add_card(make_unit_definition("target", "Target", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 1, kPlayerOne, {{"damage", "2"}});
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::pass(kPlayerZero), registry));
    assert(state.find_card(target)->state.power == 2);
}

void test_eredin_frost_bonus_counts_equal_strongest_as_dominance() {
    GameState state = make_running_state(kPlayerZero);
    state.add_card(
        deck_a::make_eredin_breacc_glas_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    const EntityId target = state.add_card(
        make_unit_definition("equal_target", "Equal Target", 7),
        kPlayerOne,
        Location{kPlayerOne, Zone::Melee, 0}
    );
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 1, kPlayerOne, {{"damage", "2"}});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::pass(kPlayerZero), registry));

    // Project-wide has_dominance() treats 7 >= 7 as Dominance, therefore
    // Eredin must increase frost from 2 to 3 here as well.
    assert(state.find_card(target)->state.power == 4);
}

void test_multiple_eredins_stack_frost_damage_bonus() {
    GameState state = make_running_state(kPlayerZero);
    state.add_card(
        deck_a::make_eredin_breacc_glas_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    state.add_card(
        deck_a::make_eredin_breacc_glas_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Ranged, 0}
    );
    const EntityId target = state.add_card(
        make_unit_definition("double_eredin_target", "Double Eredin Target", 7),
        kPlayerOne,
        Location{kPlayerOne, Zone::Melee, 0}
    );
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 1, kPlayerOne, {{"damage", "2"}});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::pass(kPlayerZero), registry));

    // Base Frost 2 + two active Eredins = 4 damage.
    assert(state.find_card(target)->state.power == 3);
}

void test_locked_eredin_does_not_increase_frost_damage() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId eredin = state.add_card(
        deck_a::make_eredin_breacc_glas_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    state.find_card(eredin)->state.locked = true;
    const EntityId target = state.add_card(
        make_unit_definition("locked_eredin_target", "Locked Eredin Target", 5),
        kPlayerOne,
        Location{kPlayerOne, Zone::Melee, 0}
    );
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 1, kPlayerOne, {{"damage", "2"}});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::pass(kPlayerZero), registry));

    assert(state.find_card(target)->state.power == 3);
}

void test_golden_child_history_excludes_frost_absorbed_by_armor() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    const EntityId child = state.add_card(deck_a::make_caranthir_golden_child_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId target = state.add_card(make_unit_definition("armored", "Armored", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(target)->state.armor = 2;
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 1, kPlayerOne, {{"damage", "2"}});
    assert(apply_action_with_kernel(state, Action::pass(kPlayerZero), registry));
    assert(state.find_card(target)->state.power == 5);
    assert(state.find_card(child)->memory["frost_damage_4"] == "0");
}

void test_unseen_elder_turn_end_uses_fast_rng_for_non_bleeding_target() {
    GameState state = make_running_state(kPlayerZero);
    state.add_card(deck_a::make_unseen_elder_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId first = state.add_card(make_unit_definition("enemy1", "Enemy 1", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId second = state.add_card(make_unit_definition("enemy2", "Enemy 2", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 1});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
    assert(result);
    assert(state.find_card(first)->state.bleeding == 0);
    assert(state.find_card(second)->state.bleeding == 1);
    assert(state.find_card(second)->state.power == 4);
}

void test_blood_scent_final_charge_spawns_on_fast_rng_row() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId leader = state.add_card(deck_a::make_blood_scent_definition(), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    state.add_card(make_unit_definition("m23_leader_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.player(kPlayerZero).leader = leader;
    state.find_card(leader)->state.order_charges = 1;
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult pending = apply_action_with_kernel(state, Action::use_leader(kPlayerZero, leader), registry);
    assert(pending);
    assert(state.pending_choice.has_value());
    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, leader, enemy), registry);
    assert(resolved);
    assert(state.find_card(enemy)->state.bleeding == 3);
    assert(state.player(kPlayerZero).row(Zone::Melee).empty());
    assert(state.player(kPlayerZero).row(Zone::Ranged).size() == 1);
    const EntityId spawned = state.player(kPlayerZero).row(Zone::Ranged).front();
    assert(state.find_card(spawned)->definition->id == std::string(deck_a::kEkimmaraId));
}

}  // namespace

int main() {
    test_blood_moon_applies_bleeding_and_decrements_duration_on_owner_turn_start();
    test_blood_moon_damages_already_bleeding_unit_and_expires();
    test_blood_moon_two_damage_is_normal_damage_and_armor_absorbs_it();
    test_bleeding_tick_ignores_armor();
    test_blood_moon_addition_extends_existing_effect();
    test_different_weather_replaces_existing_weather();
    test_frost_damages_highest_unit_and_expires();
    test_frost_extends_existing_duration_and_uses_fast_rng_for_highest_tie();
    test_eredin_increases_frost_damage_only_with_dominance();
    test_eredin_frost_bonus_counts_equal_strongest_as_dominance();
    test_multiple_eredins_stack_frost_damage_bonus();
    test_locked_eredin_does_not_increase_frost_damage();
    test_golden_child_history_excludes_frost_absorbed_by_armor();
    test_unseen_elder_turn_end_uses_fast_rng_for_non_bleeding_target();
    test_blood_scent_final_charge_spawns_on_fast_rng_row();
    std::cout << "gwent_deck_a_m23_parity_tests: OK\n";
    return 0;
}
