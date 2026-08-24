#include "gwent/engine/primitives.hpp"
#include "gwent/engine/board_rules.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace gwent {

namespace {

void remove_board_bound_listeners_for_source(GameState& state, EntityId source_entity_id, Location destination) {
    if (is_battle_zone(destination.zone) || destination.zone == Zone::Leader) {
        return;
    }

    for (auto it = state.listeners.begin(); it != state.listeners.end();) {
        const RuntimeListener& listener = it->second;
        if (listener.source_id.has_value()
            && listener.source_id.value() == source_entity_id
            && listener.source_must_be_on_board) {
            it = state.listeners.erase(it);
        } else {
            ++it;
        }
    }
}

bool boost_blocked_by_active_effect(const GameState& state, EntityId target_entity_id) {
    const RuntimeCard* target = state.find_card(target_entity_id);
    if (target == nullptr || target->state.bleeding <= 0) {
        return false;
    }
    constexpr std::string_view prefix = "boost_blocked_by:";
    for (const auto& [key, value] : target->memory) {
        if (value != "true" || key.rfind(std::string(prefix), 0) != 0) {
            continue;
        }
        const std::string raw_id = key.substr(prefix.size());
        try {
            const EntityId blocker_id = std::stoi(raw_id);
            const RuntimeCard* blocker = state.find_card(blocker_id);
            const auto blocker_location = state.location_of(blocker_id);
            if (blocker != nullptr
                && blocker_location.has_value()
                && is_battle_zone(blocker_location->zone)
                && !blocker->state.locked
                && blocker->controller_id != target->controller_id) {
                return true;
            }
        } catch (...) {
            continue;
        }
    }
    return false;
}

}  // namespace

bool primitive_move_card(GameState& state, EntityId entity_id, Location destination) {
    const auto current = state.location_of(entity_id);
    if (!current.has_value()) {
        return false;
    }
    if (is_battle_zone(destination.zone)) {
        const bool already_in_destination_row = current->side == destination.side && current->zone == destination.zone;
        if (!already_in_destination_row && !row_has_space(state, destination.side, destination.zone)) {
            return false;
        }
    }

    state.move_card(entity_id, destination);
    remove_board_bound_listeners_for_source(state, entity_id, destination);
    if (RuntimeCard* card = state.find_card(entity_id)) {
        if (is_battle_zone(destination.zone)) {
            card->controller_id = destination.side;
        }
    }
    return true;
}

EntityId primitive_spawn_card(GameState& state, const CardDefinition& definition, PlayerId owner_id, Location destination) {
    if (!state.card_catalog) {
        state.card_catalog = std::make_shared<CardCatalog>();
    }
    const CardDefId definition_id = state.card_catalog->intern(definition);
    return primitive_spawn_card(state, definition_id, owner_id, destination);
}

EntityId primitive_spawn_card(GameState& state, CardDefId definition_id, PlayerId owner_id, Location destination) {
    if (is_battle_zone(destination.zone) && !row_has_space(state, destination.side, destination.zone)) {
        return kInvalidEntityId;
    }
    const EntityId entity_id = state.add_card(definition_id, owner_id, destination);
    if (RuntimeCard* card = state.find_card(entity_id)) {
        if (is_battle_zone(destination.zone)) {
            card->controller_id = destination.side;
        }
    }
    return entity_id;
}

bool primitive_boost_card(GameState& state, EntityId entity_id, int amount) {
    if (amount <= 0) {
        return false;
    }
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr || !card->has_power() || boost_blocked_by_active_effect(state, entity_id)) {
        return false;
    }
    card->state.power += amount;
    return true;
}

bool primitive_transform_card(GameState& state, EntityId entity_id, CardDefId definition_id) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr || !state.card_catalog) {
        return false;
    }
    const CardDefinition& definition = state.card_catalog->at(definition_id);
    card->definition_id = definition_id;
    card->definition = &definition;
    card->state = CardState::from_definition(definition);
    card->runtime = {};
    card->memory = {};
    return true;
}

bool primitive_reset_card(GameState& state, EntityId entity_id) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr || card->definition == nullptr) {
        return false;
    }

    // Reset restores the card's printed runtime state without changing its
    // identity, owner or current controller. Callers that implement Replay
    // should move the card off the battlefield before invoking this primitive
    // so board-bound listeners are detached before state/memory are cleared.
    card->state = CardState::from_definition(*card->definition);
    card->runtime = {};
    card->memory = {};
    return true;
}

