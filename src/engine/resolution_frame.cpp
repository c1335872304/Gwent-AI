#include "gwent/engine/resolution_frame.hpp"

#include <algorithm>

namespace gwent {
namespace {

void require_within(std::size_t value, std::size_t limit, const char* label) {
    if (limit != 0 && value > limit) {
        throw ResolutionBudgetExceeded(std::string(label) + " exceeded resolution limit");
    }
}

}  // namespace

void consume_task_budget(ResolutionFrame& frame) {
    ++frame.budget.tasks_processed;
    require_within(frame.budget.tasks_processed, frame.limits.max_tasks, "task budget");
}

void consume_trigger_budget(ResolutionFrame& frame) {
    ++frame.budget.trigger_invocations;
    require_within(frame.budget.trigger_invocations, frame.limits.max_trigger_invocations, "trigger budget");
}

void consume_effect_budget(ResolutionFrame& frame) {
    ++frame.budget.effect_invocations;
    require_within(frame.budget.effect_invocations, frame.limits.max_effect_invocations, "effect budget");
}

void consume_choice_budget(ResolutionFrame& frame) {
    ++frame.budget.choices_requested;
    require_within(frame.budget.choices_requested, frame.limits.max_choices, "choice budget");
}

void observe_resolution_depth(ResolutionFrame& frame, std::size_t depth) {
    frame.budget.max_resolution_depth_seen = std::max(frame.budget.max_resolution_depth_seen, depth);
    require_within(depth, frame.limits.max_resolution_depth, "resolution depth");
}

DecisionToken decision_token_from_action(const Action& action) {
    DecisionToken token;
    token.kind = action.type;
    token.source_entity_id = action.source_entity_id;
    if (action.target.kind == ActionTargetKind::Card) {
        token.target_entity_id = action.target.entity_id;
    } else if (action.target.kind == ActionTargetKind::CardDefinition) {
        token.target_definition_id = action.target.definition_id;
    } else if (action.target.kind == ActionTargetKind::Row) {
        token.row = Location{
            action.target.side,
            action.target.zone,
            action.insert_position >= 0 ? static_cast<std::size_t>(action.insert_position) : 0
        };
    }
    return token;
}

}  // namespace gwent
