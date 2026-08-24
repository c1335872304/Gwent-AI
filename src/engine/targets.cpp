#include "gwent/engine/targets.hpp"

#include "gwent/engine/query.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace gwent {
namespace {

bool metadata_true(const RuntimeCard& card, const std::string& key) {
    const auto it = card.definition->metadata.find(key);
    if (it == card.definition->metadata.end()) {
        return false;
    }
    return it->second == "true" || it->second == "1" || it->second == "yes" || it->second == "on";
}

std::vector<PlayerId> candidate_sides(
    const GameState& state,
    const TargetContext& context,
    RelativeSide relative_side
) {
    if (relative_side == RelativeSide::Any) {
        return {kPlayerZero, kPlayerOne};
    }

    const auto resolved = resolve_relative_side(state, context, relative_side);
    if (!resolved.has_value()) {
        return {};
    }
    return {resolved.value()};
}

std::vector<Zone> zones_for_scope(ZoneScope scope) {
    switch (scope) {
        case ZoneScope::Any:
            return {Zone::Deck, Zone::Hand, Zone::Stay, Zone::Melee, Zone::Ranged, Zone::Cemetery, Zone::Banished, Zone::Leader};
        case ZoneScope::BattleRows:
            return {Zone::Melee, Zone::Ranged};
        case ZoneScope::Melee:
            return {Zone::Melee};
        case ZoneScope::Ranged:
            return {Zone::Ranged};
        case ZoneScope::Hand:
            return {Zone::Hand};
        case ZoneScope::Deck:
            return {Zone::Deck};
        case ZoneScope::Stay:
            return {Zone::Stay};
        case ZoneScope::Cemetery:
            return {Zone::Cemetery};
        case ZoneScope::Banished:
            return {Zone::Banished};
        case ZoneScope::Leader:
            return {Zone::Leader};
    }
    return {};
}

bool kind_matches(const RuntimeCard& card, TargetCardKind kind) {
    switch (kind) {
        case TargetCardKind::Any:
            return true;
        case TargetCardKind::Unit:
            return card.is_unit_card();
        case TargetCardKind::HasPower:
            return card.has_power();
        case TargetCardKind::NonUnit:
            return !card.is_unit_card();
        case TargetCardKind::Stratagem:
            return card.definition->card_type == CardType::Stratagem;
        case TargetCardKind::Leader:
            return card.definition->card_type == CardType::Leader;
        case TargetCardKind::OrderSource:
            return is_order_source_card(card);
    }
    return false;
}

int ordering_power(const GameState& state, EntityId entity_id) {
    const RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr || !card->has_power()) {
        return 0;
    }
    return card->state.power;
}

}  // namespace

bool is_order_source_card(const RuntimeCard& card) {
    if (card.runtime.order_used || card.runtime.order_pending_consume) {
        return false;
    }
    if (card.state.cooldown > 0 || card.state.timer > 0) {
        return false;
    }
    if (card.definition->card_type == CardType::Stratagem) {
        return true;
    }
    if (card.state.order_charges > 0) {
        return true;
    }
    if (card.definition->has_effect("order")) {
        return true;
    }
    for (const std::string& effect_id : card.definition->effect_ids) {
        if (effect_id.rfind("order:", 0) == 0) {
            return true;
        }
    }
    return metadata_true(card, "order");
}

