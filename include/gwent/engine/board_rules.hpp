#pragma once

#include <cstddef>

#include "gwent/core/state.hpp"

namespace gwent {

// Modern two-row board default. Kept configurable through match_config so
// experiments/tests can override it without card-specific row-capacity logic.
inline constexpr std::size_t kDefaultRowCapacity = 9;

[[nodiscard]] std::size_t row_capacity(const GameState& state) noexcept;
[[nodiscard]] bool row_has_space(
    const GameState& state,
    PlayerId side,
    Zone row,
    std::size_t required_slots = 1
) noexcept;

// Dominance is controlled by a side when it has at least one unit on its
// battlefield and its strongest unit is tied with or stronger than the
// opponent's strongest unit.
[[nodiscard]] bool has_dominance(const GameState& state, PlayerId side) noexcept;

// Might is active when the side controls at least one 10+ power unit on each battle row.
[[nodiscard]] bool has_might(const GameState& state, PlayerId side) noexcept;

}  // namespace gwent
