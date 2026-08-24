#include "kernel_internal.hpp"

namespace gwent::kernel_detail {

KernelResult reject(Action action, ActionStatus status, std::string message) {
    KernelResult result;
    result.action = action;
    result.status = status;
    result.applied = false;
    result.message = std::move(message);
    return result;
}

bool should_validate_before(InvariantPolicy policy) noexcept {
    return policy == InvariantPolicy::BeforeApply || policy == InvariantPolicy::BeforeAndAfterApply;
}

bool should_validate_after(InvariantPolicy policy) noexcept {
    return policy == InvariantPolicy::AfterApply || policy == InvariantPolicy::BeforeAndAfterApply;
}

KernelResult reject_invariant_violation(Action action, std::string_view phase, const InvariantReport& report) {
    return reject(
        action,
        ActionStatus::InvariantViolation,
        std::string{"state invariant violation "} + std::string{phase} + " action: " + invariant_report_to_string(report)
    );
}

bool apply_post_action_invariant_policy(KernelResult& result, const GameState& state, const KernelConfig& config) {
    if (!result.applied || !should_validate_after(config.invariant_policy)) {
        return true;
    }
    InvariantOptions invariant_options = config.invariant_options;
    if (invariant_options.performance_counters == nullptr) {
        invariant_options.performance_counters = config.performance_counters;
    }
    const InvariantReport report = validate_state_invariants(state, invariant_options);
    if (report.ok()) {
        return true;
    }
    result.status = ActionStatus::InvariantViolation;
    result.applied = false;
    result.message = std::string{"state invariant violation after action: "} + invariant_report_to_string(report);
    result.next_decision.reset();
    result.events.push_back(EventRecord{TaskType::Pass, "invariant_violation", result.action.player_id, kInvalidEntityId, kInvalidEntityId, static_cast<int>(report.error_count()), "after"});
    return false;
}

bool contains_action(const std::vector<Action>& actions, const Action& action) {
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

bool is_current_turn_action(ActionType type) noexcept {
    return type == ActionType::Pass
        || type == ActionType::EndTurn
        || type == ActionType::PlayCard
        || type == ActionType::DiscardCard
        || type == ActionType::UseLeader
        || type == ActionType::UseOrder;
}
void emit(
    std::vector<EventRecord>& events,
    TaskType task_type,
    std::string name,
    PlayerId actor_id,
    EntityId source_id,
    EntityId target_id,
    int amount,
    std::string message
) {
    EventRecord event;
    event.task_type = task_type;
    event.name = std::move(name);
    event.kind = event_kind_from_name(event.name);
    event.actor_id = actor_id;
    event.source_entity_id = source_id;
    event.target_entity_id = target_id;
    event.amount = amount;
    event.message = std::move(message);
    events.push_back(std::move(event));
}
KernelResult validate_action(const GameState& state, const Action& action) {
    if (!is_valid_player_id(action.player_id)) {
        return reject(action, ActionStatus::InvalidPlayer, "action.player_id must be 0 or 1");
    }

    const bool choose_action = action.type == ActionType::ChooseCardTarget
        || action.type == ActionType::ChooseRowTarget
        || action.type == ActionType::ChooseInsertPosition;

    if (choose_action) {
        if (!state.pending_choice.has_value()) {
            return reject(action, ActionStatus::InvalidPhase, "choose-target requires a pending choice");
        }
        const PendingChoice& choice = *state.pending_choice;
        if (choice.player_id != action.player_id) {
            return reject(action, ActionStatus::NotCurrentPlayer, "pending choice must be answered by its owning player");
        }
        if (state.engine_phase != EnginePhase::AwaitingChoice) {
            return reject(action, ActionStatus::InvalidPhase, "choose-target requires AwaitingChoice engine phase");
        }
    } else if (state.pending_choice.has_value()) {
        return reject(action, ActionStatus::IllegalAction, "pending choice must be resolved before submitting another action");
    }

    if (is_current_turn_action(action.type)) {
        if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing) {
            return reject(action, ActionStatus::InvalidPhase, "turn action requires a running match in playing phase");
        }
        if (state.engine_phase != EnginePhase::ActionWindow) {
            return reject(action, ActionStatus::InvalidPhase, "turn action requires ActionWindow engine phase");
        }
        if (state.current_player_id != action.player_id) {
            return reject(action, ActionStatus::NotCurrentPlayer, "turn action must be submitted by current_player_id");
        }
    }

    // Player decisions are factorized for Q-Transformer (source first, target
    // next), while tools/tests may still submit a fully specified concrete
    // equivalent. Both paths share is_executable_action(), which derives the
    // concrete form from the same staged source/target predicates.
    if (!is_executable_action(state, action)) {
        if (choose_action) {
            return reject(action, ActionStatus::InvalidTarget, "chosen target is not present on the pending-choice legal surface");
        }
        if (action.type == ActionType::UseLeader
            && can_use_leader(state, action.player_id)
            && !leader_action_target_is_valid(state, action.player_id, action.source_entity_id, action.target)) {
            return reject(action, ActionStatus::InvalidTarget, "leader action target is not legal for this source");
        }
        if (action.type == ActionType::UseOrder
            && can_use_order_source(state, action.player_id, action.source_entity_id)
            && !order_action_target_is_valid(state, action.player_id, action.source_entity_id, action.target)) {
            return reject(action, ActionStatus::InvalidTarget, "order action target is not legal for this source");
        }
        return reject(action, ActionStatus::IllegalAction, "action is not present on the authoritative legal-action surface");
    }

    KernelResult ok;
    ok.action = action;
    ok.status = ActionStatus::Applied;
    ok.applied = true;
    return ok;
}

void enqueue_root_action(TaskQueue& queue, const Action& action) {
    switch (action.type) {
        case ActionType::Pass:
            queue.push_back(Task::pass(action.player_id));
            break;
        case ActionType::EndTurn:
            queue.push_back(Task::end_turn(action.player_id));
            break;
        case ActionType::PlayCard:
            queue.push_back(Task::play_card(action.player_id, action.source_entity_id, action.target, action.insert_position));
            break;
        case ActionType::DiscardCard:
            queue.push_back(Task::discard_card(action.player_id, action.source_entity_id, action.source_entity_id));
            break;
        case ActionType::Mulligan:
            queue.push_back(Task::mulligan(action.player_id, action.source_entity_id));
            break;
        case ActionType::KeepHand:
            queue.push_back(Task::keep_hand(action.player_id));
            break;
        case ActionType::UseLeader:
            queue.push_back(Task::use_leader(action.player_id, action.source_entity_id, action.target));
            break;
        case ActionType::UseOrder:
            queue.push_back(Task::use_order(action.player_id, action.source_entity_id, action.target));
            break;
        case ActionType::ChooseCardTarget:
        case ActionType::ChooseRowTarget:
        case ActionType::ChooseInsertPosition:
            queue.push_back(Task::resolve_pending_choice(action.player_id, action.source_entity_id, action.target, action.insert_position));
            break;
    }
}

}  // namespace gwent::kernel_detail
