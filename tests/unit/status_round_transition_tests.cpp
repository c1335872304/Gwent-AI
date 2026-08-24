#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
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
    state.match_config["standard_deck_game"] = "true";
    state.match_config["hand_limit"] = "10";
    state.match_config["between_round_draw"] = "3";
    state.match_config["round_mulligan_base"] = "2";
    return state;
}

bool has_event(const KernelResult& result, const std::string& name) {
    return std::any_of(result.events.begin(), result.events.end(), [&](const EventRecord& event) {
        return event.name == name;
    });
}

void add_deck_cards(GameState& state, PlayerId player_id, int count, const std::string& prefix) {
    for (int i = 0; i < count; ++i) {
        CardDefinition card = make_unit_definition(prefix + "_deck_" + std::to_string(i), "Deck Card", 1);
        state.add_card(card, player_id, Location{player_id, Zone::Deck, state.player(player_id).deck.size()});
    }
}

void test_turn_end_ticks_bleeding_and_vitality() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId bleeder = state.add_card(make_unit_definition("bleeder", "Bleeder", 4), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId vital = state.add_card(make_unit_definition("vital", "Vital", 3), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.find_card(bleeder)->state.bleeding = 2;
    state.find_card(vital)->state.vitality = 1;

    const KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero));

    assert(result);
    assert(state.find_card(bleeder)->state.power == 3);
    assert(state.find_card(bleeder)->state.bleeding == 1);
    assert(state.find_card(vital)->state.power == 4);
    assert(state.find_card(vital)->state.vitality == 0);
    assert(state.current_player_id == kPlayerOne);
    assert(has_event(result, "bleeding_ticked"));
    assert(has_event(result, "vitality_ticked"));
    assert(has_event(result, "turn_advanced"));
}

void test_turn_start_ticks_unlocked_counters() {
    GameState state = make_running_state(kPlayerOne);
    const EntityId card_id = state.add_card(make_unit_definition("counter", "Counter", 4), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    RuntimeCard* card = state.find_card(card_id);
    assert(card != nullptr);
    card->state.timer = 2;
    card->state.cooldown = 1;
    card->state.countdown = 3;

    const EntityId p1_hand = state.add_card(make_unit_definition("p1_play", "P1 Play", 1), kPlayerOne, Location{kPlayerOne, Zone::Hand, 0});
    KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerOne, p1_hand, kPlayerOne, Zone::Melee, 0));
    assert(played);
    assert(state.find_card(card_id)->state.timer == 2);
    assert(state.find_card(card_id)->state.cooldown == 1);

    const KernelResult ended = apply_action_with_kernel(state, Action::end_turn(kPlayerOne));
    assert(ended);
    assert(state.current_player_id == kPlayerZero);
    assert(state.find_card(card_id)->state.timer == 1);
    assert(state.find_card(card_id)->state.cooldown == 0);
    assert(state.find_card(card_id)->state.countdown == 3);
    assert(has_event(ended, "timer_ticked"));
    assert(has_event(ended, "cooldown_ticked"));
    assert(!has_event(ended, "countdown_ticked"));
}

