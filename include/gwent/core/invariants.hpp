#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "gwent/core/ids.hpp"
#include "gwent/core/location.hpp"
#include "gwent/core/performance.hpp"
#include "gwent/core/state.hpp"

namespace gwent {

enum class InvariantSeverity : std::uint8_t {
    Warning,
    Error,
};

struct InvariantIssue {
    InvariantSeverity severity = InvariantSeverity::Error;
    std::string code;
    std::string message;
    std::optional<EntityId> entity_id;
    std::optional<Location> location;
};

struct InvariantReport {
    std::vector<InvariantIssue> issues;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] std::size_t error_count() const noexcept;
    [[nodiscard]] std::size_t warning_count() const noexcept;
    [[nodiscard]] std::string summary() const;
};

struct InvariantOptions {
    bool require_locations_for_all_cards = true;
    bool require_cards_for_all_locations = true;
    bool require_zone_index_matches_vector_index = true;
    bool require_single_zone_membership = true;
    bool require_battle_entity_types = true;
    bool require_leader_zone_type = true;
    bool require_pending_choices_are_well_formed = true;
    bool require_listener_sources_are_valid = true;
    bool require_leaders_only_in_leader_zone = true;
    bool require_non_power_cards_are_powerless = true;
    bool require_row_effects_are_well_formed = true;
    bool require_turn_state_is_phase_consistent = true;
    bool require_engine_phase_consistent = true;
    bool require_turn_context_consistent = true;
    bool require_pass_reason_consistent = true;

    // Optional caller-owned metrics sink used by profiling/debug builds.
    PerformanceCounters* performance_counters = nullptr;
};

[[nodiscard]] InvariantReport validate_state_invariants(
    const GameState& state,
    const InvariantOptions& options = {}
);

[[nodiscard]] std::string invariant_report_to_string(const InvariantReport& report);

}  // namespace gwent
