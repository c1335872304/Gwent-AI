#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "gwent/cards/deck_a.hpp"
#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/engine/primitives.hpp"
#include "gwent/engine/row_effects.hpp"
#include "gwent/game/setup.hpp"

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

void test_deck_a_catalog_and_deck_spec_are_complete() {
    const CardCatalog catalog = deck_a::make_catalog();
    assert(catalog.size() == 47);
    assert(catalog.contains(deck_a::kBloodScentId));
    assert(catalog.contains(deck_a::kArmorerWorkshopId));
    assert(catalog.contains(deck_a::kWildHuntRiderId));
    assert(catalog.contains(deck_a::kWildHuntNavigatorId));
    assert(catalog.contains(deck_a::kWildHuntWarriorId));
    assert(catalog.contains(deck_a::kWildHuntHoundId));
    assert(catalog.contains(deck_a::kWildHuntBruiserId));
    assert(catalog.contains(deck_a::kNaglfarCrewId));
    assert(catalog.contains(deck_a::kNaglfarTaskmasterId));
    assert(catalog.contains(deck_a::kAenElleAristocratId));
    assert(catalog.contains(deck_a::kAenElleSlaveTraderId));
    assert(catalog.contains(deck_a::kEkimmaraId));

    const CardDefinition& leader = catalog.at(deck_a::kBloodScentId);
    assert(leader.card_type == CardType::Leader);
    assert(leader.metadata.at("charges") == "3");

    const CardDefinition& stratagem = catalog.at(deck_a::kArmorerWorkshopId);
    assert(stratagem.card_type == CardType::Stratagem);
    assert(!stratagem.has_power());

    const CardDefinition& garkain = catalog.at(deck_a::kGarkainId);
    assert(garkain.card_type == CardType::Unit);
    assert(garkain.base_power == 5);
    assert(garkain.metadata.at("cooldown") == "2");
    assert(garkain.has_category("吸血鬼"));

    const DeckSpec deck = deck_a::make_deck_spec();
    const int main_deck_count = std::accumulate(deck.cards.begin(), deck.cards.end(), 0, [](int total, const DeckCardSpec& spec) {
        return total + spec.count;
    });
    assert(deck.leader.id == std::string(deck_a::kBloodScentId));
    assert(deck.stratagem.id == std::string(deck_a::kArmorerWorkshopId));
    assert(main_deck_count == 25);
}

void test_standard_setup_works_with_full_deck_a_spec() {
    GameState state;
    MatchSetupConfig config;
    config.shuffle_decks = false;
    config.starting_player_id = kPlayerZero;

    const DeckSpec deck = deck_a::make_deck_spec();
    const OpeningSetupResult setup = setup_standard_match(state, deck, deck, config);

    assert(setup.starting_player_id == kPlayerZero);
    assert(state.status == MatchStatus::Running);
    assert(state.phase == MatchPhase::Playing);
    assert(state.player(kPlayerZero).hand.size() == 10);
    assert(state.player(kPlayerZero).deck.size() == 15);
    assert(state.player(kPlayerOne).hand.size() == 10);
    assert(state.player(kPlayerOne).deck.size() == 15);
    assert(state.player(kPlayerZero).mulligans_available == 3);
    assert(state.player(kPlayerOne).mulligans_available == 2);

    const EntityId leader_id = state.player(kPlayerZero).leader.value();
    const RuntimeCard* leader = state.find_card(leader_id);
    assert(leader != nullptr);
    assert(leader->definition->id == std::string(deck_a::kBloodScentId));
    assert(leader->state.order_charges == 3);

    const RuntimeCard* stratagem = state.find_card(setup.stratagem_entity_id);
    assert(stratagem != nullptr);
    assert(stratagem->definition->card_type == CardType::Stratagem);
    assert((state.location_of(setup.stratagem_entity_id) == Location{kPlayerZero, Zone::Melee, 0}));
}

void test_bruxa_deploy_requests_target_then_applies_bleeding() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 6, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult pending = apply_action_with_kernel(state, Action::play_card(kPlayerZero, bruxa, kPlayerZero, Zone::Melee, 0), registry);
    assert(pending);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->effect_id == std::string(deck_a::kBruxaDeployEffect));
    assert(state.find_card(enemy)->state.bleeding == 0);

    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, bruxa, enemy), registry);
    assert(resolved);
    assert(!state.pending_choice.has_value());
    assert(state.find_card(enemy)->state.bleeding == 3);
}

void test_blood_scent_uses_three_charges_and_spawns_after_final_charge() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId leader = state.add_card(deck_a::make_blood_scent_definition(), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    state.add_card(make_unit_definition("blood_scent_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.player(kPlayerZero).leader = leader;
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 10, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    KernelConfig config;
    config.leader_action_consumes_turn = false;

    for (int use_index = 0; use_index < 3; ++use_index) {
        KernelResult pending = apply_action_with_kernel(state, Action::use_leader(kPlayerZero, leader), registry, config);
        assert(pending);
        assert(state.pending_choice.has_value());
        KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, leader, enemy), registry, config);
        assert(resolved);
    }

    assert(state.find_card(enemy)->state.bleeding == 9);
    assert(state.find_card(leader)->state.order_charges == 0);
    assert(state.player(kPlayerZero).leader_used);
    assert(!can_use_leader(state, kPlayerZero));
    assert(state.player(kPlayerZero).row(Zone::Melee).size() == 1);
    const EntityId spawned = state.player(kPlayerZero).row(Zone::Melee).front();
    assert(state.find_card(spawned)->definition->id == std::string(deck_a::kEkimmaraId));
}

void test_cooldown_order_is_blocked_then_reduced_by_vampire_played_listener() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId garkain = state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 8, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    KernelConfig config;
    config.order_action_consumes_turn = false;

    KernelResult play_garkain = apply_action_with_kernel(state, Action::play_card(kPlayerZero, garkain, kPlayerZero, Zone::Melee, 0), registry, config);
    assert(play_garkain);
    assert(count_events(play_garkain.events, "trigger:effect") == 0);
    state.current_player_id = kPlayerZero;

    KernelResult pending_order = apply_action_with_kernel(state, Action::use_order(kPlayerZero, garkain), registry, config);
    assert(pending_order);
    assert(state.pending_choice.has_value());
    KernelResult resolved_order = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, garkain, enemy), registry, config);
    assert(resolved_order);
    assert(state.find_card(enemy)->state.bleeding == 2);
    assert(state.find_card(garkain)->state.cooldown == 2);
    assert(!can_use_order_source(state, kPlayerZero, garkain));

    state.turn_contexts[static_cast<std::size_t>(kPlayerZero)] = {};
    state.engine_phase = EnginePhase::ActionWindow;
    KernelResult play_vampire = apply_action_with_kernel(state, Action::play_card(kPlayerZero, bruxa, kPlayerZero, Zone::Ranged, 0), registry, config);
    assert(play_vampire);
    assert(state.find_card(garkain)->state.cooldown == 1);
    assert(state.pending_choice.has_value());
}

void test_fleder_boosts_once_when_enemy_receives_bleeding() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId fleder = state.add_card(deck_a::make_fleder_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 8, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult play_fleder = apply_action_with_kernel(state, Action::play_card(kPlayerZero, fleder, kPlayerZero, Zone::Melee, 0), registry);
    assert(play_fleder);
    state.current_player_id = kPlayerZero;
    state.turn_contexts[static_cast<std::size_t>(kPlayerZero)] = {};
    state.engine_phase = EnginePhase::ActionWindow;
    state.find_card(fleder)->state.countdown = 1;

    KernelResult pending = apply_action_with_kernel(state, Action::play_card(kPlayerZero, bruxa, kPlayerZero, Zone::Ranged, 0), registry);
    assert(pending);
    assert(state.pending_choice.has_value());
    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, bruxa, enemy), registry);
    assert(resolved);

    assert(state.find_card(enemy)->state.bleeding == 3);
    assert(state.find_card(fleder)->state.power == 7);
    assert(state.find_card(fleder)->state.countdown == 0);
}



void test_locked_fleder_listener_does_not_boost_or_consume_countdown() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId fleder = state.add_card(deck_a::make_fleder_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId enemy = state.add_card(make_unit_definition("locked_fleder_enemy", "Enemy", 8, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, fleder, kPlayerZero, Zone::Melee, 0), registry));
    state.current_player_id = kPlayerZero;
    state.turn_contexts[static_cast<std::size_t>(kPlayerZero)] = {};
    state.engine_phase = EnginePhase::ActionWindow;
    state.find_card(fleder)->state.countdown = 1;
    state.find_card(fleder)->state.locked = true;
    const int before_power = state.find_card(fleder)->state.power;

    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, bruxa, kPlayerZero, Zone::Ranged, 0), registry));
    assert(state.pending_choice.has_value());
    assert(apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, bruxa, enemy), registry));
    assert(state.find_card(enemy)->state.bleeding == 3);
    assert(state.find_card(fleder)->state.power == before_power);
    assert(state.find_card(fleder)->state.countdown == 1);
}

void test_locked_garkain_listener_does_not_reduce_cooldown() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId garkain = state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    state.add_card(make_unit_definition("locked_garkain_enemy", "Enemy", 8, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, garkain, kPlayerZero, Zone::Melee, 0), registry));
    state.current_player_id = kPlayerZero;
    state.turn_contexts[static_cast<std::size_t>(kPlayerZero)] = {};
    state.engine_phase = EnginePhase::ActionWindow;
    state.find_card(garkain)->state.cooldown = 2;
    state.find_card(garkain)->state.locked = true;

    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, bruxa, kPlayerZero, Zone::Ranged, 0), registry));
    assert(state.find_card(garkain)->state.cooldown == 2);
}

void test_garkain_order_is_melee_row_only() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId ranged_garkain = state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    const EntityId melee_garkain = state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    assert(!can_use_order_source(state, kPlayerZero, ranged_garkain));
    assert(can_use_order_source(state, kPlayerZero, melee_garkain));
}

