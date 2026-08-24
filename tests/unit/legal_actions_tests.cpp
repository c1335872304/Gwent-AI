#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

#include "gwent/cards/deck_a.hpp"
#include "gwent/core/card_definition.hpp"
#include "gwent/game/setup.hpp"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/engine/kernel.hpp"

using namespace gwent;

namespace {

CardDefinition make_leader(std::string id = "leader") {
    CardDefinition leader;
    leader.id = std::move(id);
    leader.name = "Leader";
    leader.card_type = CardType::Leader;
    return leader;
}

DeckSpec make_deck(std::string prefix, int copies) {
    DeckSpec deck;
    deck.leader = make_leader(prefix + "_leader");
    deck.stratagem = make_stratagem_definition(prefix + "_stratagem", prefix + " Stratagem");
    deck.cards.push_back(DeckCardSpec{make_unit_definition(prefix + "_soldier", prefix + " Soldier", 4), copies});
    return deck;
}

CardDefinition make_hand_card(std::string id, CardUseInfo use_info = CardUseInfo::MyPlace) {
    auto card = make_unit_definition(std::move(id), "Hand Unit", 4);
    card.use_info = use_info;
    return card;
}

bool has_action(const std::vector<Action>& actions, Action expected) {
    return std::find(actions.begin(), actions.end(), expected) != actions.end();
}

std::size_t count_actions(const std::vector<Action>& actions, ActionType type) {
    return static_cast<std::size_t>(std::count_if(actions.begin(), actions.end(), [type](const Action& action) {
        return action.type == type;
    }));
}

void test_action_value_contract() {
    const Action pass = Action::pass(kPlayerZero);
    assert(pass.type == ActionType::Pass);
    assert(pass.player_id == kPlayerZero);
    assert(pass.source_entity_id == kInvalidEntityId);
    assert(pass.target.kind == ActionTargetKind::None);

    const Action play = Action::play_card(kPlayerOne, 42, kPlayerZero, Zone::Ranged, 0);
    assert(play.type == ActionType::PlayCard);
    assert(play.source_entity_id == 42);
    assert(play.target.kind == ActionTargetKind::Row);
    assert(play.target.side == kPlayerZero);
    assert(play.target.zone == Zone::Ranged);
    assert(play.insert_position == 0);
    assert(play != Action::play_card(kPlayerOne, 42, kPlayerZero, Zone::Ranged, 1));

    assert(to_string(ActionType::UseOrder) == "USE_ORDER");
    assert(to_string(ActionTargetKind::Row) == "ROW");
    assert(to_string(DecisionType::TurnAction) == "TURN_ACTION");
}

void test_mulligan_actions_require_allowance_and_deck_card() {
    GameState state = make_empty_standard_state();
    const EntityId h0 = state.add_card(make_hand_card("h0"), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId h1 = state.add_card(make_hand_card("h1"), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    state.add_card(make_hand_card("d0"), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.round_starting_player_id = kPlayerZero;
    state.mulligan_player_id = kPlayerZero;
    state.player(kPlayerZero).mulligans_available = 1;

    const auto actions = legal_mulligan_actions(state, kPlayerZero);
    assert(actions.size() == 3);
    assert(has_action(actions, Action::keep_hand(kPlayerZero)));
    assert(has_action(actions, Action::mulligan(kPlayerZero, h0)));
    assert(has_action(actions, Action::mulligan(kPlayerZero, h1)));

    state.player(kPlayerZero).mulligans_available = 0;
    const auto keep_only = legal_mulligan_actions(state, kPlayerZero);
    assert(keep_only.size() == 1);
    assert(has_action(keep_only, Action::keep_hand(kPlayerZero)));

    state.player(kPlayerZero).mulligans_available = 1;
    state.player(kPlayerZero).deck.clear();
    state.locations.at(2).reset();
    const auto no_deck = legal_mulligan_actions(state, kPlayerZero);
    assert(no_deck.size() == 1);
    assert(has_action(no_deck, Action::keep_hand(kPlayerZero)));
}

void test_play_card_row_targets_follow_use_info() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;

    const EntityId mine = state.add_card(make_hand_card("mine", CardUseInfo::MyPlace), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_hand_card("enemy", CardUseInfo::EnemyPlace), kPlayerZero, Location{kPlayerZero, Zone::Hand, 1});
    const EntityId any = state.add_card(make_hand_card("any", CardUseInfo::AnyPlace), kPlayerZero, Location{kPlayerZero, Zone::Hand, 2});

    // Turn surface is factorized: choose the hand card first, then choose the
    // deployment row in a RowTarget decision. No card x row duplicates here.
    const auto actions = legal_play_card_actions(state, kPlayerZero);
    assert(actions.size() == 3);
    assert(has_action(actions, Action::play_card(kPlayerZero, mine)));
    assert(has_action(actions, Action::play_card(kPlayerZero, enemy)));
    assert(has_action(actions, Action::play_card(kPlayerZero, any)));

    const auto mine_rows = legal_row_targets_for_card(state, kPlayerZero, *state.find_card(mine));
    assert(std::find(mine_rows.begin(), mine_rows.end(), ActionTarget::row(kPlayerZero, Zone::Melee)) != mine_rows.end());
    assert(std::find(mine_rows.begin(), mine_rows.end(), ActionTarget::row(kPlayerZero, Zone::Ranged)) != mine_rows.end());
    assert(std::find(mine_rows.begin(), mine_rows.end(), ActionTarget::row(kPlayerOne, Zone::Melee)) == mine_rows.end());

    const auto enemy_rows = legal_row_targets_for_card(state, kPlayerZero, *state.find_card(enemy));
    assert(std::find(enemy_rows.begin(), enemy_rows.end(), ActionTarget::row(kPlayerOne, Zone::Melee)) != enemy_rows.end());
    assert(std::find(enemy_rows.begin(), enemy_rows.end(), ActionTarget::row(kPlayerOne, Zone::Ranged)) != enemy_rows.end());
    assert(std::find(enemy_rows.begin(), enemy_rows.end(), ActionTarget::row(kPlayerZero, Zone::Melee)) == enemy_rows.end());

    const auto any_rows = legal_row_targets_for_card(state, kPlayerZero, *state.find_card(any));
    assert(any_rows.size() == 4);
}

void test_standard_setup_current_turn_includes_stratagem_order() {
    GameState state;
    MatchSetupConfig config;
    config.starting_player_id = kPlayerZero;
    config.shuffle_decks = false;

    const OpeningSetupResult setup = setup_standard_match(state, make_deck("p0", 12), make_deck("p1", 12), config);

    const Decision mulligan0 = make_current_turn_decision(state);
    assert(mulligan0.type == DecisionType::Mulligan);
    assert(mulligan0.player_id == kPlayerZero);
    assert(has_action(mulligan0.legal_actions, Action::keep_hand(kPlayerZero)));

    auto keep0 = apply_action_with_kernel(state, Action::keep_hand(kPlayerZero));
    assert(keep0.applied);
    const Decision mulligan1 = make_current_turn_decision(state);
    assert(mulligan1.type == DecisionType::Mulligan);
    assert(mulligan1.player_id == kPlayerOne);
    auto keep1 = apply_action_with_kernel(state, Action::keep_hand(kPlayerOne));
    assert(keep1.applied);

    const Decision decision = make_current_turn_decision(state);
    assert(decision.type == DecisionType::TurnAction);
    assert(decision.player_id == kPlayerZero);
    assert(decision.has_actions());
    assert(has_action(decision.legal_actions, Action::pass(kPlayerZero)));
    assert(has_action(decision.legal_actions, Action::use_order(kPlayerZero, setup.stratagem_entity_id)));

    // Stratagems are command entities already on the board. They must not be
    // offered as hand PlayCard actions.
    for (const Action& action : decision.legal_actions) {
        assert(!(action.type == ActionType::PlayCard && action.source_entity_id == setup.stratagem_entity_id));
    }

    assert(count_actions(decision.legal_actions, ActionType::UseOrder) == 1);
}

void test_passed_player_has_no_turn_actions() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.player(kPlayerZero).passed = true;
    state.add_card(make_hand_card("h0"), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    const Decision decision = make_turn_decision(state, kPlayerZero);
    assert(decision.type == DecisionType::TurnAction);
    assert(!decision.has_actions());
}

void test_leader_action_disappears_after_use_flag() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    const EntityId leader_id = state.add_card(make_leader(), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});

    auto actions = legal_leader_actions(state, kPlayerZero);
    assert(actions.size() == 1);
    assert(has_action(actions, Action::use_leader(kPlayerZero, leader_id)));

    state.player(kPlayerZero).leader_used = true;
    assert(legal_leader_actions(state, kPlayerZero).empty());
}

void test_intra_turn_action_does_not_reenable_pass_without_hand() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.round_no = 2;
    state.turn_no = 26;
    state.current_player_id = kPlayerZero;
    state.turn_contexts[static_cast<std::size_t>(kPlayerZero)].used_intra_turn_action = true;

