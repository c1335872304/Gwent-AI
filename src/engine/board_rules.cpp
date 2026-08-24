#include "gwent/engine/board_rules.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace gwent {

std::size_t row_capacity(const GameState& state) noexcept {
    const auto it = state.match_config.find("row_capacity");
    if (it == state.match_config.end() || it->second.empty()) {
        return kDefaultRowCapacity;
    }

    std::size_t value = kDefaultRowCapacity;
    const std::string& text = it->second;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0) {
        return kDefaultRowCapacity;
    }
    return value;
}

bool row_has_space(
    const GameState& state,
    PlayerId side,
    Zone row,
    std::size_t required_slots
) noexcept {
    if (!is_valid_player_id(side) || !is_battle_zone(row)) {
        return false;
    }
    if (required_slots == 0) {
        return true;
    }
    const std::size_t current = state.player(side).row(row).size();
    const std::size_t capacity = row_capacity(state);
    return current <= capacity && required_slots <= capacity - current;
}

bool has_dominance(const GameState& state, PlayerId side) noexcept {
    if (!is_valid_player_id(side)) {
        return false;
    }

    const auto strongest_power = [&](PlayerId player_id) {
        int strongest = 0;
        for (Zone zone : kBattleZones) {
            for (EntityId entity_id : state.player(player_id).row(zone)) {
                const RuntimeCard* card = state.find_card(entity_id);
                if (card != nullptr && card->has_power()) {
                    strongest = std::max(strongest, card->state.power);
                }
            }
        }
        return strongest;
    };

    const int own = strongest_power(side);
    const int enemy = strongest_power(state.opponent_id(side));
    return own > 0 && own >= enemy;
}


bool has_might(const GameState& state, PlayerId side) noexcept {
    if (!is_valid_player_id(side)) {
        return false;
    }

    for (Zone zone : kBattleZones) {
        bool row_has_ten = false;
        for (EntityId entity_id : state.player(side).row(zone)) {
            const RuntimeCard* card = state.find_card(entity_id);
            if (card != nullptr && card->has_power() && card->state.power >= 10) {
                row_has_ten = true;
                break;
            }
        }
        if (!row_has_ten) {
            return false;
        }
    }
    return true;
}

}  // namespace gwent
