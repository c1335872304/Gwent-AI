#include "gwent/core/state.hpp"

#include <algorithm>
#include <iterator>

namespace gwent {

std::vector<EntityId>& PlayerState::row(Zone zone) {
    if (!is_battle_zone(zone)) {
        throw std::invalid_argument("zone is not a battle row");
    }
    return rows[row_index(zone)];
}

const std::vector<EntityId>& PlayerState::row(Zone zone) const {
    if (!is_battle_zone(zone)) {
        throw std::invalid_argument("zone is not a battle row");
    }
    return rows[row_index(zone)];
}

std::vector<RowEffect>& PlayerState::effects_on_row(Zone zone) {
    if (!is_battle_zone(zone)) {
        throw std::invalid_argument("zone is not a battle row");
    }
    return row_effects[row_index(zone)];
}

const std::vector<RowEffect>& PlayerState::effects_on_row(Zone zone) const {
    if (!is_battle_zone(zone)) {
        throw std::invalid_argument("zone is not a battle row");
    }
    return row_effects[row_index(zone)];
}

std::size_t PlayerState::zone_size(Zone zone) const {
    switch (zone) {
        case Zone::Deck: return deck.size();
        case Zone::Hand: return hand.size();
        case Zone::Stay: return stay.size();
        case Zone::Melee: return row(Zone::Melee).size();
        case Zone::Ranged: return row(Zone::Ranged).size();
        case Zone::Cemetery: return cemetery.size();
        case Zone::Banished: return banished.size();
        case Zone::Leader: return leader.has_value() ? 1U : 0U;
    }
    return 0;
}

PlayerState& GameState::player(PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        throw std::out_of_range("player_id must be 0 or 1");
    }
    return players[static_cast<std::size_t>(player_id)];
}

const PlayerState& GameState::player(PlayerId player_id) const {
    if (!is_valid_player_id(player_id)) {
        throw std::out_of_range("player_id must be 0 or 1");
    }
    return players[static_cast<std::size_t>(player_id)];
}

PlayerId GameState::opponent_id(PlayerId player_id) const {
    if (!is_valid_player_id(player_id)) {
        throw std::out_of_range("player_id must be 0 or 1");
    }
    return player_id == kPlayerZero ? kPlayerOne : kPlayerZero;
}

RuntimeCard* GameState::find_card(EntityId entity_id) noexcept {
    if (entity_id < 0 || static_cast<std::size_t>(entity_id) >= cards.size()) {
        return nullptr;
    }
    return &cards[static_cast<std::size_t>(entity_id)];
}

const RuntimeCard* GameState::find_card(EntityId entity_id) const noexcept {
    if (entity_id < 0 || static_cast<std::size_t>(entity_id) >= cards.size()) {
        return nullptr;
    }
    return &cards[static_cast<std::size_t>(entity_id)];
}

std::optional<Location> GameState::location_of(EntityId entity_id) const {
    if (entity_id < 0 || static_cast<std::size_t>(entity_id) >= locations.size()) {
        return std::nullopt;
    }
    return locations[static_cast<std::size_t>(entity_id)];
}

EntityId GameState::add_card(const CardDefinition& definition, PlayerId owner_id, Location location) {
    if (!card_catalog) {
        card_catalog = std::make_shared<CardCatalog>();
    }
    const CardDefId definition_id = card_catalog->intern(definition);
    return add_card(definition_id, owner_id, location);
}

EntityId GameState::add_card(CardDefId definition_id, PlayerId owner_id, Location location) {
    if (!is_valid_player_id(owner_id)) {
        throw std::out_of_range("owner_id must be 0 or 1");
    }
    if (!is_valid_player_id(location.side)) {
        throw std::out_of_range("location.side must be 0 or 1");
    }
    if (!card_catalog) {
        throw std::logic_error("cannot add CardDefId without a CardCatalog");
    }
    const CardDefinition& stored_definition = card_catalog->at(definition_id);

    const EntityId entity_id = next_entity_id++;
    if (entity_id != static_cast<EntityId>(cards.size())) {
        throw std::logic_error("dense entity storage requires monotonic contiguous EntityId values");
    }
    cards.push_back(RuntimeCard::create(entity_id, definition_id, stored_definition, owner_id));
    locations.emplace_back(std::nullopt);
    insert_into_zone(entity_id, location);
    return entity_id;
}