void test_passed_player_gets_virtual_turn_and_status_tick() {
    GameState state = make_running_state(kPlayerZero);
    state.player(kPlayerOne).passed = true;

    const EntityId enemy = state.add_card(make_unit_definition("enemy_bleeder", "Enemy Bleeder", 3), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.find_card(enemy)->state.bleeding = 1;

    const EntityId hand_card = state.add_card(make_unit_definition("play", "Play", 4), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    // Keep one card in hand so this END_TURN remains an ordinary turn close;
    // the test is specifically about the already-passed opponent receiving a
    // virtual turn, not about hand-exhausted auto-pass.
    state.add_card(make_unit_definition("reserve", "Reserve", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});

    const KernelResult played = apply_action_with_kernel(state, Action::play_card(kPlayerZero, hand_card, kPlayerZero, Zone::Melee, 0));
    assert(played);
    assert(state.current_player_id == kPlayerZero);

    const KernelResult result = apply_action_with_kernel(state, Action::end_turn(kPlayerZero));
    assert(result);
    assert(state.current_player_id == kPlayerZero);
    assert(state.turn_no == 3);
    assert(state.find_card(enemy)->state.power == 2);
    assert(state.find_card(enemy)->state.bleeding == 0);
    assert(has_event(result, "turn_advanced_to_passed_player"));
    assert(has_event(result, "bleeding_ticked"));
    assert(result.next_decision.has_value());
    assert(result.next_decision->player_id == kPlayerZero);
}

void test_round_cleanup_and_next_round_prepare_hand() {
    GameState state = make_running_state(kPlayerZero);
    add_deck_cards(state, kPlayerZero, 3, "p0");
    add_deck_cards(state, kPlayerOne, 1, "p1");

    const EntityId p0_normal = state.add_card(make_unit_definition("p0_normal", "P0 Normal", 6), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId p1_normal = state.add_card(make_unit_definition("p1_normal", "P1 Normal", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    CardDefinition resilient_def = make_unit_definition("resilient", "Resilient", 4);
    resilient_def.base_armor = 2;
    const EntityId resilient = state.add_card(resilient_def, kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.find_card(resilient)->state.resilience = true;
    state.find_card(resilient)->state.power = 8;
    state.find_card(resilient)->state.armor = 9;

    const EntityId stratagem = state.add_card(make_stratagem_definition("strat", "Stratagem"), kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero));
    assert(result);
    result = apply_action_with_kernel(state, Action::pass(kPlayerOne));

    assert(result);
    assert(state.status == MatchStatus::Running);
    assert(state.phase == MatchPhase::Playing);
    assert(state.round_no == 2);
    assert(state.turn_no == 1);
    assert(state.round_starting_player_id == kPlayerZero);
    assert(state.current_player_id == kPlayerZero);
    assert(state.player(kPlayerZero).round_wins == 1);
    assert(state.player(kPlayerOne).round_wins == 0);
    assert(!state.player(kPlayerZero).passed);
    assert(!state.player(kPlayerOne).passed);

    assert((state.location_of(p0_normal) == Location{kPlayerZero, Zone::Cemetery, 0}));
    assert((state.location_of(p1_normal) == Location{kPlayerOne, Zone::Cemetery, 0}));
    assert((state.location_of(stratagem) == Location{kPlayerZero, Zone::Banished, 0}));
    assert((state.location_of(resilient) == Location{kPlayerZero, Zone::Ranged, 0}));
    assert(state.find_card(resilient)->state.power == 4);
    assert(state.find_card(resilient)->state.armor == 2);
    assert(!state.find_card(resilient)->state.resilience);

    assert(state.player(kPlayerZero).hand.size() == 3);
    assert(state.player(kPlayerZero).mulligans_available == 2);
    assert(state.player(kPlayerOne).hand.size() == 1);
    assert(state.player(kPlayerOne).mulligans_available == 4);
    assert(has_event(result, "round_finished"));
    assert(has_event(result, "round_one_stratagem_banished"));
    assert(has_event(result, "next_round_prepared"));
}


void test_resilience_carries_damage_but_not_boosts() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition boosted_def = make_unit_definition("res_boosted", "Boosted Resilience", 5);
    boosted_def.base_armor = 1;
    const EntityId boosted = state.add_card(boosted_def, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.find_card(boosted)->state.resilience = true;
    state.find_card(boosted)->state.power = 9;
    state.find_card(boosted)->state.armor = 7;

    const EntityId damaged = state.add_card(make_unit_definition("res_damaged", "Damaged Resilience", 5), kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    state.find_card(damaged)->state.resilience = true;
    state.find_card(damaged)->state.power = 3;

    const EntityId exact = state.add_card(make_unit_definition("res_exact", "Exact Resilience", 5), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.find_card(exact)->state.resilience = true;
    state.find_card(exact)->state.power = 5;

    KernelResult result = apply_action_with_kernel(state, Action::pass(kPlayerZero));
    assert(result);
    result = apply_action_with_kernel(state, Action::pass(kPlayerOne));
    assert(result);

    assert(state.round_no == 2);
    assert((state.location_of(boosted) == Location{kPlayerZero, Zone::Melee, 0}));
    assert((state.location_of(damaged) == Location{kPlayerZero, Zone::Melee, 1}));
    assert((state.location_of(exact) == Location{kPlayerZero, Zone::Ranged, 0}));

    assert(state.find_card(boosted)->state.power == 5);
    assert(state.find_card(boosted)->state.armor == 1);
    assert(!state.find_card(boosted)->state.resilience);

    assert(state.find_card(damaged)->state.power == 3);
    assert(!state.find_card(damaged)->state.resilience);

    assert(state.find_card(exact)->state.power == 5);
    assert(!state.find_card(exact)->state.resilience);
    assert(has_event(result, "resilience_preserved"));
}

void test_poison_second_stack_destroys_unit() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition source_def = make_unit_definition("poisoner", "Poisoner", 4);
    source_def.effect_ids.push_back("double_poison");
    const EntityId source = state.add_card(source_def, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId target = state.add_card(make_unit_definition("target", "Target", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    registry.register_handler("double_poison", [target](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::add_status_card(call.actor_id, target, CardStatus::Poison, 1, call.source_entity_id));
        context.enqueue(Task::add_status_card(call.actor_id, target, CardStatus::Poison, 1, call.source_entity_id));
    });

    const KernelResult result = apply_action_with_kernel(state, Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0), registry);

    assert(result);
    assert((state.location_of(target) == Location{kPlayerOne, Zone::Cemetery, 0}));
    assert(has_event(result, "card_destroyed"));
}

}  // namespace

int main() {
    test_turn_end_ticks_bleeding_and_vitality();
    test_turn_start_ticks_unlocked_counters();
    test_passed_player_gets_virtual_turn_and_status_tick();
    test_round_cleanup_and_next_round_prepare_hand();
    test_resilience_carries_damage_but_not_boosts();
    test_poison_second_stack_destroys_unit();

    std::cout << "gwent_status_round_transition_tests: OK\n";
    return 0;
}
