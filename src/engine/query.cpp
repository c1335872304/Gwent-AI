#include "gwent/engine/query.hpp"

#include <stdexcept>
#include <string>

namespace gwent {
namespace {

void require_player_id_for_query(PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        throw std::out_of_range("query player id must be 0 or 1");
    }
}

}  // namespace

std::vector<EntityId> query_zone_entities(
    const GameState& state,
    PlayerId side,
    Zone zone
) {
    require_player_id_for_query(side);
    const PlayerState& player = state.player(side);
    switch (zone) {
        case Zone::Deck:
            return player.deck;
        case Zone::Hand:
            return player.hand;
        case Zone::Stay:
            return player.stay;
        case Zone::Melee:
        case Zone::Ranged:
            return player.row(zone);
        case Zone::Cemetery:
            return player.cemetery;
        case Zone::Banished:
            return player.banished;
        case Zone::Leader:
            if (player.leader.has_value()) {
                return {*player.leader};
            }
            return {};
    }
    return {};
}

std::vector<EntityId> query_battlefield_entities(
    const GameState& state,
    PlayerId side
) {
    require_player_id_for_query(side);
    const PlayerState& player = state.player(side);
    std::vector<EntityId> entities;
    entities.reserve(player.row(Zone::Melee).size() + player.row(Zone::Ranged).size());
    entities.insert(entities.end(), player.row(Zone::Melee).begin(), player.row(Zone::Melee).end());
    entities.insert(entities.end(), player.row(Zone::Ranged).begin(), player.row(Zone::Ranged).end());
    return entities;
}

std::vector<EntityId> query_battlefield_entities(
    const EntityIndex& index,
    PlayerId side
) {
    require_player_id_for_query(side);
    return index.battlefield_entities[static_cast<std::size_t>(side)];
}

std::vector<EntityId> query_battlefield_units(
    const EntityIndex& index,
    PlayerId side
) {
    require_player_id_for_query(side);
    return index.battlefield_units[static_cast<std::size_t>(side)];
}

std::vector<EntityId> query_units_on_row(
    const GameState& state,
    PlayerId side,
    Zone row_zone,
    bool exclude_deploying
) {
    require_player_id_for_query(side);
    if (!is_battle_zone(row_zone)) {
        return {};
    }

    std::vector<EntityId> units;
    for (const EntityId entity_id : state.player(side).row(row_zone)) {
        const RuntimeCard* card = state.find_card(entity_id);
        if (card == nullptr || !card->has_power()) {
            continue;
        }
        if (exclude_deploying && card->state.deploying) {
            continue;
        }
        units.push_back(entity_id);
    }
    return units;
}

bool has_unit_on_row(
    const GameState& state,
    PlayerId side,
    Zone row_zone,
    bool exclude_deploying
) {
    require_player_id_for_query(side);
    if (!is_battle_zone(row_zone)) {
        return false;
    }
    for (const EntityId entity_id : state.player(side).row(row_zone)) {
        const RuntimeCard* card = state.find_card(entity_id);
        if (card == nullptr || !card->has_power()) {
            continue;
        }
        if (exclude_deploying && card->state.deploying) {
            continue;
        }
        return true;
    }
    return false;
}

bool has_battlefield_unit(
    const GameState& state,
    PlayerId side,
    bool exclude_deploying
) {
    return has_unit_on_row(state, side, Zone::Melee, exclude_deploying)
        || has_unit_on_row(state, side, Zone::Ranged, exclude_deploying);
}

}  // namespace gwent
