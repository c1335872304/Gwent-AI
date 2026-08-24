#include "gwent/engine/kernel.hpp"

#include <cstddef>
#include <memory>
#include <optional>

#include "kernel_internal.hpp"

namespace gwent {
namespace {

EnginePhase external_engine_phase(const GameState& state) noexcept {
    if (state.status == MatchStatus::Finished || state.phase == MatchPhase::Finished) {
        return EnginePhase::Finished;
    }
    if (state.pending_choice.has_value()) {
        return EnginePhase::AwaitingChoice;
    }
    if (state.status == MatchStatus::Running && state.phase == MatchPhase::Playing) {
        return EnginePhase::ActionWindow;
    }
    return EnginePhase::NotStarted;
}

EnginePhase engine_phase_for_task(TaskType type) noexcept {
    switch (type) {
        case TaskType::EndTurn:
        case TaskType::Pass:
        case TaskType::AdvanceTurnOrFinishRound:
        case TaskType::ApplyTurnTransition:
        case TaskType::DispatchTurnStartRowEffects:
        case TaskType::DispatchTurnStart:
        case TaskType::TickTurnCounters:
        case TaskType::TickTurnStatuses:
        case TaskType::DispatchTurnEnd:
            return EnginePhase::TurnClosing;
        case TaskType::FinalizeRoundEnd:
        case TaskType::RoundCleanup:
        case TaskType::DispatchRoundStart:
            return EnginePhase::RoundClosing;
        default:
            return EnginePhase::Resolving;
    }
}

ResolutionLimits limits_from_config(const KernelConfig& config) noexcept {
    ResolutionLimits limits;
    limits.max_tasks = config.max_tasks_per_action != 0
        ? config.max_tasks_per_action
        : config.max_tasks_per_resolution;
    limits.max_trigger_invocations = config.max_trigger_invocations;
    limits.max_effect_invocations = config.max_effect_invocations;
    limits.max_choices = config.max_choice_depth;
    limits.max_resolution_depth = config.max_resolution_depth;
    return limits;
}

std::shared_ptr<ResolutionFrame> resolution_frame_for_action(
    const GameState& state,
    const Action& action,
    const KernelConfig& config,
    const GameState& state_before
) {
    if ((action.type == ActionType::ChooseCardTarget
            || action.type == ActionType::ChooseRowTarget
            || action.type == ActionType::ChooseInsertPosition)
        && state.pending_choice.has_value()
        && state.pending_choice->resolution_frame) {
        return state.pending_choice->resolution_frame;
    }

    auto frame = std::make_shared<ResolutionFrame>();
    frame->root_action = action;
    frame->limits = limits_from_config(config);
    frame->root_state_before = std::make_shared<GameState>(state_before);
    return frame;
}

void rollback_root_resolution(GameState& state, const std::shared_ptr<ResolutionFrame>& frame, GameState&& fallback) {
    if (frame && frame->root_state_before) {
        state = *frame->root_state_before;
    } else {
        state = std::move(fallback);
    }
}

}  // namespace

KernelResult apply_action_with_kernel(
    GameState& state,
    const Action& action,
    const EffectRegistry& effects,
    const KernelConfig& config
) {
    PerformanceScope apply_scope(config.performance_counters, PerformanceMetric::Apply);

    // Player-facing calls only occur at stable engine boundaries. Normalizing
    // here also upgrades old handcrafted/debug states that predate EnginePhase.
    state.engine_phase = external_engine_phase(state);

    if (kernel_detail::should_validate_before(config.invariant_policy)) {
        InvariantOptions invariant_options = config.invariant_options;
        if (invariant_options.performance_counters == nullptr) {
            invariant_options.performance_counters = config.performance_counters;
        }
        const InvariantReport report = validate_state_invariants(state, invariant_options);
        if (!report.ok()) {
            return kernel_detail::reject_invariant_violation(action, "before", report);
        }
    }

    KernelResult result = kernel_detail::validate_action(state, action);
    if (!result.applied) {
        return result;
    }

    // Snapshot the externally visible state for legacy/fallback rollback. For a
    // staged resolution, the shared ResolutionFrame additionally retains the
    // snapshot from before the root action and survives every PendingChoice.
    GameState state_before = state;
    const std::shared_ptr<ResolutionFrame> frame = resolution_frame_for_action(state, action, config, state_before);

    // The ordered prefix is the exact sequence of player decisions already made
    // in this root resolution. The root source action is token 0; each staged
    // target choice appends one more token before resolution continues.
    frame->decision_prefix.push_back(decision_token_from_action(action));

    try {
        TaskQueue queue;
        kernel_detail::enqueue_root_action(queue, action);

        while (!queue.empty()) {
            consume_task_budget(*frame);
            std::optional<Task> task = queue.pop_front();
            if (!task.has_value()) {
                break;
            }
            record_kernel_task_processed(config.performance_counters);
            state.engine_phase = engine_phase_for_task(task->type);
            kernel_detail::process_task(state, queue, result, effects, config, frame, task.value());
            if (!result.applied) {
                break;
            }
            if (state.pending_choice.has_value()) {
                // request_*_choice() has moved the remaining FIFO tail into the
                // PendingChoice. Trigger resume state and budgets live in frame.
                state.engine_phase = EnginePhase::AwaitingChoice;
                break;
            }
        }

        if (result.applied) {
            // Invariants are checked only at externally observable stable
            // boundaries, never in the middle of a partially resolved chain.
            state.engine_phase = external_engine_phase(state);
            (void)kernel_detail::apply_post_action_invariant_policy(result, state, config);
        }
    } catch (const ResolutionBudgetExceeded& ex) {
        result.status = ActionStatus::TaskLimitExceeded;
        result.applied = false;
        result.message = ex.what();
    } catch (...) {
        rollback_root_resolution(state, frame, std::move(state_before));
        throw;
    }

    if (!result.applied) {
        rollback_root_resolution(state, frame, std::move(state_before));
        result.next_decision.reset();
        result.events.push_back(EventRecord{
            TaskType::Pass,
            "action_rolled_back",
            action.player_id,
            action.source_entity_id,
            kInvalidEntityId,
            0,
            result.message
        });
        return result;
    }

    state.engine_phase = external_engine_phase(state);
    kernel_detail::append_next_decision(state, result);
    return result;
}

KernelResult apply_action_with_kernel(GameState& state, const Action& action, const KernelConfig& config) {
    const EffectRegistry empty_registry;
    return apply_action_with_kernel(state, action, empty_registry, config);
}

}  // namespace gwent
