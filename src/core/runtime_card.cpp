#include "gwent/core/runtime_card.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

namespace gwent {
namespace {

bool metadata_bool(const CardDefinition& definition, std::string_view key, bool default_value = false) {
    const auto it = definition.metadata.find(std::string(key));
    if (it == definition.metadata.end()) {
        return default_value;
    }
    return it->second == "true" || it->second == "1" || it->second == "yes" || it->second == "on";
}

int metadata_int(const CardDefinition& definition, std::string_view key, int default_value = 0) {
    const auto it = definition.metadata.find(std::string(key));
    if (it == definition.metadata.end()) {
        return default_value;
    }

    int value = default_value;
    const std::string& text = it->second;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return default_value;
    }
    return value;
}

}  // namespace

bool CardState::is_dead() const noexcept {
    return power <= 0;
}

CardState CardState::from_definition(const CardDefinition& definition) {
    CardState state;

    // Non-units, especially Stratagems, are intentionally normalized to zero
    // runtime power even if bad static data accidentally gives them base_power.
    if (definition.has_power()) {
        state.base_power = definition.base_power;
        state.power = definition.base_power;
    }

    state.armor = std::max(0, definition.base_armor);
    state.spying = definition.has_category("spying");
    state.shield = metadata_bool(definition, "shield");
    state.resilience = metadata_bool(definition, "resilience");
    state.doomed = definition.doomed || metadata_bool(definition, "doomed");
    state.veil = metadata_bool(definition, "veil");
    state.immune = metadata_bool(definition, "immune");
    state.defender = metadata_bool(definition, "defender");
    state.rupture = metadata_bool(definition, "rupture");
    state.countdown = std::max(0, metadata_int(definition, "countdown", metadata_int(definition, "counter", 0)));
    state.order_charges = std::max(0, metadata_int(definition, "order_charges", metadata_int(definition, "charges", 0)));
    return state;
}

bool RuntimeCard::is_token() const noexcept {
    return definition != nullptr && definition->is_token;
}

bool RuntimeCard::is_unit_card() const noexcept {
    return definition != nullptr && definition->is_unit_card();
}

bool RuntimeCard::has_power() const noexcept {
    return definition != nullptr && definition->has_power();
}

bool RuntimeCard::has_category(std::string_view category) const noexcept {
    return definition != nullptr && definition->has_category(category);
}

std::vector<std::string> RuntimeCard::effective_categories() const {
    // First milestone: no infusion system yet, so runtime categories equal static categories.
    return definition == nullptr ? std::vector<std::string>{} : definition->categories;
}

std::string RuntimeCard::effective_main_category() const {
    if (definition == nullptr) {
        return {};
    }
    if (!definition->main_category.empty()) {
        return definition->main_category;
    }
    return definition->categories.empty() ? std::string{} : definition->categories.front();
}

RuntimeCard RuntimeCard::create(
    EntityId entity_id,
    CardDefId definition_id,
    const CardDefinition& definition,
    PlayerId owner_id
) {
    RuntimeCard card;
    card.entity_id = entity_id;
    card.definition_id = definition_id;
    card.definition = &definition;
    card.owner_id = owner_id;
    card.controller_id = owner_id;
    card.state = CardState::from_definition(definition);
    return card;
}

RuntimeCard RuntimeCard::create(EntityId entity_id, const CardDefinition& definition, PlayerId owner_id) {
    return create(entity_id, kInvalidCardDefId, definition, owner_id);
}

}  // namespace gwent
