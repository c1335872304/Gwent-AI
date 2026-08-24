#include "gwent/engine/action_status.hpp"

namespace gwent {

std::string_view to_string(ActionStatus status) noexcept {
    switch (status) {
        case ActionStatus::Applied: return "APPLIED";
        case ActionStatus::IllegalAction: return "ILLEGAL_ACTION";
        case ActionStatus::InvalidPlayer: return "INVALID_PLAYER";
        case ActionStatus::NotCurrentPlayer: return "NOT_CURRENT_PLAYER";
        case ActionStatus::InvalidPhase: return "INVALID_PHASE";
        case ActionStatus::InvalidSource: return "INVALID_SOURCE";
        case ActionStatus::InvalidTarget: return "INVALID_TARGET";
        case ActionStatus::EmptyDeck: return "EMPTY_DECK";
        case ActionStatus::UnsupportedAction: return "UNSUPPORTED_ACTION";
        case ActionStatus::TaskLimitExceeded: return "TASK_LIMIT_EXCEEDED";
        case ActionStatus::InvariantViolation: return "INVARIANT_VIOLATION";
    }
    return "UNKNOWN";
}

}  // namespace gwent
