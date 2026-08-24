#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/engine/reducer.hpp"
#include "gwent/game/setup.hpp"

using namespace gwent;

namespace {

CardDefinition make_leader(std::string id = "leader") {
    CardDefinition leader;
    leader.id = std::move(id);
    leader.name = "Leader";
    leader.card_type = CardType::Leader;
    return leader;
}

DeckSpec make_deck(std::string prefix, int copies, int power = 4) {
    DeckSpec deck;
    deck.leader = make_leader(prefix + "_leader");
    deck.stratagem = make_stratagem_definition(prefix + "_stratagem", prefix + " Stratagem");
    deck.cards.push_back(DeckCardSpec{make_unit_definition(prefix + "_soldier", prefix + " Soldier", power), copies});
    return deck;
}

CardDefinition make_hand_unit(std::string id, int power = 4, CardUseInfo use_info = CardUseInfo::MyPlace) {
    CardDefinition card = make_unit_definition(std::move(id), "Hand Unit", power);
    card.use_info = use_info;
    return card;
}

bool has_event(const ActionResult& result, const std::string& event) {
    return std::find(result.events.begin(), result.events.end(), event) != result.events.end();
}

void test_play_card_moves_hand_unit_and_requires_explicit_end_turn() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.round_no = 1;
    state.turn_no = 1;
    state.current_player_id = kPlayerZero;

    const EntityId card_id = state.add_card(make_hand_unit("p0_unit", 5), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    const ActionResult result = apply_action(state, Action::play_card(kPlayerZero, card_id, kPlayerZero, Zone::Melee, 0));

    assert(result.applied);
    assert(result.status == ActionStatus::Applied);
    assert(has_event(result, "card_played:no_effects"));
    assert(!has_event(result, "turn_advanced"));
    assert(result.next_decision.has_value());
    assert(result.next_decision->player_id == kPlayerZero);

    assert(state.player(kPlayerZero).hand.empty());
    assert(state.player(kPlayerZero).row(Zone::Melee).size() == 1);
    assert(state.location_of(card_id).value() == (Location{kPlayerZero, Zone::Melee, 0}));
    assert(state.board_score(kPlayerZero) == 5);
    assert(state.current_player_id == kPlayerZero);
    assert(state.turn_no == 1);
    assert(state.turn_contexts[0].consumed_hand_card);

    const ActionResult ended = apply_action(state, Action::end_turn(kPlayerZero));
    assert(ended.applied);
    assert(has_event(ended, "hand_exhausted_pass"));
    assert(has_event(ended, "turn_advanced"));
    assert(state.player(kPlayerZero).passed);
    assert(state.player(kPlayerZero).pass_reason == PassReason::HandExhausted);
    assert(state.current_player_id == kPlayerOne);
    assert(state.turn_no == 2);
}

void test_play_card_rejects_non_current_player() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.current_player_id = kPlayerZero;
    const EntityId card_id = state.add_card(make_hand_unit("p1_unit"), kPlayerOne, Location{kPlayerOne, Zone::Hand, 0});

    const ActionResult result = apply_action(state, Action::play_card(kPlayerOne, card_id, kPlayerOne, Zone::Melee, 0));

    assert(!result.applied);
    assert(result.status == ActionStatus::NotCurrentPlayer);
    assert(state.player(kPlayerOne).hand.size() == 1);
    assert(state.player(kPlayerOne).row(Zone::Melee).empty());
}

void test_mulligan_swaps_one_card_without_advancing_turn() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.current_player_id = kPlayerZero;
    state.round_starting_player_id = kPlayerZero;
    state.mulligan_player_id = kPlayerZero;

    const EntityId hand_id = state.add_card(make_hand_unit("old"), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId deck_id = state.add_card(make_hand_unit("new"), kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    state.player(kPlayerZero).mulligans_available = 1;

    const ActionResult result = apply_action(state, Action::mulligan(kPlayerZero, hand_id));

    assert(result.applied);
    assert(result.status == ActionStatus::Applied);
    assert(has_event(result, "mulligan"));
    assert(has_event(result, "mulligan_replacement_drawn"));
    assert(result.next_decision.has_value());
    assert(result.next_decision->type == DecisionType::Mulligan);
    assert(result.next_decision->player_id == kPlayerOne);

    assert(state.player(kPlayerZero).mulligans_available == 0);
    assert(state.player(kPlayerZero).hand.size() == 1);
    assert(state.player(kPlayerZero).hand.front() == deck_id);
    assert(state.player(kPlayerZero).deck.size() == 1);
    assert(state.player(kPlayerZero).deck.front() == hand_id);
    assert(state.location_of(deck_id).value() == (Location{kPlayerZero, Zone::Hand, 0}));
    assert(state.location_of(hand_id).value() == (Location{kPlayerZero, Zone::Deck, 0}));
    assert(state.current_player_id == kPlayerZero);
    assert(state.turn_no == 0);
}

void test_pass_sets_flag_and_finishes_round_when_both_passed() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.round_no = 1;
    state.turn_no = 1;
    state.current_player_id = kPlayerZero;

    const ActionResult first = apply_action(state, Action::pass(kPlayerZero));
    assert(first.applied);
    assert(state.player(kPlayerZero).passed);
    assert(state.player(kPlayerZero).pass_reason == PassReason::Explicit);
    assert(!state.player(kPlayerOne).passed);
    assert(state.current_player_id == kPlayerOne);
    assert(state.phase == MatchPhase::Playing);

    const ActionResult second = apply_action(state, Action::pass(kPlayerOne));
    assert(second.applied);
    assert(state.player(kPlayerOne).passed);
    assert(state.phase == MatchPhase::Finished);
    assert(state.status == MatchStatus::Running);
    assert(state.player(kPlayerZero).round_wins == 1);
    assert(state.player(kPlayerOne).round_wins == 1);
    assert(!second.next_decision.has_value());
    assert(has_event(second, "round_finished:winner=tie"));
}

