#include "gwent/engine/decision.hpp"

namespace gwent {

std::string_view to_string(DecisionType type) noexcept {
    switch (type) {
        case DecisionType::None: return "NONE";
        case DecisionType::TurnAction: return "TURN_ACTION";
        case DecisionType::Mulligan: return "MULLIGAN";
        case DecisionType::ChooseTarget: return "CHOOSE_TARGET";
    }
    return "UNKNOWN";
}

}  // namespace gwent