std::optional<PlayerId> resolve_relative_side(
    const GameState& state,
    const TargetContext& context,
    RelativeSide side
) {
    switch (side) {
        case RelativeSide::Any:
            return std::nullopt;
        case RelativeSide::Actor:
            return is_valid_player_id(context.actor_id) ? std::optional<PlayerId>{context.actor_id} : std::nullopt;
        case RelativeSide::Opponent:
            return is_valid_player_id(context.actor_id) ? std::optional<PlayerId>{state.opponent_id(context.actor_id)} : std::nullopt;
        case RelativeSide::SourceOwner: {
            const RuntimeCard* source = state.find_card(context.source_entity_id);
            return source == nullptr ? std::nullopt : std::optional<PlayerId>{source->owner_id};
        }
        case RelativeSide::SourceController: {
            const RuntimeCard* source = state.find_card(context.source_entity_id);
            return source == nullptr ? std::nullopt : std::optional<PlayerId>{source->controller_id};
        }
        case RelativeSide::TargetSide:
            if (context.action_target.kind == ActionTargetKind::Row && is_valid_player_id(context.action_target.side)) {
                return context.action_target.side;
            }
            if (context.action_target.kind == ActionTargetKind::Card) {
                const auto location = state.location_of(context.action_target.entity_id);
                if (location.has_value()) {
                    return location->side;
                }
            }
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Location> resolve_row_destination(
    const GameState& state,
    const TargetContext& context,
    const RowDestinationSpec& spec
) {
    if (!is_battle_zone(spec.row)) {
        return std::nullopt;
    }
    const auto side = resolve_relative_side(state, context, spec.side);
    if (!side.has_value()) {
        return std::nullopt;
    }
    const std::size_t index = spec.append ? state.player(side.value()).row(spec.row).size() : spec.index;
    return Location{side.value(), spec.row, index};
}

bool is_targetable(
    const GameState& state,
    const TargetContext& context,
    const RuntimeCard& card,
    const TargetSelector& selector,
    TargetingChannel channel
) {
    (void)context;

    TargetabilityPolicy policy;
    policy.allow_leader = selector.zone_scope == ZoneScope::Leader
        || selector.card_kind == TargetCardKind::Leader
        || channel == TargetingChannel::ExplicitLeaderTarget
        || channel == TargetingChannel::SystemEffect;
    policy.respect_immune = selector.require_not_immune;
    policy.respect_veil = selector.require_not_veiled;
    policy.require_board_presence = selector.zone_scope == ZoneScope::BattleRows
        || selector.zone_scope == ZoneScope::Melee
        || selector.zone_scope == ZoneScope::Ranged;

    if (card.definition->card_type == CardType::Leader && !policy.allow_leader) {
        return false;
    }
    if (policy.require_board_presence) {
        const auto location = state.location_of(card.entity_id);
        if (!location.has_value() || !is_battle_zone(location->zone)) {
            return false;
        }
    }
    if (policy.respect_immune && card.state.immune) {
        return false;
    }
    if (selector.require_not_locked && card.state.locked) {
        return false;
    }
    if (policy.respect_veil && card.state.veil) {
        return false;
    }
    return true;
}

bool target_matches_selector(
    const GameState& state,
    const TargetContext& context,
    EntityId entity_id,
    const TargetSelector& selector
) {
    const RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr) {
        return false;
    }
    if (!is_targetable(state, context, *card, selector, context.targeting_channel)) {
        return false;
    }
    if (selector.exclude_source && entity_id == context.source_entity_id) {
        return false;
    }

    const auto location = state.location_of(entity_id);
    if (!location.has_value()) {
        return false;
    }

    const auto sides = candidate_sides(state, context, selector.side);
    if (selector.side != RelativeSide::Any && sides.empty()) {
        return false;
    }
    if (selector.side != RelativeSide::Any && std::find(sides.begin(), sides.end(), location->side) == sides.end()) {
        return false;
    }

    const auto zones = zones_for_scope(selector.zone_scope);
    if (std::find(zones.begin(), zones.end(), location->zone) == zones.end()) {
        return false;
    }

    if (!kind_matches(*card, selector.card_kind)) {
        return false;
    }
    if (selector.require_alive) {
        if (!card->has_power() || card->state.is_dead()) {
            return false;
        }
    }
    if (selector.require_bleeding && card->state.bleeding <= 0) {
        return false;
    }
    return true;
}

std::vector<EntityId> select_card_targets(
    const GameState& state,
    const TargetContext& context,
    const TargetSelector& selector
) {
    PerformanceScope scope(context.performance_counters, PerformanceMetric::TargetSelector);
    std::vector<EntityId> candidates;
    const auto sides = selector.side == RelativeSide::Any
        ? std::vector<PlayerId>{kPlayerZero, kPlayerOne}
        : candidate_sides(state, context, selector.side);
    const auto zones = zones_for_scope(selector.zone_scope);

    for (const PlayerId side : sides) {
        for (const Zone zone : zones) {
            auto zone_entities = query_zone_entities(state, side, zone);
            candidates.insert(candidates.end(), zone_entities.begin(), zone_entities.end());
        }
    }

    record_target_candidates_scanned(context.performance_counters, candidates.size());

    std::vector<EntityId> selected;
    for (const EntityId entity_id : candidates) {
        if (target_matches_selector(state, context, entity_id, selector)) {
            selected.push_back(entity_id);
        }
    }

    if (selector.ordering == TargetOrdering::WeakestFirst) {
        std::stable_sort(selected.begin(), selected.end(), [&](EntityId lhs, EntityId rhs) {
            const int lp = ordering_power(state, lhs);
            const int rp = ordering_power(state, rhs);
            if (lp != rp) {
                return lp < rp;
            }
            return lhs < rhs;
        });
    } else if (selector.ordering == TargetOrdering::StrongestFirst) {
        std::stable_sort(selected.begin(), selected.end(), [&](EntityId lhs, EntityId rhs) {
            const int lp = ordering_power(state, lhs);
            const int rp = ordering_power(state, rhs);
            if (lp != rp) {
                return lp > rp;
            }
            return lhs < rhs;
        });
    }

    if (selector.max_targets > 0 && static_cast<int>(selected.size()) > selector.max_targets) {
        selected.resize(static_cast<std::size_t>(selector.max_targets));
    }
    return selected;
}

bool action_card_target_is_valid(
    const GameState& state,
    const TargetContext& context,
    const TargetSelector& selector
) {
    if (context.action_target.kind != ActionTargetKind::Card) {
        return false;
    }
    return target_matches_selector(state, context, context.action_target.entity_id, selector);
}

std::optional<TargetSelector> target_selector_from_metadata_value(std::string_view value) {
    TargetSelector selector;
    selector.max_targets = 1;

    if (value == "enemy_unit") {
        selector.side = RelativeSide::Opponent;
        selector.zone_scope = ZoneScope::BattleRows;
        selector.card_kind = TargetCardKind::HasPower;
        selector.require_not_immune = true;
        return selector;
    }
    if (value == "allied_unit") {
        selector.side = RelativeSide::Actor;
        selector.zone_scope = ZoneScope::BattleRows;
        selector.card_kind = TargetCardKind::HasPower;
        return selector;
    }
    if (value == "bleeding_enemy_unit") {
        selector.side = RelativeSide::Opponent;
        selector.zone_scope = ZoneScope::BattleRows;
        selector.card_kind = TargetCardKind::HasPower;
        selector.require_not_immune = true;
        selector.require_bleeding = true;
        return selector;
    }
    if (value == "own_hand_unit" || value == "allied_hand_unit") {
        selector.side = RelativeSide::Actor;
        selector.zone_scope = ZoneScope::Hand;
        selector.card_kind = TargetCardKind::Unit;
        return selector;
    }
    if (value == "own_hand_card" || value == "allied_hand_card") {
        selector.side = RelativeSide::Actor;
        selector.zone_scope = ZoneScope::Hand;
        selector.card_kind = TargetCardKind::Any;
        return selector;
    }
    if (value == "battlefield_unit") {
        selector.side = RelativeSide::Any;
        selector.zone_scope = ZoneScope::BattleRows;
        selector.card_kind = TargetCardKind::HasPower;
        selector.require_not_immune = true;
        return selector;
    }
    if (value == "any_battlefield_entity") {
        selector.side = RelativeSide::Any;
        selector.zone_scope = ZoneScope::BattleRows;
        selector.card_kind = TargetCardKind::Any;
        return selector;
    }

    return std::nullopt;
}

std::optional<TargetSelector> metadata_card_target_selector(
    const RuntimeCard& card,
    std::string_view metadata_key
) {
    const auto it = card.definition->metadata.find(std::string(metadata_key));
    if (it == card.definition->metadata.end() || it->second.empty()) {
        return std::nullopt;
    }
    return target_selector_from_metadata_value(it->second);
}

std::vector<ActionTarget> select_action_card_targets(
    const GameState& state,
    const TargetContext& context,
    const TargetSelector& selector
) {
    TargetSelector all_targets = selector;
    all_targets.max_targets = 0;

    const std::vector<EntityId> ids = select_card_targets(state, context, all_targets);
    std::vector<ActionTarget> targets;
    targets.reserve(ids.size());
    for (EntityId entity_id : ids) {
        targets.push_back(ActionTarget::card(entity_id));
    }
    return targets;
}

bool action_target_matches_card_selector(
    const GameState& state,
    const TargetContext& context,
    const TargetSelector& selector
) {
    if (context.action_target.kind != ActionTargetKind::Card) {
        return false;
    }
    return action_card_target_is_valid(state, context, selector);
}

}  // namespace gwent