bool primitive_set_power(GameState& state, EntityId entity_id, int power) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr || !card->has_power()) {
        return false;
    }
    card->state.power = std::max(0, power);
    return true;
}

bool primitive_damage_card(GameState& state, EntityId entity_id, int amount) {
    if (amount <= 0) {
        return false;
    }
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr || !card->has_power()) {
        return false;
    }

    int remaining = amount;
    if (card->state.armor > 0) {
        const int armor_damage = std::min(card->state.armor, remaining);
        card->state.armor -= armor_damage;
        remaining -= armor_damage;
    }
    if (remaining > 0) {
        card->state.power -= remaining;
    }
    return true;
}

bool primitive_damage_card_ignoring_armor(GameState& state, EntityId entity_id, int amount) {
    if (amount <= 0) {
        return false;
    }
    RuntimeCard* card = state.find_card(entity_id);
    const auto location = state.location_of(entity_id);
    if (card == nullptr || !card->has_power() || !location.has_value() || !is_battle_zone(location->zone)) {
        return false;
    }

    // Bleeding is still damage, so Shield can block one tick, but Armor does not.
    if (card->state.shield) {
        card->state.shield = false;
        return true;
    }

    card->state.power -= amount;
    return true;
}

bool primitive_destroy_card(GameState& state, EntityId entity_id, Zone destination) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr) {
        return false;
    }
    if (destination != Zone::Cemetery && destination != Zone::Banished) {
        throw std::invalid_argument("destroy destination must be Cemetery or Banished");
    }
    if (card->has_power() && destination == Zone::Cemetery && card->state.power < 0) {
        card->state.power = 0;
    }
    const Location target_location{card->owner_id, destination, state.player(card->owner_id).zone_size(destination)};
    primitive_move_card(state, entity_id, target_location);
    return true;
}


bool primitive_summon_card(GameState& state, EntityId entity_id, Location destination) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr || !is_battle_zone(destination.zone)) {
        return false;
    }
    const auto current = state.location_of(entity_id);
    if (!current.has_value()) {
        return false;
    }
    if (current->zone != Zone::Deck && current->zone != Zone::Hand && current->zone != Zone::Stay && current->zone != Zone::Cemetery) {
        return false;
    }
    return primitive_move_card(state, entity_id, destination);
}

bool primitive_banish_card(GameState& state, EntityId entity_id) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr) {
        return false;
    }
    primitive_move_card(state, entity_id, Location{card->owner_id, Zone::Banished, state.player(card->owner_id).banished.size()});
    return true;
}

bool primitive_discard_card(GameState& state, EntityId entity_id) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr) {
        return false;
    }
    const auto current = state.location_of(entity_id);
    if (!current.has_value() || current->zone != Zone::Hand) {
        return false;
    }
    primitive_move_card(state, entity_id, Location{card->owner_id, Zone::Cemetery, state.player(card->owner_id).cemetery.size()});
    return true;
}

int primitive_consume_card(GameState& state, EntityId source_entity_id, EntityId target_entity_id) {
    RuntimeCard* source = state.find_card(source_entity_id);
    RuntimeCard* target = state.find_card(target_entity_id);
    if (source == nullptr || target == nullptr || !source->has_power() || !target->has_power()) {
        return 0;
    }
    const auto source_location = state.location_of(source_entity_id);
    const auto target_location = state.location_of(target_entity_id);
    if (!source_location.has_value() || !target_location.has_value() || !is_battle_zone(source_location->zone)) {
        return 0;
    }
    if (!is_battle_zone(target_location->zone) && target_location->zone != Zone::Cemetery) {
        return 0;
    }
    const int boost_amount = std::max(0, target->state.power);
    if (target_location->zone == Zone::Cemetery) {
        primitive_banish_card(state, target_entity_id);
    } else {
        const Zone destination = target->state.doomed ? Zone::Banished : Zone::Cemetery;
        primitive_destroy_card(state, target_entity_id, destination);
    }
    if (boost_amount > 0) {
        primitive_boost_card(state, source_entity_id, boost_amount);
    }
    return boost_amount;
}

int primitive_drain_card(GameState& state, EntityId source_entity_id, EntityId target_entity_id, int amount) {
    if (amount <= 0) {
        return 0;
    }
    RuntimeCard* source = state.find_card(source_entity_id);
    RuntimeCard* target = state.find_card(target_entity_id);
    if (source == nullptr || target == nullptr || !source->has_power() || !target->has_power()) {
        return 0;
    }
    if (!primitive_damage_card_ignoring_armor(state, target_entity_id, amount)) {
        return 0;
    }
    primitive_boost_card(state, source_entity_id, amount);
    return amount;
}

