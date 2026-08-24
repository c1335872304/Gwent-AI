#pragma once

#include <vector>

#include "gwent/core/performance.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/decision.hpp"
#include "gwent/engine/targets.hpp"

namespace gwent {

struct LegalActionConfig {
    bool include_pass = true;
    bool include_play_card = true;
    bool include_discard_card = true;
    bool include_leader = true;
    bool include_order = true;
    PerformanceCounters* performance_counters = nullptr;
};

// Returns legal row targets for a card according to CardUseInfo. This only
// describes where an action may point; actual engine primitives will later
// decide whether a card is physically moved, resolves as a spell, or opens a
// nested target decision.
[[nodiscard]] std::vector<ActionTarget> legal_row_targets_for_card(
    const GameState& state,
    PlayerId player_id,
    const RuntimeCard& card
);

[[nodiscard]] bool can_mulligan_card(const GameState& state, PlayerId player_id, EntityId hand_card_id);
[[nodiscard]] bool can_play_card_from_hand(const GameState& state, PlayerId player_id, EntityId hand_card_id);
[[nodiscard]] bool can_use_leader(const GameState& state, PlayerId player_id);
[[nodiscard]] bool can_use_order_source(const GameState& state, PlayerId player_id, EntityId source_id);

[[nodiscard]] std::vector<ActionTarget> legal_leader_targets(
    const GameState& state,
    PlayerId player_id,
    EntityId leader_id
);
[[nodiscard]] std::vector<ActionTarget> legal_order_targets(
    const GameState& state,
    PlayerId player_id,
    EntityId source_id
);
[[nodiscard]] bool leader_action_target_is_valid(
    const GameState& state,
    PlayerId player_id,
    EntityId leader_id,
    ActionTarget target
);
[[nodiscard]] bool order_action_target_is_valid(
    const GameState& state,
    PlayerId player_id,
    EntityId source_id,
    ActionTarget target
);

[[nodiscard]] std::vector<Action> legal_mulligan_actions(const GameState& state, PlayerId player_id);
[[nodiscard]] std::vector<Action> legal_play_card_actions(const GameState& state, PlayerId player_id);
[[nodiscard]] std::vector<Action> legal_discard_card_actions(const GameState& state, PlayerId player_id);
[[nodiscard]] std::vector<Action> legal_leader_actions(const GameState& state, PlayerId player_id);
[[nodiscard]] std::vector<Action> legal_order_actions(const GameState& state, PlayerId player_id);
[[nodiscard]] std::vector<Action> legal_pending_choice_actions(const GameState& state);

[[nodiscard]] std::vector<Action> legal_turn_actions(
    const GameState& state,
    PlayerId player_id,
    const LegalActionConfig& config = {}
);

// Single authoritative legality query used by the kernel, reducer, RL masks,
// and debug tooling.  Generation remains the source of truth: this function
// asks the corresponding legal-action surface and never re-implements card or
// target predicates.
[[nodiscard]] bool is_legal_action(
    const GameState& state,
    const Action& action,
    const LegalActionConfig& config = {}
);

// Execution legality accepts the factorized decision-surface actions plus
// fully specified concrete equivalents (e.g. PlayCard(card,row)) for tests,
// tools and backward-compatible callers.  Concrete equivalents are validated
// from the same source/target predicates; they are never emitted as RL/UI
// candidates by legal_turn_actions().
[[nodiscard]] bool is_executable_action(
    const GameState& state,
    const Action& action,
    const LegalActionConfig& config = {}
);

[[nodiscard]] Decision make_mulligan_decision(const GameState& state, PlayerId player_id);
[[nodiscard]] Decision make_pending_choice_decision(const GameState& state);
[[nodiscard]] Decision make_turn_decision(
    const GameState& state,
    PlayerId player_id,
    const LegalActionConfig& config = {}
);
[[nodiscard]] Decision make_current_turn_decision(
    const GameState& state,
    const LegalActionConfig& config = {}
);

}  // namespace gwent