void GameState::move_card(EntityId entity_id, Location destination) {
    if (!find_card(entity_id)) {
        throw std::out_of_range("cannot move missing card entity");
    }
    if (!is_valid_player_id(destination.side)) {
        throw std::out_of_range("destination.side must be 0 or 1");
    }

    remove_from_current_zone(entity_id);
    insert_into_zone(entity_id, destination);

    // Cards entering the cemetery lose temporary battlefield power changes.
    // Keep runtime base_power intact because some card effects legitimately
    // modify base power during the match; only current power is normalized.
    if (destination.zone == Zone::Cemetery) {
        RuntimeCard* card = find_card(entity_id);
        if (card != nullptr && card->has_power()) {
            card->state.power = card->state.base_power;
        }
    }
}

void GameState::normalize_zone_indices(PlayerId side, Zone zone_value) {
    if (!is_valid_player_id(side)) {
        throw std::out_of_range("side must be 0 or 1");
    }
    reindex(side, zone_value);
}

int GameState::row_score(PlayerId side, Zone row_zone) const {
    if (!is_battle_zone(row_zone)) {
        throw std::invalid_argument("row_score requires Melee or Ranged");
    }

    int score = 0;
    for (const EntityId entity_id : player(side).row(row_zone)) {
        const RuntimeCard* card = find_card(entity_id);
        if (card != nullptr && card->has_power()) {
            score += card->state.power;
        }
    }
    return score;
}

int GameState::board_score(PlayerId side) const {
    return row_score(side, Zone::Melee) + row_score(side, Zone::Ranged);
}

std::vector<EntityId>& GameState::mutable_zone(PlayerId side, Zone zone) {
    PlayerState& p = player(side);
    switch (zone) {
        case Zone::Deck: return p.deck;
        case Zone::Hand: return p.hand;
        case Zone::Stay: return p.stay;
        case Zone::Melee: return p.row(Zone::Melee);
        case Zone::Ranged: return p.row(Zone::Ranged);
        case Zone::Cemetery: return p.cemetery;
        case Zone::Banished: return p.banished;
        case Zone::Leader:
            throw std::invalid_argument("Leader zone is not vector-backed");
    }
    throw std::invalid_argument("unknown zone");
}

const std::vector<EntityId>& GameState::zone(PlayerId side, Zone zone) const {
    const PlayerState& p = player(side);
    switch (zone) {
        case Zone::Deck: return p.deck;
        case Zone::Hand: return p.hand;
        case Zone::Stay: return p.stay;
        case Zone::Melee: return p.row(Zone::Melee);
        case Zone::Ranged: return p.row(Zone::Ranged);
        case Zone::Cemetery: return p.cemetery;
        case Zone::Banished: return p.banished;
        case Zone::Leader:
            throw std::invalid_argument("Leader zone is not vector-backed");
    }
    throw std::invalid_argument("unknown zone");
}

void GameState::remove_from_current_zone(EntityId entity_id) {
    if (entity_id < 0 || static_cast<std::size_t>(entity_id) >= locations.size()) {
        return;
    }
    auto& current = locations[static_cast<std::size_t>(entity_id)];
    if (!current.has_value()) {
        return;
    }

    const Location old = *current;
    if (old.zone == Zone::Leader) {
        auto& leader = player(old.side).leader;
        if (leader == entity_id) {
            leader.reset();
        }
    } else {
        auto& items = mutable_zone(old.side, old.zone);
        const auto it = std::find(items.begin(), items.end(), entity_id);
        if (it != items.end()) {
            items.erase(it);
        }
    }
    current.reset();
    reindex(old.side, old.zone);
}

void GameState::insert_into_zone(EntityId entity_id, Location destination) {
    if (destination.zone == Zone::Leader) {
        auto& leader = player(destination.side).leader;
        if (leader.has_value() && leader.value() != entity_id) {
            throw std::logic_error("leader zone already contains a different entity");
        }
        leader = entity_id;
        destination.index = 0;
    } else {
        auto& items = mutable_zone(destination.side, destination.zone);
        const auto clamped_index = std::min(destination.index, items.size());
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(clamped_index), entity_id);
        destination.index = clamped_index;
    }

    if (entity_id < 0 || static_cast<std::size_t>(entity_id) >= locations.size()) {
        throw std::out_of_range("cannot place missing card entity");
    }
    locations[static_cast<std::size_t>(entity_id)] = destination;
    reindex(destination.side, destination.zone);
}

void GameState::reindex(PlayerId side, Zone zone_value) {
    if (zone_value == Zone::Leader) {
        if (player(side).leader.has_value()) {
            locations[static_cast<std::size_t>(player(side).leader.value())] = Location{side, Zone::Leader, 0};
        }
        return;
    }

    const auto& items = zone(side, zone_value);
    for (std::size_t i = 0; i < items.size(); ++i) {
        locations[static_cast<std::size_t>(items[i])] = Location{side, zone_value, i};
    }
}

}  // namespace gwent
