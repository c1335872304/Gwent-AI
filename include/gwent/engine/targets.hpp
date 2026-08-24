#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "gwent/core/location.hpp"
#include "gwent/core/performance.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/action.hpp"

namespace gwent {

enum class RelativeSide : std::uint8_t {
    Any,
    Actor,
    Opponent,
    SourceOwner,
    SourceController,
    TargetSide,
};

enum class ZoneScope : std::uint8_t {
    Any,
    BattleRows,
    Melee,
    Ranged,
    Hand,
    Deck,
    Stay,
    Cemetery,
    Banished,
    Leader,
};

enum class TargetCardKind : std::uint8_t {
    Any,
    Unit,
    HasPower,
    NonUnit,
    Stratagem,
    Leader,
    OrderSource,
};

enum class TargetingChannel : std::uint8_t {
    NormalCardEffect,
    Order,
    LeaderAbility,
    SystemEffect,
    ExplicitLeaderTarget,
};

struct TargetabilityPolicy {
    bool allow_leader = false;
    bool respect_immune = true;
    bool respect_defender = true;
    bool respect_veil = false;
    bool require_board_presence = true;
};

enum class TargetOrdering : std::uint8_t {
    BoardOrder,
    WeakestFirst,
    StrongestFirst,
};

struct TargetContext {
    PlayerId actor_id = kPlayerZero;
    EntityId source_entity_id = kInvalidEntityId;
    ActionTarget action_target = ActionTarget::none();
    PerformanceCounters* performance_counters = nullptr;
    TargetingChannel targeting_channel = TargetingChannel::NormalCardEffect;
};

struct TargetSelector {
    RelativeSide side = RelativeSide::Any;
    ZoneScope zone_scope = ZoneScope::BattleRows;
    TargetCardKind card_kind = TargetCardKind::Any;
    bool exclude_source = false;
    bool require_not_immune = false;
    bool require_not_locked = false;
    bool require_not_veiled = false;
    bool require_alive = false;
    bool require_bleeding = false;
    int max_targets = 0;  // 0 means all matching targets.
    TargetOrdering ordering = TargetOrdering::BoardOrder;
};

struct RowDestinationSpec {
    RelativeSide side = RelativeSide::Actor;
    Zone row = Zone::Melee;
    bool append = true;
    std::size_t index = 0;
};

[[nodiscard]] bool is_order_source_card(const RuntimeCard& card);

[[nodiscard]] std::optional<PlayerId> resolve_relative_side(
    const GameState& state,
    const TargetContext& context,
    RelativeSide side
);

[[nodiscard]] std::optional<Location> resolve_row_destination(
    const GameState& state,
    const TargetContext& context,
    const RowDestinationSpec& spec
);


[[nodiscard]] bool is_targetable(
    const GameState& state,
    const TargetContext& context,
    const RuntimeCard& card,
    const TargetSelector& selector,
    TargetingChannel channel
);

[[nodiscard]] bool target_matches_selector(
    const GameState& state,
    const TargetContext& context,
    EntityId entity_id,
    const TargetSelector& selector
);

[[nodiscard]] std::vector<EntityId> select_card_targets(
    const GameState& state,
    const TargetContext& context,
    const TargetSelector& selector
);

[[nodiscard]] bool action_card_target_is_valid(
    const GameState& state,
    const TargetContext& context,
    const TargetSelector& selector
);

// Canonical helpers for player-facing card target actions. Metadata selectors
// let card definitions declare common target requirements once and then share
// them across LegalActions, Kernel validation and pending-choice generation.
[[nodiscard]] std::optional<TargetSelector> target_selector_from_metadata_value(std::string_view value);

[[nodiscard]] std::optional<TargetSelector> metadata_card_target_selector(
    const RuntimeCard& card,
    std::string_view metadata_key
);

[[nodiscard]] std::vector<ActionTarget> select_action_card_targets(
    const GameState& state,
    const TargetContext& context,
    const TargetSelector& selector
);

[[nodiscard]] bool action_target_matches_card_selector(
    const GameState& state,
    const TargetContext& context,
    const TargetSelector& selector
);

}  // namespace gwent
