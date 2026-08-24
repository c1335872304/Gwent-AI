#include "gwent/core/card_definition.hpp"

#include <algorithm>
#include <utility>

namespace gwent {

bool CardDefinition::is_unit_card() const noexcept {
    return card_type == CardType::Unit;
}

bool CardDefinition::has_power() const noexcept {
    // Only unit cards have power. Artifacts and stratagems are persistent
    // battle-row command/support entities, but they must never participate in
    // score, boost, damage, heal, power-set, base-power-change, or lethal checks.
    return is_unit_card();
}

bool CardDefinition::has_category(std::string_view category) const noexcept {
    return std::find(categories.begin(), categories.end(), category) != categories.end();
}

bool CardDefinition::has_effect(std::string_view effect_id) const noexcept {
    return std::find(effect_ids.begin(), effect_ids.end(), effect_id) != effect_ids.end();
}

CardDefinition make_unit_definition(
    std::string id,
    std::string name,
    int base_power,
    Faction faction
) {
    CardDefinition definition;
    definition.id = std::move(id);
    definition.name = std::move(name);
    definition.base_power = base_power;
    definition.faction = faction;
    definition.card_type = CardType::Unit;
    return definition;
}

CardDefinition make_stratagem_definition(std::string id, std::string name) {
    CardDefinition definition;
    definition.id = std::move(id);
    definition.name = std::move(name);
    definition.base_power = 0;
    definition.faction = Faction::Neutral;
    definition.card_type = CardType::Stratagem;
    definition.use_info = CardUseInfo::MyPlace;
    definition.metadata.emplace("stratagem", "true");
    return definition;
}

CardDefinition make_special_definition(std::string id, std::string name, Faction faction) {
    CardDefinition definition;
    definition.id = std::move(id);
    definition.name = std::move(name);
    definition.base_power = 0;
    definition.faction = faction;
    definition.card_type = CardType::Special;
    definition.use_info = CardUseInfo::AnyRow;
    return definition;
}

CardDefinition make_artifact_definition(std::string id, std::string name, Faction faction) {
    CardDefinition definition;
    definition.id = std::move(id);
    definition.name = std::move(name);
    definition.base_power = 0;
    definition.faction = faction;
    definition.card_type = CardType::Artifact;
    definition.use_info = CardUseInfo::MyPlace;
    definition.metadata.emplace("artifact", "true");
    return definition;
}

CardDefinition make_leader_definition(std::string id, std::string name, Faction faction) {
    CardDefinition definition;
    definition.id = std::move(id);
    definition.name = std::move(name);
    definition.base_power = 0;
    definition.faction = faction;
    definition.card_type = CardType::Leader;
    definition.use_info = CardUseInfo::MyPlace;
    definition.metadata.emplace("leader", "true");
    return definition;
}

}  // namespace gwent
