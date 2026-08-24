#pragma once

#include <vector>

#include "gwent/core/entity_index.hpp"
#include "gwent/core/state.hpp"

namespace gwent {

// Canonical read-only entity queries. These helpers intentionally return ids
// in board/vector order so LegalActions, target selectors, invariants and debug
// tooling do not each reinvent zone traversal semantics.
[[nodiscard]] std::vector<EntityId> query_zone_entities(
    const GameState& state,
    PlayerId side,
    Zone zone
);

[[nodiscard]] std::vector<EntityId> query_battlefield_entities(
    const GameState& state,
    PlayerId side
);

[[nodiscard]] std::vector<EntityId> query_battlefield_entities(
    const EntityIndex& index,
    PlayerId side
);

[[nodiscard]] std::vector<EntityId> query_battlefield_units(
    const EntityIndex& index,
    PlayerId side
);

[[nodiscard]] std::vector<EntityId> query_units_on_row(
    const GameState& state,
    PlayerId side,
    Zone row_zone,
    bool exclude_deploying = false
);

[[nodiscard]] bool has_unit_on_row(
    const GameState& state,
    PlayerId side,
    Zone row_zone,
    bool exclude_deploying = false
);

[[nodiscard]] bool has_battlefield_unit(
    const GameState& state,
    PlayerId side,
    bool exclude_deploying = false
);

}  // namespace gwent