void test_feast_of_blood_purifies_damages_and_bleeds_when_friendly_vampire_exists() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId feast = state.add_card(deck_a::make_feast_of_blood_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 6, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(enemy)->state.vitality = 2;
    state.find_card(enemy)->state.shield = true;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, feast), registry);
    assert(played);
    assert(state.pending_choice.has_value());

    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, feast, enemy), registry);
    assert(resolved);
    assert(!state.pending_choice.has_value());
    assert((state.location_of(feast) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert(state.find_card(enemy)->state.vitality == 0);
    assert(!state.find_card(enemy)->state.shield);
    assert(state.find_card(enemy)->state.power == 3);
    assert(state.find_card(enemy)->state.bleeding == 3);
}

void test_feast_of_blood_stops_followup_when_damage_kills_same_target() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId feast = state.add_card(deck_a::make_feast_of_blood_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId enemy = state.add_card(make_unit_definition("lethal_enemy", "Lethal Enemy", 3, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, feast), registry);
    assert(played);
    assert(state.pending_choice.has_value());

    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, feast, enemy), registry);
    assert(resolved);
    assert(!state.pending_choice.has_value());
    assert(state.location_of(enemy).has_value());
    assert(state.location_of(enemy)->zone == Zone::Cemetery);
    // The same target died from Feast's damage, so its later Bleeding step has no target.
    assert(state.find_card(enemy)->state.bleeding == 0);
}


void test_ozzrel_melee_consumes_enemy_cemetery_unit() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId ozzrel = state.add_card(deck_a::make_ozzrel_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId corpse = state.add_card(make_unit_definition("enemy_corpse", "Enemy Corpse", 7, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Cemetery, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, ozzrel, kPlayerZero, Zone::Melee, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->effect_id == std::string(deck_a::kOzzrelDeployEffect));

    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, ozzrel, corpse), registry);
    assert(resolved);
    assert(!state.pending_choice.has_value());
    assert(state.find_card(ozzrel)->state.power == 8);
    assert((state.location_of(corpse) == Location{kPlayerOne, Zone::Banished, 0}));
}

void test_ozzrel_ranged_consumes_own_cemetery_unit() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId ozzrel = state.add_card(deck_a::make_ozzrel_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId corpse = state.add_card(make_unit_definition("own_corpse", "Own Corpse", 6, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Cemetery, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, ozzrel, kPlayerZero, Zone::Ranged, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());

    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, ozzrel, corpse), registry);
    assert(resolved);
    assert(state.find_card(ozzrel)->state.power == 7);
    assert((state.location_of(corpse) == Location{kPlayerZero, Zone::Banished, 0}));
}

void test_imlerith_can_be_placed_on_either_row_but_only_melee_deploys() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId imlerith = state.add_card(deck_a::make_imlerith_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId drawn_unit = state.add_card(make_unit_definition("drawn", "Drawn Unit", 5, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});

    std::vector<Action> legal = legal_play_card_actions(state, kPlayerZero);
    assert(legal.size() == 1);
    assert(std::find(legal.begin(), legal.end(), Action::play_card(kPlayerZero, imlerith)) != legal.end());
    const auto imlerith_rows = legal_row_targets_for_card(state, kPlayerZero, *state.find_card(imlerith));
    assert(std::find(imlerith_rows.begin(), imlerith_rows.end(), ActionTarget::row(kPlayerZero, Zone::Melee)) != imlerith_rows.end());
    assert(std::find(imlerith_rows.begin(), imlerith_rows.end(), ActionTarget::row(kPlayerZero, Zone::Ranged)) != imlerith_rows.end());

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, imlerith, kPlayerZero, Zone::Melee, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    assert((state.location_of(drawn_unit) == Location{kPlayerZero, Zone::Hand, 0}));

    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, imlerith, drawn_unit), registry);
    assert(resolved);
    assert(state.find_card(imlerith)->state.power == 8);
    assert((state.location_of(drawn_unit) == Location{kPlayerZero, Zone::Cemetery, 0}));

    GameState ranged_state = make_running_state(kPlayerZero);
    const EntityId ranged_imlerith = ranged_state.add_card(deck_a::make_imlerith_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId ranged_deck_card = ranged_state.add_card(make_unit_definition("ranged_drawn", "Ranged Drawn", 5, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    KernelResult ranged_played = apply_action_with_kernel(ranged_state, Action::play_card(kPlayerZero, ranged_imlerith, kPlayerZero, Zone::Ranged, 0), registry);
    assert(ranged_played);
    assert(!ranged_state.pending_choice.has_value());
    assert((ranged_state.location_of(ranged_imlerith) == Location{kPlayerZero, Zone::Ranged, 0}));
    assert((ranged_state.location_of(ranged_deck_card) == Location{kPlayerZero, Zone::Deck, 0}));
    assert(ranged_state.find_card(ranged_imlerith)->state.power == 3);
}

void test_verena_blocks_boost_on_bleeding_enemy_units() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId verena = state.add_card(deck_a::make_verena_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 6, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, verena, kPlayerZero, Zone::Melee, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, verena, enemy), registry);
    assert(resolved);
    assert(state.find_card(enemy)->state.bleeding == 4);
    assert(state.find_card(enemy)->memory.count("boost_blocked_by:" + std::to_string(verena)) == 1);

    const int before = state.find_card(enemy)->state.power;
    assert(!primitive_boost_card(state, enemy, 3));
    assert(state.find_card(enemy)->state.power == before);

    state.find_card(verena)->state.locked = true;
    assert(primitive_boost_card(state, enemy, 3));
    assert(state.find_card(enemy)->state.power == before + 3);
}