void test_match_finishes_when_second_round_win_is_reached() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.round_no = 2;
    state.current_player_id = kPlayerZero;
    state.player(kPlayerZero).round_wins = 1;

    const EntityId winner_unit = state.add_card(make_hand_unit("winner", 7), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    (void)winner_unit;

    assert(state.board_score(kPlayerZero) == 7);
    assert(state.board_score(kPlayerOne) == 0);

    assert(apply_action(state, Action::pass(kPlayerZero)).applied);
    const ActionResult end = apply_action(state, Action::pass(kPlayerOne));

    assert(end.applied);
    assert(state.status == MatchStatus::Finished);
    assert(state.phase == MatchPhase::Finished);
    assert(state.winner_id.has_value());
    assert(state.winner_id.value() == kPlayerZero);
    assert(state.player(kPlayerZero).round_wins == 2);
    assert(has_event(end, "match_finished"));
}

void test_stratagem_order_is_consumed_as_command_entity() {
    GameState state;
    MatchSetupConfig config;
    config.starting_player_id = kPlayerZero;
    config.shuffle_decks = false;
    const OpeningSetupResult setup = setup_standard_match(state, make_deck("p0", 10), make_deck("p1", 10), config);
    // 本测试只验证 Stratagem/Order，不验证开局换牌；显式进入正常回合窗口。
    state.mulligan_player_id.reset();
    state.player(kPlayerZero).mulligans_available = 0;
    state.player(kPlayerOne).mulligans_available = 0;

    const ActionResult result = apply_action(state, Action::use_order(kPlayerZero, setup.stratagem_entity_id));

    assert(result.applied);
    assert(result.status == ActionStatus::Applied);
    assert(has_event(result, "stratagem_order_used:no_effects"));
    assert(state.player(kPlayerZero).row(Zone::Melee).empty());
    assert(state.player(kPlayerZero).banished.size() == 1);
    assert(state.player(kPlayerZero).banished.front() == setup.stratagem_entity_id);
    assert(state.board_score(kPlayerZero) == 0);
    assert(state.current_player_id == kPlayerZero);
    assert(state.turn_contexts[0].used_intra_turn_action);
}

void test_leader_use_stays_in_action_window_and_requires_hand_followup() {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.current_player_id = kPlayerZero;
    const EntityId leader_id = state.add_card(make_leader(), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});
    state.add_card(make_hand_unit("leader_followup"), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    const ActionResult result = apply_action(state, Action::use_leader(kPlayerZero, leader_id));

    assert(result.applied);
    assert(state.player(kPlayerZero).leader_used);
    assert(state.current_player_id == kPlayerZero);
    assert(state.turn_contexts[0].used_intra_turn_action);
    assert(has_event(result, "leader_used:no_effects"));
    assert(result.next_decision.has_value());
    assert(std::find(result.next_decision->legal_actions.begin(), result.next_decision->legal_actions.end(), Action::pass(kPlayerZero)) == result.next_decision->legal_actions.end());
}

}  // namespace

int main() {
    test_play_card_moves_hand_unit_and_requires_explicit_end_turn();
    test_play_card_rejects_non_current_player();
    test_mulligan_swaps_one_card_without_advancing_turn();
    test_pass_sets_flag_and_finishes_round_when_both_passed();
    test_match_finishes_when_second_round_win_is_reached();
    test_stratagem_order_is_consumed_as_command_entity();
    test_leader_use_stays_in_action_window_and_requires_hand_followup();

    std::cout << "gwent_reducer_tests: OK\n";
    return 0;
}
