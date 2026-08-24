#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gwent/core/invariants.hpp"
#include "gwent/core/performance.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/action_status.hpp"
#include "gwent/engine/decision.hpp"
#include "gwent/engine/effect_registry.hpp"
#include "gwent/engine/resolution_frame.hpp"
#include "gwent/engine/task_queue.hpp"

namespace gwent {

enum class InvariantPolicy : std::uint8_t {
    Disabled,
    BeforeApply,
    AfterApply,
    BeforeAndAfterApply,
};

struct KernelConfig {
    int hand_limit = 10;
    bool leader_action_consumes_turn = false;
    bool order_action_consumes_turn = false;
    // Resolution-wide safety limits. They persist across PendingChoice resume
    // calls instead of resetting for each external click.
    std::size_t max_tasks_per_resolution = 4096;
    std::size_t max_trigger_invocations = 4096;
    std::size_t max_effect_invocations = 4096;
    std::size_t max_choice_depth = 32;
    std::size_t max_resolution_depth = 64;

    // Deprecated compatibility alias. 0 means use max_tasks_per_resolution.
    std::size_t max_tasks_per_action = 0;

    // Optional debug/test safety net.  Keep Disabled by default so release users
    // can opt in without changing the historical hot path.
    InvariantPolicy invariant_policy = InvariantPolicy::Disabled;
    InvariantOptions invariant_options{};

    // Optional caller-owned metrics sink. When null, profiling is completely
    // disabled apart from a predictable null check at instrumented boundaries.
    PerformanceCounters* performance_counters = nullptr;
};

struct KernelResult {
    ActionStatus status = ActionStatus::Applied;
    Action action = Action::pass(kPlayerZero);
    bool applied = false;
    std::string message;
    std::vector<EventRecord> events;
    std::optional<Decision> next_decision;

    [[nodiscard]] explicit operator bool() const noexcept { return applied; }
};

// Executes one player-facing action by translating it into engine tasks, then
// draining the task queue. Built-in tasks cover core movement, pass/mulligan,
// leader/order consumption, basic boost/damage/destroy/spawn, and turn/round
// advancement. Card-specific behavior is injected through EffectRegistry.
[[nodiscard]] KernelResult apply_action_with_kernel(
    GameState& state,
    const Action& action,
    const EffectRegistry& effects,
    const KernelConfig& config = {}
);

// Convenience overload for callers that do not need card-specific effects yet.
[[nodiscard]] KernelResult apply_action_with_kernel(
    GameState& state,
    const Action& action,
    const KernelConfig& config = {}
);

}  // namespace gwent
