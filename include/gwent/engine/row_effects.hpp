#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "gwent/core/state.hpp"

namespace gwent {

[[nodiscard]] int row_effect_int(const RowEffect& effect, std::string_view key, int fallback = 0);
void row_effect_set_int(RowEffect& effect, std::string_view key, int value);

[[nodiscard]] int row_effect_duration(const GameState& state, PlayerId side, Zone zone, std::string_view effect_id);

RowEffect& add_or_extend_row_effect(
    GameState& state,
    PlayerId side,
    Zone zone,
    std::string_view effect_id,
    int duration,
    PlayerId trigger_player_id,
    const std::unordered_map<std::string, std::string>& data = {}
);

}  // namespace gwent
