#include <cassert>
#include <iostream>
#include <string>

#include "gwent/api/core.hpp"
#include "gwent/core/invariants.hpp"
#include "gwent/core/state.hpp"

using namespace gwent;

namespace {

GameState make_running_empty_state(PlayerId current_player = kPlayerZero) {
    GameState state = make_empty_standard_state();
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.engine_phase = EnginePhase::ActionWindow;
    state.round_no = 1;
    state.turn_no = 1;
    state.current_player_id = current_player;
    state.starting_player_id = current_player;
    state.round_starting_player_id = current_player;
    return state;
}

void test_fresh_deck_a_facade_is_valid_and_playable() {
    api::DeckAMatchConfig config;
    config.seed = 0;
    config.starting_player_id = kPlayerZero;

    api::DeckAGame game = api::DeckAGame::create(config);
    const auto report = game.validate();
    assert(report.ok());
    assert(game.state().status == MatchStatus::Running);
    assert(game.state().phase == MatchPhase::Playing);
    assert(!game.legal_actions().empty());
    assert(!game.snapshot_json().empty());
    assert(!game.checksum().empty());
}

void test_facade_apply_preserves_invariants() {
    api::DeckAMatchConfig config;
    config.seed = 0;
    config.starting_player_id = kPlayerZero;
    api::DeckAGame game = api::DeckAGame::create(config);

    const auto before = game.legal_actions();
    assert(!before.empty());

    const auto result = game.apply(before.front());
    assert(result);
    const auto report = game.validate();
    assert(report.ok());
}

void test_invariant_detects_duplicate_membership() {
    GameState state;
    CardDefinition unit = make_unit_definition("u", "Unit", 4);
    const EntityId id = state.add_card(unit, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    state.player(kPlayerZero).deck.push_back(id);

    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    assert(report.error_count() >= 1);
    const std::string text = invariant_report_to_string(report);
    assert(text.find("entity.membership") != std::string::npos || text.find("location.mismatch") != std::string::npos);
}

void test_invariant_detects_bad_battle_type() {
    GameState state;
    CardDefinition special = make_special_definition("s", "Special");
    state.add_card(special, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    const std::string text = invariant_report_to_string(report);
    assert(text.find("battle.type") != std::string::npos);
}


void test_invariant_allows_artifact_on_battle_row_without_power() {
    GameState state;
    CardDefinition artifact = make_artifact_definition("a", "Artifact");
    const EntityId id = state.add_card(artifact, kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});

    const auto report = validate_state_invariants(state);
    assert(report.ok());
    const RuntimeCard* card = state.find_card(id);
    assert(card != nullptr);
    assert(!card->has_power());
    assert(state.board_score(kPlayerZero) == 0);
}

void test_invariant_detects_artifact_with_power_data_on_battle_row() {
    GameState state;
    CardDefinition artifact = make_artifact_definition("bad_artifact", "Bad Artifact");
    artifact.base_power = 5;
    state.add_card(artifact, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});

    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    const std::string text = invariant_report_to_string(report);
    assert(text.find("battle.no_power_entity") != std::string::npos);
}

void test_invariant_detects_bad_pending_choice() {
    GameState state;
    state.pending_choice = PendingChoice{};
    state.pending_choice->kind = PendingChoiceKind::CardTarget;
    state.pending_choice->player_id = kPlayerZero;

    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    const std::string text = invariant_report_to_string(report);
    assert(text.find("pending.card_targets.empty") != std::string::npos);
}


void test_invariant_detects_leader_outside_leader_zone() {
    GameState state;
    state.add_card(make_leader_definition("leader", "Leader"), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    const std::string text = invariant_report_to_string(report);
    assert(text.find("leader.zone") != std::string::npos);
}

void test_invariant_detects_bad_row_effect_data() {
    GameState state;
    RowEffect bad;
    bad.id = "blood_moon";
    bad.data["duration"] = "not-an-int";
    bad.data["trigger_player_id"] = "7";
    state.player(kPlayerZero).effects_on_row(Zone::Melee).push_back(std::move(bad));

    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    const std::string text = invariant_report_to_string(report);
    assert(text.find("row_effect.duration") != std::string::npos);
    assert(text.find("row_effect.trigger_player") != std::string::npos);
}

void test_invariant_detects_banished_listener_source() {
    GameState state;
    const EntityId id = state.add_card(make_unit_definition("u", "Unit", 4), kPlayerZero, Location{kPlayerZero, Zone::Banished, 0});

    RuntimeListener listener;
    listener.listener_id = 1;
    listener.event = "turn_end";
    listener.handler = "handler";
    listener.source_id = id;
    listener.require_source = true;
    listener.source_must_be_on_board = false;
    state.listeners[listener.listener_id] = listener;

    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    const std::string text = invariant_report_to_string(report);
    assert(text.find("listener.source.banished") != std::string::npos);
}


void test_invariant_detects_engine_phase_leak() {
    GameState state = make_running_empty_state(kPlayerZero);
    state.engine_phase = EnginePhase::Resolving;
    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    assert(invariant_report_to_string(report).find("engine.action_window") != std::string::npos);
}

void test_invariant_detects_turn_context_dead_end() {
    GameState state = make_running_empty_state(kPlayerZero);
    state.turn_contexts[0].used_intra_turn_action = true;
    state.player(kPlayerZero).hand.clear();
    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    assert(invariant_report_to_string(report).find("turn.dead_end") != std::string::npos);
}

void test_invariant_detects_bad_pass_reason() {
    GameState state = make_running_empty_state(kPlayerZero);
    state.player(kPlayerOne).passed = true;
    state.player(kPlayerOne).pass_reason = PassReason::None;
    const auto report = validate_state_invariants(state);
    assert(!report.ok());
    assert(invariant_report_to_string(report).find("pass.reason_missing") != std::string::npos);
}

void test_kernel_invariant_policy_rejects_invalid_state_before_apply() {
    api::DeckAMatchConfig config;
    config.seed = 0;
    config.starting_player_id = kPlayerZero;
    config.kernel_config.invariant_policy = InvariantPolicy::BeforeApply;
    api::DeckAGame game = api::DeckAGame::create(config);

    const EntityId duplicate = game.state().player(kPlayerZero).hand.front();
    game.mutable_state().player(kPlayerZero).deck.push_back(duplicate);

    const auto result = game.apply(Action::pass(kPlayerZero));
    assert(!result);
    assert(result.status == ActionStatus::InvariantViolation);
    assert(result.message.find("before action") != std::string::npos);
    assert(result.message.find("entity.membership") != std::string::npos || result.message.find("location.mismatch") != std::string::npos);
}

void test_kernel_invariant_policy_rejects_invalid_state_after_apply() {
    GameState state = make_running_empty_state(kPlayerZero);
    CardDefinition corrupt = make_unit_definition("corrupt", "Corrupt", 4);
    corrupt.effect_ids.push_back("deploy:corrupt.deploy");
    const EntityId source = state.add_card(corrupt, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    EffectRegistry registry;
    registry.register_handler("corrupt.deploy", [](KernelContext& context, const EffectCall& call) {
        context.state.player(call.actor_id).hand.push_back(call.source_entity_id);
    });

    KernelConfig config;
    config.invariant_policy = InvariantPolicy::AfterApply;
    const auto result = apply_action_with_kernel(state, Action::play_card(kPlayerZero, source, kPlayerZero, Zone::Melee, 0), registry, config);

    assert(!result);
    assert(result.status == ActionStatus::InvariantViolation);
    assert(result.message.find("after action") != std::string::npos);
    assert(result.message.find("entity.membership") != std::string::npos || result.message.find("location.mismatch") != std::string::npos);
    assert((state.location_of(source) == Location{kPlayerZero, Zone::Hand, 0}));
    assert(state.player(kPlayerZero).row(Zone::Melee).empty());
}

}  // namespace

int main() {
    test_fresh_deck_a_facade_is_valid_and_playable();
    test_facade_apply_preserves_invariants();
    test_invariant_detects_duplicate_membership();
    test_invariant_detects_bad_battle_type();
    test_invariant_allows_artifact_on_battle_row_without_power();
    test_invariant_detects_artifact_with_power_data_on_battle_row();
    test_invariant_detects_bad_pending_choice();
    test_invariant_detects_leader_outside_leader_zone();
    test_invariant_detects_bad_row_effect_data();
    test_invariant_detects_banished_listener_source();
    test_invariant_detects_engine_phase_leak();
    test_invariant_detects_turn_context_dead_end();
    test_invariant_detects_bad_pass_reason();
    test_kernel_invariant_policy_rejects_invalid_state_before_apply();
    test_kernel_invariant_policy_rejects_invalid_state_after_apply();

    std::cout << "gwent_invariant_api_tests: OK\n";
    return 0;
}
