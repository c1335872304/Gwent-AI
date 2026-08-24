#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gwent/core/enums.hpp"
#include "gwent/core/ids.hpp"

namespace gwent {

enum class PassReason : std::uint8_t {
    None,
    Explicit,
    HandExhausted,
};

[[nodiscard]] constexpr std::string_view to_string(PassReason reason) noexcept {
    switch (reason) {
        case PassReason::None: return "NONE";
        case PassReason::Explicit: return "EXPLICIT";
        case PassReason::HandExhausted: return "HAND_EXHAUSTED";
    }
    return "UNKNOWN";
}

struct RowEffect {
    std::string id;
    std::unordered_map<std::string, std::string> data;
};

struct PlayerState {
    PlayerId player_id = kPlayerZero;

    std::vector<EntityId> deck;
    std::vector<EntityId> hand;
    std::vector<EntityId> stay;
    std::vector<EntityId> cemetery;
    std::vector<EntityId> banished;
    std::optional<EntityId> leader;

    // rows[0] = Melee, rows[1] = Ranged.
    std::array<std::vector<EntityId>, 2> rows{};
    std::array<std::vector<RowEffect>, 2> row_effects{};

    bool leader_used = false;
    bool passed = false;
    PassReason pass_reason = PassReason::None;
    int round_wins = 0;
    int coins = 0;

    // 由开局/回合准备阶段写入。当前规则核会把该额度暴露为顺序 Mulligan 决策；
    // KEEP_HAND 或额度耗尽后归零。保存在 state 中便于 C++ / Python trace 对齐。
    int mulligans_available = 0;
    int cards_drawn_this_round = 0;

    std::vector<std::string> starting_deck;
    std::unordered_map<std::string, std::string> rule_modifiers;

    explicit PlayerState(PlayerId id = kPlayerZero) : player_id(id) {}

    [[nodiscard]] std::vector<EntityId>& row(Zone zone);
    [[nodiscard]] const std::vector<EntityId>& row(Zone zone) const;
    [[nodiscard]] std::vector<RowEffect>& effects_on_row(Zone zone);
    [[nodiscard]] const std::vector<RowEffect>& effects_on_row(Zone zone) const;

    [[nodiscard]] std::size_t zone_size(Zone zone) const;
};

}  // namespace gwent
