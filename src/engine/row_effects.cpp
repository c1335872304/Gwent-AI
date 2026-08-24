#include "gwent/engine/row_effects.hpp"

#include <algorithm>
#include <charconv>
#include <string>

namespace gwent {

int row_effect_int(const RowEffect& effect, std::string_view key, int fallback) {
    const auto it = effect.data.find(std::string(key));
    if (it == effect.data.end()) {
        return fallback;
    }
    int value = fallback;
    const auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
    if (ec != std::errc{} || ptr != it->second.data() + it->second.size()) {
        return fallback;
    }
    return value;
}

void row_effect_set_int(RowEffect& effect, std::string_view key, int value) {
    effect.data[std::string(key)] = std::to_string(value);
}

int row_effect_duration(const GameState& state, PlayerId side, Zone zone, std::string_view effect_id) {
    int total = 0;
    for (const RowEffect& effect : state.player(side).effects_on_row(zone)) {
        if (effect.id == effect_id) {
            total += std::max(0, row_effect_int(effect, "duration"));
        }
    }
    return total;
}

RowEffect& add_or_extend_row_effect(
    GameState& state,
    PlayerId side,
    Zone zone,
    std::string_view effect_id,
    int duration,
    PlayerId trigger_player_id,
    const std::unordered_map<std::string, std::string>& data
) {
    auto& effects = state.player(side).effects_on_row(zone);
    // A battle row has one active weather slot. Reapplying the same weather
    // extends it; applying a different weather replaces the old one.
    effects.erase(std::remove_if(effects.begin(), effects.end(), [&](const RowEffect& effect) {
        return effect.id != effect_id;
    }), effects.end());
    const auto existing = std::find_if(effects.begin(), effects.end(), [&](const RowEffect& effect) {
        return effect.id == effect_id;
    });
    if (existing != effects.end()) {
        row_effect_set_int(*existing, "duration", std::max(0, row_effect_int(*existing, "duration")) + std::max(0, duration));
        row_effect_set_int(*existing, "trigger_player_id", trigger_player_id);
        for (const auto& [key, value] : data) {
            existing->data[key] = value;
        }
        return *existing;
    }

    RowEffect effect;
    effect.id = std::string(effect_id);
    effect.data = data;
    effect.data["serial"] = std::to_string(++state.next_row_effect_serial);
    row_effect_set_int(effect, "duration", std::max(0, duration));
    row_effect_set_int(effect, "trigger_player_id", trigger_player_id);
    effects.push_back(std::move(effect));
    return effects.back();
}

}  // namespace gwent
