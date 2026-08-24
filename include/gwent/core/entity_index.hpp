#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gwent/core/ids.hpp"
#include "gwent/core/location.hpp"
#include "gwent/core/performance.hpp"
#include "gwent/core/state.hpp"

namespace gwent {

// Lightweight read-only index built from GameState zone vectors.
//
// The index is intentionally not stored inside GameState yet: it is a safe
// query/perf stepping stone that keeps runtime state simple while giving
// invariants, legal actions, target selection and debug tools one canonical
// way to enumerate entities.
struct EntityIndex {
    std::vector<std::pair<EntityId, Location>> entries;
    std::unordered_map<EntityId, std::vector<Location>> memberships;
    std::unordered_map<EntityId, Location> unique_locations;
    std::unordered_map<std::string, std::vector<EntityId>> by_definition_id;

    std::array<std::vector<EntityId>, kPlayerCount> battlefield_entities{};
    std::array<std::vector<EntityId>, kPlayerCount> battlefield_units{};

    [[nodiscard]] std::optional<Location> unique_location_of(EntityId entity_id) const;
    [[nodiscard]] bool has_unique_location(EntityId entity_id) const;
};

[[nodiscard]] EntityIndex build_entity_index(const GameState& state, PerformanceCounters* performance_counters = nullptr);

}  // namespace gwent