    const Decision decision = make_current_turn_decision(state);
    assert(decision.type == DecisionType::TurnAction);
    assert(decision.player_id == kPlayerZero);
    assert(decision.legal_actions.empty());
}

void test_no_pass_after_intra_turn_action_still_hides_pass_when_followup_exists() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.current_player_id = kPlayerZero;
    state.turn_contexts[static_cast<std::size_t>(kPlayerZero)].used_intra_turn_action = true;
    const EntityId hand_card = state.add_card(make_hand_card("followup"), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    const Decision decision = make_current_turn_decision(state);
    assert(decision.type == DecisionType::TurnAction);
    assert(!has_action(decision.legal_actions, Action::pass(kPlayerZero)));
    assert(has_action(decision.legal_actions, Action::play_card(kPlayerZero, hand_card)));
}

void test_intra_turn_actions_are_hidden_when_no_hand_card_can_be_consumed() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.current_player_id = kPlayerZero;

    const EntityId leader = state.add_card(make_leader(), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    const EntityId stratagem = state.add_card(make_stratagem_definition("no_hand_strat", "No Hand Strat"), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    Decision decision = make_current_turn_decision(state);
    assert(has_action(decision.legal_actions, Action::pass(kPlayerZero)));
    assert(!has_action(decision.legal_actions, Action::use_leader(kPlayerZero, leader)));
    assert(!has_action(decision.legal_actions, Action::use_order(kPlayerZero, stratagem)));

    state.add_card(make_hand_card("followup_available"), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    decision = make_current_turn_decision(state);
    assert(has_action(decision.legal_actions, Action::use_leader(kPlayerZero, leader)));
    assert(has_action(decision.legal_actions, Action::use_order(kPlayerZero, stratagem)));
}

void test_order_target_metadata_generates_and_validates_card_targets() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;

    const EntityId garkain = state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId enemy_unit = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId ally_unit = state.add_card(make_unit_definition("ally", "Ally", 5, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    const EntityId enemy_artifact = state.add_card(make_artifact_definition("enemy_artifact", "Enemy Artifact", Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});

    const auto targets = legal_order_targets(state, kPlayerZero, garkain);
    assert(std::find(targets.begin(), targets.end(), ActionTarget::none()) != targets.end());
    assert(std::find(targets.begin(), targets.end(), ActionTarget::card(enemy_unit)) != targets.end());
    assert(std::find(targets.begin(), targets.end(), ActionTarget::card(ally_unit)) == targets.end());
    assert(std::find(targets.begin(), targets.end(), ActionTarget::card(enemy_artifact)) == targets.end());

    const auto actions = legal_order_actions(state, kPlayerZero);
    assert(has_action(actions, Action::use_order(kPlayerZero, garkain)));
    assert(actions.size() == 1);
    assert(!has_action(actions, Action::use_order(kPlayerZero, garkain, ActionTarget::card(enemy_unit))));

    assert(order_action_target_is_valid(state, kPlayerZero, garkain, ActionTarget::none()));
    assert(order_action_target_is_valid(state, kPlayerZero, garkain, ActionTarget::card(enemy_unit)));
    assert(!order_action_target_is_valid(state, kPlayerZero, garkain, ActionTarget::card(ally_unit)));
    assert(!order_action_target_is_valid(state, kPlayerZero, garkain, ActionTarget::card(enemy_artifact)));
}

void test_bleeding_enemy_metadata_filters_dettlaff_order_targets() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;

    const EntityId dettlaff = state.add_card(deck_a::make_dettlaff_aep_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId bleeding_enemy = state.add_card(make_unit_definition("bleeding", "Bleeding Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId clean_enemy = state.add_card(make_unit_definition("clean", "Clean Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});
    state.find_card(bleeding_enemy)->state.bleeding = 2;

    const auto targets = legal_order_targets(state, kPlayerZero, dettlaff);
    assert(std::find(targets.begin(), targets.end(), ActionTarget::card(bleeding_enemy)) != targets.end());
    assert(std::find(targets.begin(), targets.end(), ActionTarget::card(clean_enemy)) == targets.end());

    assert(order_action_target_is_valid(state, kPlayerZero, dettlaff, ActionTarget::card(bleeding_enemy)));
    assert(!order_action_target_is_valid(state, kPlayerZero, dettlaff, ActionTarget::card(clean_enemy)));
}

void test_leader_target_metadata_uses_sequential_target_choice() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;

    const EntityId leader = state.add_card(deck_a::make_blood_scent_definition(), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    const EntityId enemy_unit = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId ally_unit = state.add_card(make_unit_definition("ally", "Ally", 5, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    const auto actions = legal_leader_actions(state, kPlayerZero);
    assert(has_action(actions, Action::use_leader(kPlayerZero, leader)));
    assert(actions.size() == 1);
    assert(!has_action(actions, Action::use_leader(kPlayerZero, leader, ActionTarget::card(enemy_unit))));

    assert(leader_action_target_is_valid(state, kPlayerZero, leader, ActionTarget::card(enemy_unit)));
    assert(!leader_action_target_is_valid(state, kPlayerZero, leader, ActionTarget::card(ally_unit)));
}

}  // namespace

int main() {
    test_action_value_contract();
    test_mulligan_actions_require_allowance_and_deck_card();
    test_play_card_row_targets_follow_use_info();
    test_standard_setup_current_turn_includes_stratagem_order();
    test_passed_player_has_no_turn_actions();
    test_leader_action_disappears_after_use_flag();
    test_intra_turn_action_does_not_reenable_pass_without_hand();
    test_no_pass_after_intra_turn_action_still_hides_pass_when_followup_exists();
    test_intra_turn_actions_are_hidden_when_no_hand_card_can_be_consumed();
    test_order_target_metadata_generates_and_validates_card_targets();
    test_bleeding_enemy_metadata_filters_dettlaff_order_targets();
    test_leader_target_metadata_uses_sequential_target_choice();

    std::cout << "gwent_legal_actions_tests: OK\n";
    return 0;
}
