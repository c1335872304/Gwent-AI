#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "gwent/engine/action.hpp"
#include "gwent/engine/task.hpp"

namespace gwent {

struct GameState;

struct DecisionToken {
    ActionType kind = ActionType::Pass;
    EntityId source_entity_id = kInvalidEntityId;
    EntityId target_entity_id = kInvalidEntityId;
    CardDefId target_definition_id = kInvalidCardDefId;
    std::optional<Location> row;
};

struct ResolutionBudget {
    std::size_t tasks_processed = 0;
    std::size_t trigger_invocations = 0;
    std::size_t effect_invocations = 0;
    std::size_t choices_requested = 0;
    std::size_t max_resolution_depth_seen = 0;
};

struct ResolutionLimits {
    std::size_t max_tasks = 4096;
    std::size_t max_trigger_invocations = 4096;
    std::size_t max_effect_invocations = 4096;
    std::size_t max_choices = 32;
    std::size_t max_resolution_depth = 64;
};

struct TriggerDispatchFrame {
    TaskType task_type = TaskType::Pass;
    EventKind event_kind = EventKind::Unknown;
    std::string event_name;
    PlayerId actor_id = kPlayerZero;
    EntityId event_source_id = kInvalidEntityId;
    EntityId event_target_id = kInvalidEntityId;
    int amount = 0;
    std::string reason;

    std::vector<ListenerId> listener_ids;
    std::size_t next_listener_index = 0;

    std::vector<EntityId> definition_trigger_sources;
    std::size_t next_definition_index = 0;
    std::size_t next_definition_effect_index = 0;
};

struct ResolutionFrame {
    Action root_action = Action::pass(kPlayerZero);
    std::vector<TriggerDispatchFrame> trigger_stack;
    std::vector<DecisionToken> decision_prefix;
    ResolutionBudget budget;
    ResolutionLimits limits;

    // Snapshot from immediately before the root player action. It is shared by
    // every staged choice in the same resolution chain so any later failure can
    // roll back the whole root action rather than only the last click.
    std::shared_ptr<GameState> root_state_before;
};

class ResolutionBudgetExceeded final : public std::runtime_error {
public:
    explicit ResolutionBudgetExceeded(std::string message)
        : std::runtime_error(std::move(message)) {}
};

void consume_task_budget(ResolutionFrame& frame);
void consume_trigger_budget(ResolutionFrame& frame);
void consume_effect_budget(ResolutionFrame& frame);
void consume_choice_budget(ResolutionFrame& frame);
void observe_resolution_depth(ResolutionFrame& frame, std::size_t depth);

DecisionToken decision_token_from_action(const Action& action);

}  // namespace gwent
