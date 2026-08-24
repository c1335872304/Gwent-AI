#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gwent/core/state.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/action_status.hpp"
#include "gwent/engine/decision.hpp"

namespace gwent {

#define GWENT_LEGACY_REDUCER_API_VERSION 1

#if defined(GWENT_SUPPRESS_LEGACY_REDUCER_DEPRECATION)
#define GWENT_LEGACY_REDUCER_DEPRECATED
#else
#define GWENT_LEGACY_REDUCER_DEPRECATED [[deprecated("Legacy reducer API v1 is compatibility-only; use apply_action_with_kernel()")]]
#endif

struct ReducerConfig {
    int hand_limit = 10;

    // Leader/order are intra-turn actions. They do not satisfy the mandatory
    // one-hand-card consumption and therefore do not close the action window.
    bool leader_action_consumes_turn = false;
    bool order_action_consumes_turn = false;
};

struct ActionResult {
    ActionStatus status = ActionStatus::Applied;
    Action action = Action::pass(kPlayerZero);
    bool applied = false;
    std::string message;
    std::vector<std::string> events;
    std::optional<Decision> next_decision;

    [[nodiscard]] explicit operator bool() const noexcept { return applied; }
};

// Legacy compatibility reducer. New integrations should use
// apply_action_with_kernel(), which is the authoritative rules path and supports
// effects, triggers, pending choices, budgets, and transactional rollback.
// This API remains behavior-stable for historical reducer tests/callers.
GWENT_LEGACY_REDUCER_DEPRECATED [[nodiscard]] ActionResult apply_action(
    GameState& state,
    const Action& action,
    const ReducerConfig& config = {}
);

// Convenience helper for turn actions. It returns the current player's next
// decision, or std::nullopt when the match/round is no longer in a playable turn.
GWENT_LEGACY_REDUCER_DEPRECATED [[nodiscard]] std::optional<Decision> current_decision_after_reducer(const GameState& state);

#undef GWENT_LEGACY_REDUCER_DEPRECATED

}  // namespace gwent
