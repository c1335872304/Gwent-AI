#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gwent/core/enums.hpp"

namespace gwent {

// Static card template. It describes what a card is before a match starts.
// Runtime mutations belong in RuntimeCard/CardState, not here.
struct CardDefinition {
    std::string id;
    std::string name;
    int base_power = 0;
    Faction faction = Faction::Neutral;
    Faction secondary_faction = Faction::Neutral;
    bool has_secondary_faction = false;
    CardType card_type = CardType::Unit;
    CardUseInfo use_info = CardUseInfo::MyPlace;
    std::vector<std::string> effect_ids;
    std::vector<std::string> categories;
    std::string main_category;
    std::string description;
    int provision = 0;
    bool doomed = false;
    int base_armor = 0;
    std::string color;
    std::string rarity;
    bool is_token = false;

    // Keep this simple in the first C++ milestone. Card-data JSON loading can
    // later upgrade this to a typed value model, but core logic should not need
    // Python-style arbitrary objects.
    std::unordered_map<std::string, std::string> metadata;

    bool operator==(const CardDefinition&) const = default;

    [[nodiscard]] bool is_unit_card() const noexcept;
    [[nodiscard]] bool has_power() const noexcept;
    [[nodiscard]] bool has_category(std::string_view category) const noexcept;
    [[nodiscard]] bool has_effect(std::string_view effect_id) const noexcept;
};

CardDefinition make_unit_definition(
    std::string id,
    std::string name,
    int base_power,
    Faction faction = Faction::Neutral
);

CardDefinition make_stratagem_definition(
    std::string id,
    std::string name
);

CardDefinition make_special_definition(
    std::string id,
    std::string name,
    Faction faction = Faction::Neutral
);

CardDefinition make_artifact_definition(
    std::string id,
    std::string name,
    Faction faction = Faction::Neutral
);

CardDefinition make_leader_definition(
    std::string id,
    std::string name,
    Faction faction = Faction::Neutral
);

}  // namespace gwent
