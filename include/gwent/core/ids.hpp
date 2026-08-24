#pragma once

#include <cstdint>

namespace gwent {

using PlayerId = int;
using EntityId = std::int32_t;
using ListenerId = std::int32_t;
using CardDefId = std::uint32_t;

inline constexpr PlayerId kPlayerZero = 0;
inline constexpr PlayerId kPlayerOne = 1;
inline constexpr int kPlayerCount = 2;
inline constexpr EntityId kInvalidEntityId = -1;
inline constexpr CardDefId kInvalidCardDefId = static_cast<CardDefId>(-1);

constexpr bool is_valid_player_id(PlayerId player_id) noexcept {
    return player_id == kPlayerZero || player_id == kPlayerOne;
}

}  // namespace gwent
