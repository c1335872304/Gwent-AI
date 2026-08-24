#pragma once

#include <cstdint>
#include <string_view>

namespace gwent {

enum class ActionStatus : std::uint8_t {
    Applied,
    IllegalAction,
    InvalidPlayer,
    NotCurrentPlayer,
    InvalidPhase,
    InvalidSource,
    InvalidTarget,
    EmptyDeck,
    UnsupportedAction,
    TaskLimitExceeded,
    InvariantViolation,
};

[[nodiscard]] std::string_view to_string(ActionStatus status) noexcept;

}  // namespace gwent