bool primitive_add_armor(GameState& state, EntityId entity_id, int amount) {
    if (amount <= 0) {
        return false;
    }
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr || !card->has_power()) {
        return false;
    }
    card->state.armor += amount;
    return true;
}

bool primitive_add_status(GameState& state, EntityId entity_id, CardStatus status, int amount) {
    RuntimeCard* card = state.find_card(entity_id);
    const auto location = state.location_of(entity_id);
    if (card == nullptr || !location.has_value() || !is_battle_zone(location->zone)) {
        return false;
    }
    if (status != CardStatus::Veil && card->state.veil) {
        return false;
    }

    const int positive_amount = std::max(0, amount);
    switch (status) {
        case CardStatus::Shield:
            card->state.shield = true;
            return true;
        case CardStatus::Infiltration:
            card->state.infiltration = true;
            return true;
        case CardStatus::Spying:
            card->state.spying = true;
            return true;
        case CardStatus::Locked:
            card->state.locked = true;
            return true;
        case CardStatus::Resilience:
            card->state.resilience = true;
            return true;
        case CardStatus::Doomed:
            card->state.doomed = true;
            return true;
        case CardStatus::Veil:
            card->state.veil = true;
            return true;
        case CardStatus::Poison:
            card->state.poison += std::max(1, positive_amount);
            return true;
        case CardStatus::Bleeding: {
            if (positive_amount <= 0) {
                return false;
            }
            const int cancelled = std::min(card->state.vitality, positive_amount);
            card->state.vitality -= cancelled;
            const int remaining = positive_amount - cancelled;
            if (remaining > 0) {
                card->state.bleeding += remaining;
            }
            return cancelled > 0 || remaining > 0;
        }
        case CardStatus::Vitality: {
            if (positive_amount <= 0) {
                return false;
            }
            const int cancelled = std::min(card->state.bleeding, positive_amount);
            card->state.bleeding -= cancelled;
            const int remaining = positive_amount - cancelled;
            if (remaining > 0) {
                card->state.vitality += remaining;
            }
            return cancelled > 0 || remaining > 0;
        }
        case CardStatus::Bounty:
            card->state.bounty = true;
            return true;
        case CardStatus::Immune:
            card->state.immune = true;
            return true;
        case CardStatus::Defender:
            card->state.defender = true;
            return true;
        case CardStatus::Rupture:
            card->state.rupture = true;
            return true;
    }
    return false;
}


std::optional<int> primitive_modify_card_counter(
    GameState& state,
    EntityId entity_id,
    CardCounterField field,
    int amount,
    CardCounterMode mode
) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr) {
        return std::nullopt;
    }

    int* value = nullptr;
    switch (field) {
        case CardCounterField::Cooldown: value = &card->state.cooldown; break;
        case CardCounterField::Countdown: value = &card->state.countdown; break;
        case CardCounterField::Timer: value = &card->state.timer; break;
        case CardCounterField::Bleeding: value = &card->state.bleeding; break;
        case CardCounterField::Vitality: value = &card->state.vitality; break;
    }
    if (value == nullptr) {
        return std::nullopt;
    }
    const int next = mode == CardCounterMode::Set ? amount : (*value + amount);
    *value = std::max(0, next);
    return *value;
}

bool primitive_purify_card(GameState& state, EntityId entity_id) {
    RuntimeCard* card = state.find_card(entity_id);
    if (card == nullptr) {
        return false;
    }
    card->state.shield = false;
    card->state.infiltration = false;
    card->state.spying = false;
    card->state.locked = false;
    card->state.resilience = false;
    card->state.immune = false;
    card->state.defender = false;
    card->state.rupture = false;
    card->state.veil = false;
    card->state.bounty = false;
    card->state.poison = 0;
    card->state.bleeding = 0;
    card->state.vitality = 0;
    for (auto it = state.listeners.begin(); it != state.listeners.end();) {
        if (it->second.origin == RuntimeListenerOrigin::Infusion
            && it->second.source_id == std::optional<EntityId>{entity_id}) {
            it = state.listeners.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

int infusion_count(const GameState& state, EntityId entity_id) {
    return static_cast<int>(std::count_if(state.listeners.begin(), state.listeners.end(), [&](const auto& entry) {
        const RuntimeListener& listener = entry.second;
        return listener.origin == RuntimeListenerOrigin::Infusion
            && listener.source_id == std::optional<EntityId>{entity_id};
    }));
}

}  // namespace gwent
