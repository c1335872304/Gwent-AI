#include "gwent/core/entity_index.hpp"

namespace gwent {
namespace {

void add_zone_entries(
    EntityIndex& index,
    PlayerId side,
    Zone zone,
    const std::vector<EntityId>& zone_cards
) {
    for (std::size_t i = 0; i < zone_cards.size(); ++i) {
        const EntityId entity_id = zone_cards[i];
        const Location location{side, zone, i};
        index.entries.push_back({entity_id, location});
        index.memberships[entity_id].push_back(location);
    }
}

}  // namespace

std::optional<Location> EntityIndex::unique_location_of(EntityId entity_id) const {
    const auto it = unique_locations.find(entity_id);
    if (it == unique_locations.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool EntityIndex::has_unique_location(EntityId entity_id) const {
    return unique_locations.find(entity_id) != unique_locations.end();
}

EntityIndex build_entity_index(const GameState& state, PerformanceCounters* performance_counters) {
    PerformanceScope scope(performance_counters, PerformanceMetric::EntityIndexBuild);
    EntityIndex index;

    for (PlayerId pid = 0; pid < kPlayerCount; ++pid) {
        const auto& player = state.players[static_cast<std::size_t>(pid)];
        add_zone_entries(index, pid, Zone::Deck, player.deck);
        add_zone_entries(index, pid, Zone::Hand, player.hand);
        add_zone_entries(index, pid, Zone::Stay, player.stay);
        add_zone_entries(index, pid, Zone::Melee, player.rows[0]);
        add_zone_entries(index, pid, Zone::Ranged, player.rows[1]);
        add_zone_entries(index, pid, Zone::Cemetery, player.cemetery);
        add_zone_entries(index, pid, Zone::Banished, player.banished);
        if (player.leader.has_value()) {
            const EntityId leader_id = *player.leader;
            const Location leader_location{pid, Zone::Leader, 0};
            index.entries.push_back({leader_id, leader_location});
            index.memberships[leader_id].push_back(leader_location);
        }
    }

    for (const auto& [entity_id, locations] : index.memberships) {
        if (locations.size() == 1) {
            index.unique_locations[entity_id] = locations.front();
        }
    }

    for (std::size_t i = 0; i < state.cards.size(); ++i) {
        const RuntimeCard& card = state.cards[i];
        index.by_definition_id[card.definition->id].push_back(static_cast<EntityId>(i));
    }

    for (const auto& [entity_id, location] : index.entries) {
        if (!is_battle_zone(location.zone) || !is_valid_player_id(location.side)) {
            continue;
        }
        const auto membership_it = index.memberships.find(entity_id);
        if (membership_it == index.memberships.end() || membership_it->second.size() != 1) {
            continue;
        }

        auto& battle_entities = index.battlefield_entities[static_cast<std::size_t>(location.side)];
        battle_entities.push_back(entity_id);

        const RuntimeCard* card = state.find_card(entity_id);
        if (card != nullptr && card->is_unit_card()) {
            auto& battle_units = index.battlefield_units[static_cast<std::size_t>(location.side)];
            battle_units.push_back(entity_id);
        }
    }

    return index;
}

}  // namespace gwent