void test_unseen_elder_turn_end_bleeds_non_bleeding_enemy_and_ticks_devotion() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId elder = state.add_card(deck_a::make_unseen_elder_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId already_bleeding = state.add_card(make_unit_definition("bleeding_enemy", "Bleeding Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId fresh_enemy = state.add_card(make_unit_definition("fresh_enemy", "Fresh Enemy", 6, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});
    state.find_card(already_bleeding)->state.bleeding = 2;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
    assert(result);
    assert(count_events(result.events, "unseen_elder_bleeding_applied") == 1);
    assert(count_events(result.events, "unseen_elder_devotion_bleeding_ticked") >= 1);
    // The newly selected target receives Bleeding (2) first, then that same
    // Bleeding is triggered once by Devotion: 6 -> 5 power, 2 -> 1 Bleeding.
    assert(state.find_card(fresh_enemy)->state.power == 5);
    assert(state.find_card(fresh_enemy)->state.bleeding == 1);
    assert(state.find_card(already_bleeding)->state.power == 4);
    assert(state.find_card(already_bleeding)->state.bleeding == 1);
    assert(state.find_card(elder)->state.power == 7);
}


void test_unseen_elder_veil_blocks_fresh_bleeding_and_devotion_tick() {
    GameState state = make_running_state(kPlayerZero);
    state.add_card(deck_a::make_unseen_elder_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId already_bleeding = state.add_card(make_unit_definition("bleeding_enemy_veil_test", "Bleeding Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId veiled_enemy = state.add_card(make_unit_definition("veiled_enemy", "Veiled Enemy", 6, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});
    state.find_card(already_bleeding)->state.bleeding = 2;
    state.find_card(veiled_enemy)->state.veil = true;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
    assert(result);

    // The random Bleeding application may select the Veiled unit, but Veil
    // must make it fizzle completely: no Bleeding and no phantom 1 damage.
    assert(state.find_card(veiled_enemy)->state.bleeding == 0);
    assert(state.find_card(veiled_enemy)->state.power == 6);

    // Existing Bleeding units still receive the normal Devotion tick.
    assert(state.find_card(already_bleeding)->state.bleeding == 1);
    assert(state.find_card(already_bleeding)->state.power == 4);
}


void test_giant_centipede_destroys_itself_when_armor_breaks() {
    GameState state = make_running_state(kPlayerOne);
    const EntityId centipede = state.add_card(deck_a::make_giant_centipede_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.find_card(centipede)->state.armor = 2;
    const CardDefinition damage_spell = [] {
        CardDefinition d = make_special_definition("test_damage_2", "Test Damage 2", Faction::Monsters);
        d.effect_ids.push_back("special:test.damage2");
        return d;
    }();
    const EntityId spell = state.add_card(damage_spell, kPlayerOne, Location{kPlayerOne, Zone::Hand, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    registry.register_handler("test.damage2", [centipede](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::damage_card(call.actor_id, centipede, 2, call.source_entity_id));
    });

    KernelResult result = apply_action_with_kernel(state, Action::play_card(kPlayerOne, spell), registry);
    assert(result);
    assert((state.location_of(centipede) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert(count_events(result.events, "card_destroyed") == 1);
}

void test_lord_riptide_can_be_placed_on_either_row_but_only_melee_clashes_and_gains_hand_armor_on_turn_end() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId riptide = state.add_card(deck_a::make_lord_riptide_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    std::vector<Action> legal = legal_play_card_actions(state, kPlayerZero);
    assert(legal.size() == 1);
    assert(std::find(legal.begin(), legal.end(), Action::play_card(kPlayerZero, riptide)) != legal.end());
    const auto riptide_rows = legal_row_targets_for_card(state, kPlayerZero, *state.find_card(riptide));
    assert(std::find(riptide_rows.begin(), riptide_rows.end(), ActionTarget::row(kPlayerZero, Zone::Melee)) != riptide_rows.end());
    assert(std::find(riptide_rows.begin(), riptide_rows.end(), ActionTarget::row(kPlayerZero, Zone::Ranged)) != riptide_rows.end());

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, riptide, kPlayerZero, Zone::Melee, 0), registry);
    assert(played);
    assert(state.find_card(riptide)->state.power == 6);
    assert((state.location_of(enemy) == Location{kPlayerOne, Zone::Cemetery, 0}));

    GameState hand_state = make_running_state(kPlayerZero);
    const EntityId hand_riptide = hand_state.add_card(deck_a::make_lord_riptide_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    KernelResult ended = apply_action_with_kernel(hand_state, Action::pass(kPlayerZero), registry);
    assert(ended);
    assert(state.find_card(riptide)->state.armor == 0);
    assert(hand_state.find_card(hand_riptide)->state.armor == 2);

    // Might requires a 10+ power unit on each allied row. Test the card-specific
    // turn-end handler directly so one owner turn produces exactly one Armor.
    GameState might_state = make_running_state(kPlayerZero);
    const EntityId might_riptide = might_state.add_card(deck_a::make_lord_riptide_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    might_state.add_card(make_unit_definition("might_melee", "Might Melee", 10), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    might_state.add_card(make_unit_definition("might_ranged", "Might Ranged", 10), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    TaskQueue might_queue;
    std::vector<EventRecord> might_events;
    KernelContext might_context{might_state, might_queue, might_events};
    assert(registry.invoke(might_context, EffectCall{deck_a::kLordRiptideHandEndEffect, kPlayerZero, might_riptide}));
    const auto armor_task = might_queue.pop_front();
    assert(armor_task.has_value());
    assert(armor_task->type == TaskType::AddArmorCard);
    assert(armor_task->target_entity_id == might_riptide);
    assert(armor_task->amount == 1);

    GameState partial_might = make_running_state(kPlayerZero);
    const EntityId partial_riptide = partial_might.add_card(deck_a::make_lord_riptide_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    partial_might.add_card(make_unit_definition("partial_melee", "Partial Melee", 10), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    partial_might.add_card(make_unit_definition("partial_ranged", "Partial Ranged", 9), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    TaskQueue partial_queue;
    KernelContext partial_context{partial_might, partial_queue, might_events};
    assert(registry.invoke(partial_context, EffectCall{deck_a::kLordRiptideHandEndEffect, kPlayerZero, partial_riptide}));
    assert(partial_queue.empty());

    GameState ranged_state = make_running_state(kPlayerZero);
    const EntityId ranged_riptide = ranged_state.add_card(deck_a::make_lord_riptide_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId ranged_enemy = ranged_state.add_card(make_unit_definition("ranged_enemy", "Ranged Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    KernelResult ranged_played = apply_action_with_kernel(ranged_state, Action::play_card(kPlayerZero, ranged_riptide, kPlayerZero, Zone::Ranged, 0), registry);
    assert(ranged_played);
    assert((ranged_state.location_of(ranged_riptide) == Location{kPlayerZero, Zone::Ranged, 0}));
    assert((ranged_state.location_of(ranged_enemy) == Location{kPlayerOne, Zone::Melee, 0}));
    assert(ranged_state.find_card(ranged_riptide)->state.power == 9);
    assert(ranged_state.find_card(ranged_enemy)->state.power == 5);
}

void test_lord_riptide_auto_target_includes_immunity_and_randomizes_highest_ties() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    // Immunity only blocks player-selected targeting. Riptide automatically
    // chooses a highest-power enemy, so an immune highest unit must still be
    // clashed.
    GameState immune_state = make_running_state(kPlayerZero);
    const EntityId immune_riptide = immune_state.add_card(
        deck_a::make_lord_riptide_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );
    const EntityId immune_highest = immune_state.add_card(
        make_unit_definition("immune_highest", "Immune Highest", 8, Faction::Monsters),
        kPlayerOne,
        Location{kPlayerOne, Zone::Melee, 0}
    );
    immune_state.find_card(immune_highest)->state.immune = true;
    const EntityId normal_lower = immune_state.add_card(
        make_unit_definition("normal_lower", "Normal Lower", 7, Faction::Monsters),
        kPlayerOne,
        Location{kPlayerOne, Zone::Ranged, 0}
    );
    assert(apply_action_with_kernel(
        immune_state,
        Action::play_card(kPlayerZero, immune_riptide, kPlayerZero, Zone::Melee, 0),
        registry
    ));
    assert(immune_state.find_card(normal_lower)->state.power == 7);
    assert(immune_state.location_of(immune_highest)->zone == Zone::Cemetery);

    // A tie among highest-power enemies is not a leftmost/first-card rule.
    // Seed 1 is a fixture where FastRng randbelow(2) chooses index 1.
    GameState tie_state = make_running_state(kPlayerZero);
    tie_state.rng_state = 1;
    const EntityId tie_riptide = tie_state.add_card(
        deck_a::make_lord_riptide_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );
    const EntityId first = tie_state.add_card(
        make_unit_definition("tie_first", "Tie First", 5, Faction::Monsters),
        kPlayerOne,
        Location{kPlayerOne, Zone::Melee, 0}
    );
    const EntityId second = tie_state.add_card(
        make_unit_definition("tie_second", "Tie Second", 5, Faction::Monsters),
        kPlayerOne,
        Location{kPlayerOne, Zone::Melee, 1}
    );
    assert(apply_action_with_kernel(
        tie_state,
        Action::play_card(kPlayerZero, tie_riptide, kPlayerZero, Zone::Melee, 0),
        registry
    ));
    assert(tie_state.find_card(first)->state.power == 5);
    assert(tie_state.location_of(second)->zone == Zone::Cemetery);
}

void test_dettlaff_order_damages_bleeding_enemy_and_deathblow_spawns_ekimmara() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId dettlaff = state.add_card(deck_a::make_dettlaff_aep_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("dettlaff_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("bleeding_enemy", "Bleeding Enemy", 1, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(enemy)->state.bleeding = 2;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    KernelConfig config;
    config.order_action_consumes_turn = false;

    KernelResult pending = apply_action_with_kernel(state, Action::use_order(kPlayerZero, dettlaff), registry, config);
    assert(pending);
    assert(state.pending_choice.has_value());
    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, dettlaff, enemy), registry, config);
    assert(resolved);
    assert(!state.pending_choice.has_value());
    assert((state.location_of(enemy) == Location{kPlayerOne, Zone::Cemetery, 0}));
    assert(state.player(kPlayerZero).row(Zone::Melee).size() == 2);
    const EntityId spawned = state.player(kPlayerZero).row(Zone::Melee).back();
    assert(state.find_card(spawned)->definition->id == std::string(deck_a::kEkimmaraId));
    assert(state.find_card(dettlaff)->state.cooldown == 1);
}

void test_dettlaff_deploy_adds_blood_moon_row_effect_data() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId dettlaff = state.add_card(deck_a::make_dettlaff_aep_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, dettlaff, kPlayerZero, Zone::Melee, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(state.pending_choice->legal_row_targets.size() == 2);

    KernelResult chosen = apply_action_with_kernel(state, Action::choose_row_target(kPlayerZero, dettlaff, kPlayerOne, Zone::Ranged), registry);
    assert(chosen);
    assert(!state.pending_choice.has_value());
    assert(state.player(kPlayerOne).effects_on_row(Zone::Melee).empty());
    const auto& effects = state.player(kPlayerOne).effects_on_row(Zone::Ranged);
    assert(effects.size() == 1);
    assert(effects.front().id == "blood_moon");
    assert(effects.front().data.at("duration") == "3");
}

void test_bloodscented_predator_bonded_uses_target_base_power() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId first_predator = state.add_card(deck_a::make_bloodscented_predator_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    (void)first_predator;
    const EntityId predator = state.add_card(deck_a::make_bloodscented_predator_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 7, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(enemy)->state.power = 4;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, predator, kPlayerZero, Zone::Ranged, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, predator, enemy), registry);
    assert(resolved);
    assert(state.find_card(enemy)->state.bleeding == 7);
}

void test_wild_hunt_rider_summons_same_cards_from_deck_under_dominance() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId rider = state.add_card(deck_a::make_wild_hunt_rider_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId rider_in_deck = state.add_card(deck_a::make_wild_hunt_rider_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    state.add_card(make_unit_definition("big", "Big Ally", 6, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::play_card(kPlayerZero, rider, kPlayerZero, Zone::Melee, 0), registry);
    assert(result);
    assert((state.location_of(rider) == Location{kPlayerZero, Zone::Melee, 0}));
    assert((state.location_of(rider_in_deck) == Location{kPlayerZero, Zone::Melee, 1}));
    assert(state.player(kPlayerZero).deck.empty());
}


void test_aen_elle_conqueror_veil_blocks_bleeding_until_purified() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId conqueror = state.add_card(deck_a::make_aen_elle_conqueror_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    assert(state.find_card(conqueror)->state.veil);

    const EntityId leader = state.add_card(deck_a::make_blood_scent_definition(), kPlayerOne, Location{kPlayerOne, Zone::Leader, 0});
    state.player(kPlayerOne).leader = leader;
    state.current_player_id = kPlayerOne;
    state.add_card(make_unit_definition("p1_leader_followup", "Followup", 1), kPlayerOne, Location{kPlayerOne, Zone::Hand, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult pending = apply_action_with_kernel(state, Action::use_leader(kPlayerOne, leader), registry);
    assert(pending);
    assert(state.pending_choice.has_value());
    KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerOne, leader, conqueror), registry);
    assert(resolved);
    assert(state.find_card(conqueror)->state.bleeding == 0);
    assert(state.find_card(conqueror)->state.veil);

    KernelResult purified = apply_action_with_kernel(state, Action{ActionType::Pass, kPlayerOne}, registry);
    (void)purified;
    primitive_purify_card(state, conqueror);
    assert(!state.find_card(conqueror)->state.veil);
    assert(primitive_add_status(state, conqueror, CardStatus::Bleeding, 2));
    assert(state.find_card(conqueror)->state.bleeding == 2);
}

void test_wild_hunt_rider_summons_each_matching_deck_copy_once() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId rider = state.add_card(deck_a::make_wild_hunt_rider_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId rider_a = state.add_card(deck_a::make_wild_hunt_rider_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    const EntityId rider_b = state.add_card(deck_a::make_wild_hunt_rider_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 1});
    state.add_card(make_unit_definition("big", "Big Ally", 9, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult result = apply_action_with_kernel(state, Action::play_card(kPlayerZero, rider, kPlayerZero, Zone::Melee, 0), registry);
    assert(result);
    assert((state.location_of(rider) == Location{kPlayerZero, Zone::Melee, 0}));
    assert((state.location_of(rider_a) == Location{kPlayerZero, Zone::Melee, 1}));
    assert((state.location_of(rider_b) == Location{kPlayerZero, Zone::Melee, 2}));
    assert(state.player(kPlayerZero).row(Zone::Melee).size() == 3);
    assert(state.player(kPlayerZero).deck.empty());
}

void test_wild_hunt_hound_boosts_on_owners_turn_end_only_with_dominance() {
    const auto hound_power_after_pass = [](int ally_power, int enemy_power) {
        GameState state = make_running_state(kPlayerZero);
        const EntityId hound = state.add_card(deck_a::make_wild_hunt_hound_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
        state.add_card(make_unit_definition("hound_ally", "Hound Ally", ally_power), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
        state.add_card(make_unit_definition("hound_enemy", "Hound Enemy", enemy_power), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

        EffectRegistry registry;
        deck_a::register_deck_a_effects(registry);
        KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry);
        assert(result);
        return state.find_card(hound)->state.power;
    };

    assert(hound_power_after_pass(7, 6) == 5);
    assert(hound_power_after_pass(5, 8) == 4);
}

void test_wild_hunt_warrior_damages_and_adds_frost_only_with_dominance() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId warrior = state.add_card(deck_a::make_wild_hunt_warrior_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("dominant", "Dominant", 8), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 5), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, warrior, kPlayerZero, Zone::Melee, 0), registry));
    assert(state.pending_choice.has_value());
    assert(apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, warrior, enemy), registry));
    assert(state.find_card(enemy)->state.power == 3);
    const auto& frost = state.player(kPlayerOne).effects_on_row(Zone::Ranged);
    assert(frost.size() == 1);
    assert(frost.front().id == "frost");
    assert(frost.front().data.at("duration") == "1");
    assert(state.turn_contexts[0].opponent_rows_frosted_this_turn[row_index(Zone::Ranged)]);

    GameState no_dominance = make_running_state(kPlayerZero);
    const EntityId second_warrior = no_dominance.add_card(deck_a::make_wild_hunt_warrior_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId stronger_enemy = no_dominance.add_card(make_unit_definition("stronger", "Stronger", 9), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    assert(apply_action_with_kernel(no_dominance, Action::play_card(kPlayerZero, second_warrior, kPlayerZero, Zone::Melee, 0), registry));
    assert(apply_action_with_kernel(no_dominance, Action::choose_card_target(kPlayerZero, second_warrior, stronger_enemy), registry));
    assert(no_dominance.find_card(stronger_enemy)->state.power == 7);
    assert(no_dominance.player(kPlayerOne).effects_on_row(Zone::Melee).empty());
}

void test_naglfar_taskmaster_targets_only_enemies_without_dominance() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId taskmaster = state.add_card(deck_a::make_naglfar_taskmaster_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId ally = state.add_card(make_unit_definition("taskmaster_ally", "Taskmaster Ally", 4), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId enemy = state.add_card(make_unit_definition("taskmaster_enemy", "Taskmaster Enemy", 9), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(ally)->state.locked = true;
    state.find_card(enemy)->state.locked = true;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, taskmaster, kPlayerZero, Zone::Ranged, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->legal_card_targets == std::vector<EntityId>{enemy});
    KernelResult chosen = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, taskmaster, enemy), registry);
    assert(chosen);
    assert(!state.find_card(enemy)->state.locked);
    assert(state.find_card(ally)->state.locked);
}

void test_naglfar_crew_deploys_two_turns_of_frost_on_chosen_enemy_row() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId crew = state.add_card(deck_a::make_naglfar_crew_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, crew, kPlayerZero, Zone::Melee, 0), registry));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->effect_id == std::string(deck_a::kNaglfarCrewDeployEffect));
    assert(apply_action_with_kernel(state, Action::choose_row_target(kPlayerZero, crew, kPlayerOne, Zone::Ranged), registry));
    assert(!state.pending_choice.has_value());
    assert(state.player(kPlayerOne).effects_on_row(Zone::Melee).empty());
    const auto& frost = state.player(kPlayerOne).effects_on_row(Zone::Ranged);
    assert(frost.size() == 1);
    assert(frost.front().id == "frost");
    assert(frost.front().data.at("duration") == "2");
    assert(state.turn_contexts[0].opponent_rows_frosted_this_turn[row_index(Zone::Ranged)]);
    assert(state.find_card(crew)->state.power == 1);
}

void test_wild_hunt_bruiser_moves_then_damages_only_on_frost_destination() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    GameState frosted = make_running_state(kPlayerZero);
    const EntityId bruiser = frosted.add_card(deck_a::make_wild_hunt_bruiser_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    frosted.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId frosted_enemy = frosted.add_card(make_unit_definition("frosted_enemy", "Frosted Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    add_or_extend_row_effect(frosted, kPlayerOne, Zone::Ranged, "frost", 2, kPlayerOne, {{"damage", "2"}});
    assert(apply_action_with_kernel(frosted, Action::play_card(kPlayerZero, bruiser, kPlayerZero, Zone::Melee, 0), registry));
    assert(frosted.pending_choice.has_value());
    assert(frosted.pending_choice->effect_id == std::string(deck_a::kWildHuntBruiserDeployEffect));
    assert(apply_action_with_kernel(frosted, Action::choose_card_target(kPlayerZero, bruiser, frosted_enemy), registry));
    assert((frosted.location_of(frosted_enemy) == Location{kPlayerOne, Zone::Ranged, 0}));
    assert(frosted.find_card(frosted_enemy)->state.power == 4);

    GameState clear = make_running_state(kPlayerZero);
    const EntityId second_bruiser = clear.add_card(deck_a::make_wild_hunt_bruiser_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    clear.add_card(make_unit_definition("followup2", "Followup 2", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId clear_enemy = clear.add_card(make_unit_definition("clear_enemy", "Clear Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});
    assert(apply_action_with_kernel(clear, Action::play_card(kPlayerZero, second_bruiser, kPlayerZero, Zone::Melee, 0), registry));
    assert(apply_action_with_kernel(clear, Action::choose_card_target(kPlayerZero, second_bruiser, clear_enemy), registry));
    assert((clear.location_of(clear_enemy) == Location{kPlayerOne, Zone::Melee, 0}));
    assert(clear.find_card(clear_enemy)->state.power == 6);

    GameState blocked_frost = make_running_state(kPlayerZero);
    const EntityId blocked_bruiser = blocked_frost.add_card(deck_a::make_wild_hunt_bruiser_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    blocked_frost.add_card(make_unit_definition("blocked_followup", "Blocked Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId blocked_enemy = blocked_frost.add_card(make_unit_definition("blocked_enemy", "Blocked Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    for (int index = 0; index < 9; ++index) {
        blocked_frost.add_card(make_unit_definition("blocked_row_" + std::to_string(index), "Blocked Row", 1),
            kPlayerOne, Location{kPlayerOne, Zone::Ranged, static_cast<std::size_t>(index)});
    }
    add_or_extend_row_effect(blocked_frost, kPlayerOne, Zone::Melee, "frost", 2, kPlayerOne, {{"damage", "2"}});
    assert(apply_action_with_kernel(blocked_frost, Action::play_card(kPlayerZero, blocked_bruiser, kPlayerZero, Zone::Melee, 0), registry));
    assert(apply_action_with_kernel(blocked_frost, Action::choose_card_target(kPlayerZero, blocked_bruiser, blocked_enemy), registry));
    assert((blocked_frost.location_of(blocked_enemy) == Location{kPlayerOne, Zone::Melee, 0}));
    assert(blocked_frost.find_card(blocked_enemy)->state.power == 6);

    GameState blocked_clear = make_running_state(kPlayerZero);
    const EntityId blocked_clear_bruiser = blocked_clear.add_card(deck_a::make_wild_hunt_bruiser_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    blocked_clear.add_card(make_unit_definition("blocked_clear_followup", "Blocked Clear Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId blocked_clear_enemy = blocked_clear.add_card(make_unit_definition("blocked_clear_enemy", "Blocked Clear Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    for (int index = 0; index < 9; ++index) {
        blocked_clear.add_card(make_unit_definition("blocked_clear_row_" + std::to_string(index), "Blocked Clear Row", 1),
            kPlayerOne, Location{kPlayerOne, Zone::Ranged, static_cast<std::size_t>(index)});
    }
    assert(apply_action_with_kernel(blocked_clear, Action::play_card(kPlayerZero, blocked_clear_bruiser, kPlayerZero, Zone::Melee, 0), registry));
    assert(apply_action_with_kernel(blocked_clear, Action::choose_card_target(kPlayerZero, blocked_clear_bruiser, blocked_clear_enemy), registry));
    assert((blocked_clear.location_of(blocked_clear_enemy) == Location{kPlayerOne, Zone::Melee, 0}));
    assert(blocked_clear.find_card(blocked_clear_enemy)->state.power == 6);
}

void test_wild_hunt_navigator_boosts_from_corresponding_row_without_dominance() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId navigator = state.add_card(deck_a::make_wild_hunt_navigator_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId ally = state.add_card(make_unit_definition("ally", "Ally", 4, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.add_card(make_unit_definition("strong_enemy", "Strong Enemy", 8), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 2, kPlayerOne, {{"damage", "2"}});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Ranged, "frost", 3, kPlayerOne, {{"damage", "2"}});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, navigator, kPlayerZero, Zone::Melee, 0), registry));
    assert(apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, navigator, ally), registry));
    assert(state.find_card(ally)->state.power == 7);
}

void test_wild_hunt_navigator_boosts_from_both_rows_with_dominance() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId navigator = state.add_card(deck_a::make_wild_hunt_navigator_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId ally = state.add_card(make_unit_definition("dominant_ally", "Dominant Ally", 10), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("enemy", "Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 2, kPlayerOne, {{"damage", "2"}});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Ranged, "frost", 3, kPlayerOne, {{"damage", "2"}});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, navigator, kPlayerZero, Zone::Ranged, 0), registry));
    assert(apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, navigator, ally), registry));
    assert(state.find_card(ally)->state.power == 15);
}

void test_wild_hunt_navigator_zero_weather_is_legal_noop() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId navigator = state.add_card(deck_a::make_wild_hunt_navigator_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId ally = state.add_card(make_unit_definition("ally", "Ally", 4), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, navigator, kPlayerZero, Zone::Ranged, 0), registry));
    assert(state.pending_choice.has_value());
    const KernelResult resolved = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, navigator, ally), registry);
    assert(resolved);
    assert(!state.pending_choice.has_value());
    assert(state.find_card(ally)->state.power == 4);
}

void test_aen_elle_aristocrat_order_moves_only_with_dominance() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    GameState dominant = make_running_state(kPlayerZero);
    const EntityId aristocrat = dominant.add_card(deck_a::make_aen_elle_aristocrat_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    dominant.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    dominant.add_card(make_unit_definition("dominant", "Dominant", 10), kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    const EntityId target = dominant.add_card(make_unit_definition("enemy", "Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    assert(apply_action_with_kernel(dominant, Action::use_order(kPlayerZero, aristocrat), registry));
    assert(dominant.pending_choice.has_value());
    assert(dominant.pending_choice->effect_id == std::string(deck_a::kAenElleAristocratOrderEffect));
    assert(apply_action_with_kernel(dominant, Action::choose_card_target(kPlayerZero, aristocrat, target), registry));
    assert((dominant.location_of(target) == Location{kPlayerOne, Zone::Ranged, 0}));
    assert(dominant.find_card(aristocrat)->runtime.order_used);

    // A full destination row does not make the enemy an illegal Order target.
    // The Order is consumed, the move simply fails, and the unit stays put.
    GameState blocked = make_running_state(kPlayerZero);
    const EntityId blocked_aristocrat = blocked.add_card(
        deck_a::make_aen_elle_aristocrat_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    blocked.add_card(make_unit_definition("blocked_dominance", "Blocked Dominance", 10),
        kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    blocked.add_card(make_unit_definition("blocked_order_followup", "Blocked Order Followup", 1),
        kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId blocked_target = blocked.add_card(
        make_unit_definition("blocked_order_enemy", "Blocked Order Enemy", 6),
        kPlayerOne,
        Location{kPlayerOne, Zone::Melee, 0}
    );
    for (int index = 0; index < 9; ++index) {
        blocked.add_card(
            make_unit_definition("blocked_order_filler_" + std::to_string(index), "Blocked Order Filler", 1),
            kPlayerOne,
            Location{kPlayerOne, Zone::Ranged, static_cast<std::size_t>(index)}
        );
    }
    assert(apply_action_with_kernel(blocked, Action::use_order(kPlayerZero, blocked_aristocrat), registry));
    assert(blocked.pending_choice.has_value());
    assert(std::find(
        blocked.pending_choice->legal_card_targets.begin(),
        blocked.pending_choice->legal_card_targets.end(),
        blocked_target
    ) != blocked.pending_choice->legal_card_targets.end());
    assert(apply_action_with_kernel(
        blocked,
        Action::choose_card_target(kPlayerZero, blocked_aristocrat, blocked_target),
        registry
    ));
    assert((blocked.location_of(blocked_target) == Location{kPlayerOne, Zone::Melee, 0}));
    assert(blocked.find_card(blocked_aristocrat)->runtime.order_used);

    GameState no_dominance = make_running_state(kPlayerZero);
    const EntityId second = no_dominance.add_card(deck_a::make_aen_elle_aristocrat_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    no_dominance.add_card(make_unit_definition("followup2", "Followup 2", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId stronger = no_dominance.add_card(make_unit_definition("stronger", "Stronger", 9), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    assert(!can_use_order_source(no_dominance, kPlayerZero, second));
    const auto no_dominance_actions = legal_turn_actions(no_dominance, kPlayerZero);
    assert(std::find(no_dominance_actions.begin(), no_dominance_actions.end(), Action::use_order(kPlayerZero, second)) == no_dominance_actions.end());
    assert(!apply_action_with_kernel(no_dominance, Action::use_order(kPlayerZero, second), registry));
    assert(!no_dominance.pending_choice.has_value());
    assert((no_dominance.location_of(stronger) == Location{kPlayerOne, Zone::Melee, 0}));
    assert(!no_dominance.find_card(second)->runtime.order_used);
}

void test_aen_elle_aristocrat_turn_end_extends_each_marked_row_once() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId aristocrat = state.add_card(deck_a::make_aen_elle_aristocrat_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 2, kPlayerOne, {{"damage", "2"}});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Ranged, "frost", 4, kPlayerOne, {{"damage", "2"}});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};

    state.turn_contexts[0].opponent_rows_frosted_this_turn = {true, false};
    assert(registry.invoke(context, EffectCall{deck_a::kAenElleAristocratTurnEndEffect, kPlayerZero, aristocrat}));
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 3);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 4);

    state.turn_contexts[0].opponent_rows_frosted_this_turn = {true, true};
    assert(registry.invoke(context, EffectCall{deck_a::kAenElleAristocratTurnEndEffect, kPlayerZero, aristocrat}));
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 4);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 5);
}

void test_aen_elle_aristocrat_real_end_turn_extends_frost_before_marker_reset() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId aristocrat = state.add_card(
        deck_a::make_aen_elle_aristocrat_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    (void)aristocrat;
    state.add_card(make_unit_definition("dominance", "Dominance", 10), kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    const EntityId warrior = state.add_card(
        deck_a::make_wild_hunt_warrior_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId enemy = state.add_card(
        make_unit_definition("enemy_for_aristocrat", "Enemy", 6),
        kPlayerOne,
        Location{kPlayerOne, Zone::Ranged, 0}
    );

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    assert(apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, warrior, kPlayerZero, Zone::Ranged, 0),
        registry
    ));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->effect_id == std::string(deck_a::kWildHuntWarriorDeployEffect));
    assert(apply_action_with_kernel(
        state,
        Action::choose_card_target(kPlayerZero, warrior, enemy),
        registry
    ));

    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 1);
    assert(state.turn_contexts[0].opponent_rows_frosted_this_turn[row_index(Zone::Ranged)]);

    const KernelResult ended = apply_action_with_kernel(state, Action::end_turn(kPlayerZero), registry);
    assert(ended);
    // The Aristocrat must observe the frost marker during turn_end, extend the
    // same row by one turn, and only then may the transition clear the marker.
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 1);
    assert(!state.turn_contexts[0].opponent_rows_frosted_this_turn[row_index(Zone::Ranged)]);
}

void test_frost_applied_row_markers_reset_after_turn_transition() {
    GameState state = make_running_state(kPlayerZero);
    state.add_card(deck_a::make_aen_elle_aristocrat_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.turn_contexts[0].opponent_rows_frosted_this_turn = {true, true};

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::pass(kPlayerZero), registry));
    assert(!state.turn_contexts[0].opponent_rows_frosted_this_turn[0]);
    assert(!state.turn_contexts[0].opponent_rows_frosted_this_turn[1]);
}

void test_naglfar_crew_turn_end_checks_corresponding_enemy_row() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    GameState matching = make_running_state(kPlayerZero);
    const EntityId matching_crew = matching.add_card(deck_a::make_naglfar_crew_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    add_or_extend_row_effect(matching, kPlayerOne, Zone::Melee, "frost", 2, kPlayerOne, {{"damage", "2"}});
    assert(apply_action_with_kernel(matching, Action::pass(kPlayerZero), registry));
    assert(matching.find_card(matching_crew)->state.power == 2);

    GameState opposite = make_running_state(kPlayerZero);
    const EntityId opposite_crew = opposite.add_card(deck_a::make_naglfar_crew_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    add_or_extend_row_effect(opposite, kPlayerOne, Zone::Ranged, "frost", 2, kPlayerOne, {{"damage", "2"}});
    assert(apply_action_with_kernel(opposite, Action::pass(kPlayerZero), registry));
    assert(opposite.find_card(opposite_crew)->state.power == 1);
}

void test_naglfar_taskmaster_can_target_allies_with_dominance() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId taskmaster = state.add_card(deck_a::make_naglfar_taskmaster_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId ally = state.add_card(make_unit_definition("dominant_ally", "Dominant Ally", 10), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("weaker_enemy", "Weaker Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(ally)->state.locked = true;

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, taskmaster, kPlayerZero, Zone::Ranged, 0), registry);
    assert(played);
    assert(state.pending_choice.has_value());
    assert(std::find(state.pending_choice->legal_card_targets.begin(), state.pending_choice->legal_card_targets.end(), ally)
        != state.pending_choice->legal_card_targets.end());
    KernelResult chosen = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, taskmaster, ally), registry);
    assert(chosen);
    assert(!state.find_card(ally)->state.locked);
}

void test_slave_trader_infusion_uses_any_trader_and_set_power_ignores_armor_shield() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId trader = state.add_card(deck_a::make_aen_elle_slave_trader_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId target = state.add_card(make_unit_definition("target", "Target", 3), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(target)->state.armor = 5;
    state.find_card(target)->state.shield = true;
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 2, kPlayerOne, {{"damage", "2"}});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Ranged, "frost", 3, kPlayerOne, {{"damage", "2"}});

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, trader, kPlayerZero, Zone::Melee, 0), registry));
    assert(state.pending_choice.has_value());
    assert(apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, trader, target), registry));
    assert(state.find_card(trader)->state.vitality == 5);
    assert(infusion_count(state, target) == 1);
    TaskQueue duplicate_queue;
    std::vector<EventRecord> duplicate_events;
    KernelContext duplicate_context{state, duplicate_queue, duplicate_events};
    assert(registry.invoke(duplicate_context, EffectCall{
        deck_a::kAenElleSlaveTraderDeployEffect,
        kPlayerZero,
        trader,
        ActionTarget::card(target)
    }));
    assert(infusion_count(state, target) == 1);

    primitive_move_card(state, trader, Location{kPlayerZero, Zone::Cemetery, 0});
    const EntityId other_trader = state.add_card(deck_a::make_aen_elle_slave_trader_definition(), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    (void)other_trader;
    state.current_player_id = kPlayerOne;
    state.turn_contexts = {};
    assert(apply_action_with_kernel(state, Action::pass(kPlayerOne), registry));
    assert(state.find_card(target)->state.power == 1);
    assert(state.find_card(target)->state.locked);
    assert(state.find_card(target)->state.armor == 5);
    assert(state.find_card(target)->state.shield);
}

void test_slave_trader_infusion_respects_veil_lock_purify_and_host_departure() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    GameState veiled = make_running_state(kPlayerZero);
    const EntityId trader = veiled.add_card(deck_a::make_aen_elle_slave_trader_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId veil_target = veiled.add_card(make_unit_definition("veil_target", "Veil Target", 3), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    veiled.find_card(veil_target)->state.veil = true;
    const KernelResult no_target = apply_action_with_kernel(veiled, Action::play_card(kPlayerZero, trader, kPlayerZero, Zone::Melee, 0), registry);
    assert(no_target);
    assert(!veiled.pending_choice.has_value());
    assert(infusion_count(veiled, veil_target) == 0);
    assert(count_events(no_target.events, "supported:invalid_target") == 1);

    GameState state = make_running_state(kPlayerZero);
    const EntityId second_trader = state.add_card(deck_a::make_aen_elle_slave_trader_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId target = state.add_card(make_unit_definition("target", "Target", 3), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, second_trader, kPlayerZero, Zone::Melee, 0), registry));
    assert(apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, second_trader, target), registry));
    assert(infusion_count(state, target) == 1);
    state.find_card(target)->state.locked = true;
    state.current_player_id = kPlayerOne;
    state.turn_contexts = {};
    assert(apply_action_with_kernel(state, Action::pass(kPlayerOne), registry));
    assert(state.find_card(target)->state.power == 3);

    assert(primitive_purify_card(state, target));
    assert(infusion_count(state, target) == 0);
    state.find_card(target)->state.locked = false;
    RuntimeListener infusion;
    infusion.event = "turn_end";
    infusion.handler = std::string(deck_a::kAenElleSlaveTraderInfusionEffect);
    infusion.source_id = target;
    infusion.once = false;
    infusion.origin = RuntimeListenerOrigin::Infusion;
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    context.add_listener(std::move(infusion));
    assert(infusion_count(state, target) == 1);
    assert(primitive_move_card(state, target, Location{kPlayerOne, Zone::Cemetery, 0}));
    assert(infusion_count(state, target) == 0);
}

void test_oberon_pool_evolution_and_definition_choice_chain() {
    const auto catalog = std::make_shared<CardCatalog>(deck_a::make_catalog());
    std::vector<std::string> pool;
    for (const CardDefinition& definition : catalog->all()) {
        if (!definition.is_token && definition.card_type == CardType::Unit && definition.color == "Bronze"
            && definition.has_category("狂猎")) {
            pool.push_back(definition.id);
        }
    }
    assert(pool.size() == 10);

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    state.card_catalog = catalog;
    const EntityId king = state.add_card(deck_a::make_oberon_king_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.round_no = 2;
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    assert(registry.invoke(context, EffectCall{deck_a::kOberonKingRoundStartEffect, kPlayerZero, king}));
    assert(state.find_card(king)->definition->id == deck_a::kOberonInvaderId);

    state.round_no = 3;
    state.player(kPlayerZero).starting_deck = {std::string(deck_a::kWildHuntWarriorId)};
    assert(registry.invoke(context, EffectCall{deck_a::kOberonInvaderRoundStartEffect, kPlayerZero, king}));
    assert(state.find_card(king)->definition->id == deck_a::kOberonConquerorId);
    assert(state.find_card(king)->state.veil);

    GameState random_a = make_running_state(kPlayerZero);
    GameState random_b = make_running_state(kPlayerZero);
    random_a.card_catalog = catalog;
    random_b.card_catalog = catalog;
    random_a.rng_state = 12345;
    random_b.rng_state = 12345;
    const EntityId king_a = random_a.add_card(deck_a::make_oberon_king_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId king_b = random_b.add_card(deck_a::make_oberon_king_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    assert(apply_action_with_kernel(random_a, Action::play_card(kPlayerZero, king_a, kPlayerZero, Zone::Melee, 0), registry));
    assert(apply_action_with_kernel(random_b, Action::play_card(kPlayerZero, king_b, kPlayerZero, Zone::Melee, 0), registry));
    assert(random_a.pending_choice.has_value() && random_b.pending_choice.has_value());
    assert(random_a.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(random_a.pending_choice->resume_kind == PendingChoiceResumeKind::PlayCard);
    assert(random_a.player(kPlayerZero).stay.size() == 1);
    assert(random_b.player(kPlayerZero).stay.size() == 1);
    const EntityId generated_a = random_a.player(kPlayerZero).stay.front();
    const EntityId generated_b = random_b.player(kPlayerZero).stay.front();
    assert(random_a.find_card(generated_a)->definition->id
        == random_b.find_card(generated_b)->definition->id);

    const auto generated_rows = legal_pending_choice_actions(random_a);
    assert(generated_rows.size() == 2);
    assert(apply_action_with_kernel(random_a, generated_rows.front(), registry));
    assert(random_a.pending_choice.has_value());
    assert(random_a.pending_choice->kind == PendingChoiceKind::InsertPosition);
    const auto generated_positions = legal_pending_choice_actions(random_a);
    assert(!generated_positions.empty());
    assert(apply_action_with_kernel(random_a, generated_positions.front(), registry));
    const auto generated_location = random_a.location_of(generated_a);
    assert(generated_location.has_value());
    assert(is_battle_zone(generated_location->zone));

    GameState choice = make_running_state(kPlayerZero);
    choice.card_catalog = catalog;
    const EntityId invader = choice.add_card(deck_a::make_oberon_invader_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    choice.add_card(make_unit_definition("ally", "Ally", 4), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    choice.add_card(make_unit_definition("enemy", "Enemy", 4), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    assert(apply_action_with_kernel(choice, Action::play_card(kPlayerZero, invader, kPlayerZero, Zone::Melee, 0), registry));
    assert(choice.pending_choice.has_value());
    assert(choice.pending_choice->kind == PendingChoiceKind::CardDefinitionChoice);
    assert(choice.pending_choice->legal_card_definition_ids.size() == 3);
    for (CardDefId id : choice.pending_choice->legal_card_definition_ids) {
        const CardDefinition& definition = catalog->at(id);
        assert(definition.card_type == CardType::Unit);
        assert(definition.color == "Bronze");
        assert(definition.has_category("狂猎"));
    }
    const auto actions = legal_pending_choice_actions(choice);
    assert(actions.size() == 3);
    for (const Action& action : actions) {
        assert(action.target.kind == ActionTargetKind::CardDefinition);
        assert(action.target.entity_id == kInvalidEntityId);
    }
    const CardDefId selected = actions.front().target.definition_id;
    assert(apply_action_with_kernel(choice, actions.front(), registry));
    assert(choice.pending_choice.has_value());
    assert(choice.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(choice.pending_choice->resolution_frame != nullptr);
    const auto& prefix = choice.pending_choice->resolution_frame->decision_prefix;
    assert(prefix.size() >= 2);
    assert(prefix.back().target_definition_id == selected);
    const auto rows = legal_pending_choice_actions(choice);
    assert(rows.size() == 2);
    assert(apply_action_with_kernel(choice, rows.front(), registry));
    for (int depth = 0; choice.pending_choice.has_value() && depth < 4; ++depth) {
        const auto nested = legal_pending_choice_actions(choice);
        assert(!nested.empty());
        assert(apply_action_with_kernel(choice, nested.front(), registry));
    }
    assert(!choice.pending_choice.has_value());
    bool found = false;
    for (Zone zone : {Zone::Melee, Zone::Ranged}) {
        for (EntityId id : choice.player(kPlayerZero).row(zone)) {
            if (id != invader && choice.find_card(id)->definition->id == catalog->at(selected).id) found = true;
        }
    }
    assert(found);
}

void test_oberon_conqueror_starting_deck_filter_and_passive() {
    const auto catalog = std::make_shared<CardCatalog>(deck_a::make_catalog());
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    state.card_catalog = catalog;
    state.player(kPlayerZero).starting_deck = {
        std::string(deck_a::kWildHuntWarriorId),
        std::string(deck_a::kWildHuntNavigatorId),
        std::string(deck_a::kBruxaId),
    };
    const EntityId conqueror = state.add_card(deck_a::make_oberon_conqueror_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    assert(registry.invoke(context, EffectCall{deck_a::kOberonConquerorDeployEffect, kPlayerZero, conqueror}));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->legal_card_definition_ids.size() == 2);
    std::vector<std::string> conqueror_choices;
    for (CardDefId id : state.pending_choice->legal_card_definition_ids) {
        const CardDefinition& definition = catalog->at(id);
        assert(definition.color == "Bronze");
        assert(definition.has_category("狂猎"));
        conqueror_choices.push_back(definition.id);
    }
    std::sort(conqueror_choices.begin(), conqueror_choices.end());
    std::vector<std::string> expected_choices{
        std::string(deck_a::kWildHuntNavigatorId),
        std::string(deck_a::kWildHuntWarriorId),
    };
    std::sort(expected_choices.begin(), expected_choices.end());
    assert(conqueror_choices == expected_choices);

    EffectCall self_played{deck_a::kOberonConquerorCardPlayedEffect, kPlayerZero, conqueror};
    self_played.event_target_entity_id = conqueror;
    assert(registry.invoke(context, self_played));
    assert(queue.empty());

    const EntityId warrior = state.add_card(
        deck_a::make_wild_hunt_warrior_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Ranged, 0}
    );
    EffectCall other_played{deck_a::kOberonConquerorCardPlayedEffect, kPlayerZero, conqueror};
    other_played.event_target_entity_id = warrior;
    assert(registry.invoke(context, other_played));
    assert(!queue.empty());
    const auto task = queue.pop_front();
    assert(task.has_value());
    assert(task->type == TaskType::BoostCard);
    assert(task->target_entity_id == warrior);
    assert(task->amount == 1);
}

void test_crystal_skull_boosts_allied_unit_and_grants_veil_once() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId skull = state.add_card(deck_a::make_crystal_skull_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId ally = state.add_card(make_unit_definition("ally", "Ally", 4), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.add_card(make_unit_definition("enemy", "Enemy", 4), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    KernelConfig config;
    config.order_action_consumes_turn = false;
    const auto legal = legal_turn_actions(state, kPlayerZero);
    assert(std::find(legal.begin(), legal.end(), Action::use_order(kPlayerZero, skull)) != legal.end());
    assert(apply_action_with_kernel(state, Action::use_order(kPlayerZero, skull, ActionTarget::card(ally)), registry, config));
    assert(state.find_card(ally)->state.power == 8);
    assert(state.find_card(ally)->state.veil);
    assert(state.location_of(skull)->zone == Zone::Banished);
}

void test_imleriths_wrath_uses_strongest_ally_or_destroys_on_frost() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    GameState damage = make_running_state(kPlayerZero);
    const EntityId wrath = damage.add_card(deck_a::make_imleriths_wrath_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    damage.add_card(make_unit_definition("strong", "Strong", 7), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId target = damage.add_card(make_unit_definition("target", "Target", 10), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});
    assert(apply_action_with_kernel(damage, Action::play_card(kPlayerZero, wrath), registry));
    assert(damage.pending_choice.has_value());
    assert(apply_action_with_kernel(damage, Action::choose_card_target(kPlayerZero, wrath, target), registry));
    assert(damage.find_card(target)->state.power == 3);

    GameState frost = make_running_state(kPlayerZero);
    const EntityId frost_wrath = frost.add_card(deck_a::make_imleriths_wrath_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId frost_target = frost.add_card(make_unit_definition("frost_target", "Frost Target", 20), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    add_or_extend_row_effect(frost, kPlayerOne, Zone::Melee, "frost", 1, kPlayerOne, {{"damage", "2"}});
    assert(apply_action_with_kernel(frost, Action::play_card(kPlayerZero, frost_wrath), registry));
    assert(apply_action_with_kernel(frost, Action::choose_card_target(kPlayerZero, frost_wrath, frost_target), registry));
    assert(frost.location_of(frost_target)->zone == Zone::Cemetery);

    GameState zero = make_running_state(kPlayerZero);
    const EntityId zero_wrath = zero.add_card(deck_a::make_imleriths_wrath_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId zero_target = zero.add_card(make_unit_definition("zero_target", "Zero Target", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    assert(apply_action_with_kernel(zero, Action::play_card(kPlayerZero, zero_wrath), registry));
    assert(apply_action_with_kernel(zero, Action::choose_card_target(kPlayerZero, zero_wrath, zero_target), registry));
    assert(zero.find_card(zero_target)->state.power == 5);
    assert(zero.location_of(zero_wrath)->zone == Zone::Cemetery);
}

void test_apiarian_phantom_order_and_unused_turn_end_contract() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    const EntityId phantom = state.add_card(deck_a::make_apiarian_phantom_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    assert(state.find_card(phantom)->state.veil);
    assert(state.find_card(phantom)->state.order_charges == 1);
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    assert(registry.invoke(context, EffectCall{deck_a::kApiarianPhantomTurnEndEffect, kPlayerZero, phantom}));
    assert(queue.size() == 1);
    assert(queue.pop_front()->amount == 1);

    KernelConfig config;
    config.order_action_consumes_turn = false;
    assert(apply_action_with_kernel(state, Action::use_order(kPlayerZero, phantom), registry, config));
    assert(apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, phantom, enemy), registry, config));
    assert(state.find_card(enemy)->state.power == 3);
    assert(state.find_card(phantom)->runtime.order_used);
    TaskQueue used_queue;
    KernelContext used_context{state, used_queue, events};
    assert(registry.invoke(used_context, EffectCall{deck_a::kApiarianPhantomTurnEndEffect, kPlayerZero, phantom}));
    assert(used_queue.empty());

    GameState ranged = make_running_state(kPlayerZero);
    const EntityId ranged_phantom = ranged.add_card(deck_a::make_apiarian_phantom_definition(), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    ranged.add_card(make_unit_definition("followup2", "Followup 2", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    ranged.add_card(make_unit_definition("enemy2", "Enemy 2", 6), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const auto ranged_actions = legal_turn_actions(ranged, kPlayerZero);
    assert(std::find(ranged_actions.begin(), ranged_actions.end(), Action::use_order(kPlayerZero, ranged_phantom)) == ranged_actions.end());
}

void test_white_frost_moves_then_frosts_and_passive_checks_corresponding_row() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    const EntityId leader = state.add_card(deck_a::make_white_frost_definition(), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    state.player(kPlayerZero).leader = leader;
    state.add_card(make_unit_definition("followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    KernelConfig config;
    config.leader_action_consumes_turn = false;
    assert(apply_action_with_kernel(state, Action::use_leader(kPlayerZero, leader), registry, config));
    assert(apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, leader, enemy), registry, config));
    assert(state.location_of(enemy)->zone == Zone::Ranged);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 2);
    assert(state.find_card(leader)->state.order_charges == 1);

    CardDefinition hunt = make_unit_definition("hunt", "Hunt", 4);
    hunt.categories = {"狂猎"};
    const EntityId matching = state.add_card(hunt, kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    const EntityId nonmatching = state.add_card(hunt, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    std::vector<EventRecord> events;
    TaskQueue matching_queue;
    KernelContext matching_context{state, matching_queue, events};
    EffectCall matching_call{deck_a::kWhiteFrostCardPlayedEffect, kPlayerZero, leader};
    matching_call.event_target_entity_id = matching;
    assert(registry.invoke(matching_context, matching_call));
    assert(matching_queue.size() == 1 && matching_queue.pop_front()->target_entity_id == matching);
    TaskQueue nonmatching_queue;
    KernelContext nonmatching_context{state, nonmatching_queue, events};
    matching_call.event_target_entity_id = nonmatching;
    assert(registry.invoke(nonmatching_context, matching_call));
    assert(nonmatching_queue.empty());

    GameState blocked = make_running_state(kPlayerZero);
    const EntityId blocked_leader = blocked.add_card(deck_a::make_white_frost_definition(), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    const EntityId blocked_enemy = blocked.add_card(make_unit_definition("blocked", "Blocked", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    for (int index = 0; index < 9; ++index) {
        blocked.add_card(make_unit_definition("filler_" + std::to_string(index), "Filler", 1), kPlayerOne, Location{kPlayerOne, Zone::Ranged, static_cast<std::size_t>(index)});
    }
    TaskQueue blocked_queue;
    std::vector<EventRecord> blocked_events;
    KernelContext blocked_context{blocked, blocked_queue, blocked_events};
    assert(registry.invoke(blocked_context, EffectCall{deck_a::kWhiteFrostLeaderEffect, kPlayerZero, blocked_leader, ActionTarget::card(blocked_enemy)}));
    assert(blocked.location_of(blocked_enemy)->zone == Zone::Melee);
    assert(row_effect_duration(blocked, kPlayerOne, Zone::Melee, "frost") == 2);
    assert(row_effect_duration(blocked, kPlayerOne, Zone::Ranged, "frost") == 0);
    assert(std::none_of(blocked_events.begin(), blocked_events.end(), [](const EventRecord& event) { return event.name == "card_moved"; }));
}

void test_eredin_and_ancient_foglet_row_effect_contracts() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    const EntityId eredin = state.add_card(deck_a::make_eredin_breacc_glas_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId foglet = state.add_card(deck_a::make_ancient_foglet_definition(), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "blood_moon", 3, kPlayerOne, {});
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    assert(registry.invoke(context, EffectCall{deck_a::kAncientFogletDeployEffect, kPlayerZero, foglet}));
    assert(queue.pop_front()->amount == 3);
    assert(registry.invoke(context, EffectCall{deck_a::kEredinDeployEffect, kPlayerZero, eredin,
        ActionTarget::row(kPlayerOne, Zone::Melee)}));
    assert(state.player(kPlayerOne).effects_on_row(Zone::Melee).size() == 1);
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "blood_moon") == 0);
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 2);
    const auto foglet_boost = queue.pop_front();
    assert(foglet_boost.has_value() && foglet_boost->target_entity_id == foglet && foglet_boost->amount == 2);

    // Extending an already-active Frost is still a new row-effect
    // application. The Foglet gains exactly the newly added duration.
    assert(registry.invoke(context, EffectCall{deck_a::kEredinDeployEffect, kPlayerZero, eredin,
        ActionTarget::row(kPlayerOne, Zone::Melee)}));
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 4);
    const auto extension_boost = queue.pop_front();
    assert(extension_boost.has_value() && extension_boost->target_entity_id == foglet && extension_boost->amount == 2);

    state.find_card(foglet)->state.locked = true;
    assert(registry.invoke(context, EffectCall{deck_a::kEredinDeployEffect, kPlayerZero, eredin,
        ActionTarget::row(kPlayerOne, Zone::Ranged)}));
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 2);
    assert(queue.empty());
}

void test_winter_queen_summons_and_round_end_boosts_with_devotion() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    const EntityId queen = state.add_card(deck_a::make_winter_queen_definition(), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Melee, "frost", 2, kPlayerOne, {});
    add_or_extend_row_effect(state, kPlayerOne, Zone::Ranged, "frost", 3, kPlayerOne, {});
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    assert(registry.invoke(context, EffectCall{deck_a::kWinterQueenTurnEndEffect, kPlayerZero, queen}));
    assert(state.location_of(queen)->zone == Zone::Ranged);
    assert(registry.invoke(context, EffectCall{deck_a::kWinterQueenRoundEndEffect, kPlayerZero, queen}));
    const auto boost = queue.pop_front();
    assert(boost.has_value() && boost->amount == 10);
}

void test_red_riders_definition_choice_supports_both_rows_mode() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    state.card_catalog = std::make_shared<CardCatalog>(deck_a::make_catalog());
    const EntityId riders = state.add_card(deck_a::make_red_riders_definition(), kPlayerZero, Location{kPlayerZero, Zone::Stay, 0});
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    assert(registry.invoke(context, EffectCall{deck_a::kRedRidersSpecialEffect, kPlayerZero, riders}));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->legal_card_definition_ids.size() == 3);
    context.clear_continuation();
    state.pending_choice.reset();
    const CardDefId both = state.card_catalog->id_of(deck_a::kRedRidersBothRowsId).value();
    assert(registry.invoke(context, EffectCall{deck_a::kRedRidersSpecialEffect, kPlayerZero, riders,
        ActionTarget::card_definition(both)}));
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 2);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 2);
}

void test_red_riders_replay_resets_unit_and_doomed_unit_is_banished() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    GameState state = make_running_state(kPlayerZero);
    state.card_catalog = std::make_shared<CardCatalog>(deck_a::make_catalog());
    const EntityId riders = state.add_card(
        deck_a::make_red_riders_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Stay, 0}
    );

    CardDefinition replay_target = make_unit_definition("replay_target", "Replay Target", 4, Faction::Monsters);
    replay_target.color = "Bronze";
    replay_target.categories = {"狂猎"};
    const EntityId target = state.add_card(
        replay_target,
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    RuntimeCard* target_card = state.find_card(target);
    target_card->state.power = 10;
    target_card->state.armor = 5;
    target_card->state.bleeding = 3;
    target_card->state.locked = true;
    target_card->runtime.order_used = true;
    target_card->memory["temporary"] = "yes";

    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    assert(registry.invoke(context, EffectCall{
        deck_a::kRedRidersSpecialEffect,
        kPlayerZero,
        riders,
        ActionTarget::card(target)
    }));
    assert(state.location_of(target)->zone == Zone::Stay);
    target_card = state.find_card(target);
    assert(target_card->state.power == 4);
    assert(target_card->state.armor == 0);
    assert(target_card->state.bleeding == 0);
    assert(!target_card->state.locked);
    assert(!target_card->runtime.order_used);
    assert(target_card->memory.empty());
    assert(!queue.empty());
    const auto replay_task = queue.pop_front();
    assert(replay_task.has_value());
    assert(replay_task->type == TaskType::PlayCard);
    assert(replay_task->source_entity_id == target);

    GameState doomed_state = make_running_state(kPlayerZero);
    doomed_state.card_catalog = std::make_shared<CardCatalog>(deck_a::make_catalog());
    const EntityId doomed_riders = doomed_state.add_card(
        deck_a::make_red_riders_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Stay, 0}
    );
    CardDefinition doomed_definition = make_unit_definition("doomed_replay", "Doomed Replay", 4, Faction::Monsters);
    doomed_definition.color = "Bronze";
    doomed_definition.categories = {"狂猎"};
    const EntityId doomed_target = doomed_state.add_card(
        doomed_definition,
        kPlayerZero,
        Location{kPlayerZero, Zone::Ranged, 0}
    );
    doomed_state.find_card(doomed_target)->state.doomed = true;

    TaskQueue doomed_queue;
    std::vector<EventRecord> doomed_events;
    KernelContext doomed_context{doomed_state, doomed_queue, doomed_events};
    assert(registry.invoke(doomed_context, EffectCall{
        deck_a::kRedRidersSpecialEffect,
        kPlayerZero,
        doomed_riders,
        ActionTarget::card(doomed_target)
    }));
    assert(doomed_state.location_of(doomed_target)->zone == Zone::Banished);
    assert(doomed_queue.empty());
}

void test_golden_child_resets_to_recent_actual_frost_damage() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    const EntityId child = state.add_card(deck_a::make_caranthir_golden_child_definition(), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    RuntimeCard* card = state.find_card(child);
    card->state.power = 20;
    card->memory["frost_damage_0"] = "1";
    card->memory["frost_damage_1"] = "2";
    card->memory["frost_damage_2"] = "3";
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};
    assert(registry.invoke(context, EffectCall{deck_a::kCaranthirGoldenChildTurnStartEffect, kPlayerZero, child}));
    assert(card->state.power == 1);
    const auto boost = queue.pop_front();
    assert(boost.has_value() && boost->amount == 6);
}

void test_tir_na_lia_devotion_belongs_to_order_not_deploy() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState state = make_running_state(kPlayerZero);
    state.card_catalog = std::make_shared<CardCatalog>(deck_a::make_catalog());
    const EntityId tir = state.add_card(deck_a::make_tir_na_lia_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    RuntimeCard* card = state.find_card(tir);
    card->memory["saved_frost_melee"] = "3";
    card->memory["saved_frost_ranged"] = "1";
    state.player(kPlayerZero).starting_deck = {std::string(deck_a::kWildHuntWarriorId), std::string(deck_a::kWildHuntWarriorId), std::string(deck_a::kWildHuntNavigatorId)};
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{state, queue, events};

    // Deploy only boosts an allied unit. Devotion must not restore Frost here.
    assert(registry.invoke(context, EffectCall{deck_a::kTirNaLiaDeployEffect, kPlayerZero, tir}));
    assert(!state.pending_choice.has_value());
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 0);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 0);

    const EntityId ally = state.add_card(make_unit_definition("ally", "Ally", 4, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    assert(registry.invoke(context, EffectCall{deck_a::kTirNaLiaDeployEffect, kPlayerZero, tir, ActionTarget::card(ally)}));
    const auto boost = queue.pop_front();
    assert(boost.has_value() && boost->target_entity_id == ally && boost->amount == 2);

    // Devotion augments the Order: restore last-round Frost first, then create
    // Red Riders as a staged continuation.
    assert(registry.invoke(context, EffectCall{deck_a::kTirNaLiaOrderEffect, kPlayerZero, tir}));
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 3);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 1);
    assert(context.continuation.size() == 1);
    assert(context.continuation.front().kind == ContinuationStepKind::PlayStagedCard);
}

void test_ard_gaeth_applies_both_rows_and_echoes_before_round_draw() {
    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    GameState effect = make_running_state(kPlayerZero);
    const EntityId gate = effect.add_card(deck_a::make_ard_gaeth_definition(), kPlayerZero, Location{kPlayerZero, Zone::Stay, 0});
    TaskQueue queue;
    std::vector<EventRecord> events;
    KernelContext context{effect, queue, events};
    assert(registry.invoke(context, EffectCall{deck_a::kArdGaethSpecialEffect, kPlayerZero, gate}));
    assert(row_effect_duration(effect, kPlayerOne, Zone::Melee, "frost") == 3);
    assert(row_effect_duration(effect, kPlayerOne, Zone::Ranged, "frost") == 3);

    GameState echo = make_running_state(kPlayerZero);
    echo.match_config["standard_deck_game"] = "true";
    echo.match_config["between_round_draw"] = "1";
    echo.match_config["hand_limit"] = "10";
    const EntityId echo_gate = echo.add_card(deck_a::make_ard_gaeth_definition(), kPlayerZero, Location{kPlayerZero, Zone::Cemetery, 0});
    const EntityId filler = echo.add_card(make_unit_definition("filler", "Filler", 1), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    assert(apply_action_with_kernel(echo, Action::pass(kPlayerZero), registry));
    assert(apply_action_with_kernel(echo, Action::pass(kPlayerOne), registry));
    assert(echo.round_no == 2);
    assert(echo.location_of(echo_gate)->zone == Zone::Hand);
    assert(echo.player(kPlayerZero).hand.front() == echo_gate);
    assert(echo.location_of(filler)->zone == Zone::Deck);
}

}  // namespace

int main() {
    test_deck_a_catalog_and_deck_spec_are_complete();
    test_standard_setup_works_with_full_deck_a_spec();
    test_bruxa_deploy_requests_target_then_applies_bleeding();
    test_blood_scent_uses_three_charges_and_spawns_after_final_charge();
    test_cooldown_order_is_blocked_then_reduced_by_vampire_played_listener();
    test_fleder_boosts_once_when_enemy_receives_bleeding();
    test_locked_fleder_listener_does_not_boost_or_consume_countdown();
    test_locked_garkain_listener_does_not_reduce_cooldown();
    test_garkain_order_is_melee_row_only();
    test_feast_of_blood_purifies_damages_and_bleeds_when_friendly_vampire_exists();
    test_feast_of_blood_stops_followup_when_damage_kills_same_target();
    test_ozzrel_melee_consumes_enemy_cemetery_unit();
    test_ozzrel_ranged_consumes_own_cemetery_unit();
    test_imlerith_can_be_placed_on_either_row_but_only_melee_deploys();
    test_verena_blocks_boost_on_bleeding_enemy_units();
    test_unseen_elder_turn_end_bleeds_non_bleeding_enemy_and_ticks_devotion();
    test_unseen_elder_veil_blocks_fresh_bleeding_and_devotion_tick();
    test_giant_centipede_destroys_itself_when_armor_breaks();
    test_lord_riptide_can_be_placed_on_either_row_but_only_melee_clashes_and_gains_hand_armor_on_turn_end();
    test_lord_riptide_auto_target_includes_immunity_and_randomizes_highest_ties();
    test_dettlaff_order_damages_bleeding_enemy_and_deathblow_spawns_ekimmara();
    test_dettlaff_deploy_adds_blood_moon_row_effect_data();
    test_bloodscented_predator_bonded_uses_target_base_power();
    test_wild_hunt_rider_summons_same_cards_from_deck_under_dominance();
    test_aen_elle_conqueror_veil_blocks_bleeding_until_purified();
    test_wild_hunt_rider_summons_each_matching_deck_copy_once();
    test_wild_hunt_hound_boosts_on_owners_turn_end_only_with_dominance();
    test_wild_hunt_warrior_damages_and_adds_frost_only_with_dominance();
    test_wild_hunt_bruiser_moves_then_damages_only_on_frost_destination();
    test_wild_hunt_navigator_boosts_from_corresponding_row_without_dominance();
    test_wild_hunt_navigator_boosts_from_both_rows_with_dominance();
    test_wild_hunt_navigator_zero_weather_is_legal_noop();
    test_aen_elle_aristocrat_order_moves_only_with_dominance();
    test_aen_elle_aristocrat_turn_end_extends_each_marked_row_once();
    test_aen_elle_aristocrat_real_end_turn_extends_frost_before_marker_reset();
    test_frost_applied_row_markers_reset_after_turn_transition();
    test_naglfar_crew_deploys_two_turns_of_frost_on_chosen_enemy_row();
    test_naglfar_crew_turn_end_checks_corresponding_enemy_row();
    test_naglfar_taskmaster_targets_only_enemies_without_dominance();
    test_naglfar_taskmaster_can_target_allies_with_dominance();
    test_slave_trader_infusion_uses_any_trader_and_set_power_ignores_armor_shield();
    test_slave_trader_infusion_respects_veil_lock_purify_and_host_departure();
    test_oberon_pool_evolution_and_definition_choice_chain();
    test_oberon_conqueror_starting_deck_filter_and_passive();
    test_crystal_skull_boosts_allied_unit_and_grants_veil_once();
    test_imleriths_wrath_uses_strongest_ally_or_destroys_on_frost();
    test_apiarian_phantom_order_and_unused_turn_end_contract();
    test_white_frost_moves_then_frosts_and_passive_checks_corresponding_row();
    test_eredin_and_ancient_foglet_row_effect_contracts();
    test_winter_queen_summons_and_round_end_boosts_with_devotion();
    test_red_riders_definition_choice_supports_both_rows_mode();
    test_red_riders_replay_resets_unit_and_doomed_unit_is_banished();
    test_golden_child_resets_to_recent_actual_frost_damage();
    test_tir_na_lia_devotion_belongs_to_order_not_deploy();
    test_ard_gaeth_applies_both_rows_and_echoes_before_round_draw();

    std::cout << "gwent_deck_a_catalog_effects_tests: OK\n";
    return 0;
}
