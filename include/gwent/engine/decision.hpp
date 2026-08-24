#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "gwent/core/ids.hpp"
#include "gwent/engine/action.hpp"

namespace gwent {

enum class DecisionType : std::uint8_t {
    None,
    TurnAction,
    Mulligan,
    ChooseTarget,
};

struct Decision {
    DecisionType type = DecisionType::None;
    PlayerId player_id = kPlayerZero;
    std::vector<Action> legal_actions;

    [[nodiscard]] bool has_actions() const noexcept { return !legal_actions.empty(); }
};

std::string_view to_string(DecisionType type) noexcept;

}  // namespace gwent
