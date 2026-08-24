#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/ids.hpp"

namespace gwent {

struct CardState {
    int base_power = 0;
    int power = 0;
    int armor = 0;

    bool shield = false;
    bool infiltration = false;
    bool spying = false;
    bool locked = false;
    bool resilience = false;
    bool immune = false;
    bool defender = false;
    bool rupture = false;
    bool doomed = false;
    bool veil = false;
    bool bounty = false;

    int poison = 0;
    int bleeding = 0;
    int vitality = 0;

    bool deploying = false;
    bool face_down = false;

    int order_charges = 0;
    int timer = 0;
    int cooldown = 0;
    int countdown = 0;

    [[nodiscard]] bool is_dead() const noexcept;

    static CardState from_definition(const CardDefinition& definition);
};

// High-frequency state-machine flags are typed so misspelled string keys cannot
// silently alter core action semantics. Card-specific low-frequency metadata
// remains in RuntimeCard::memory for data-driven effects.

// Card-specific runtime metadata is intentionally sparse (typically zero or a
// handful of entries). A flat vector avoids the ~node/hash-table footprint of
// std::unordered_map on every RuntimeCard and makes GameState snapshots much
// cheaper while preserving the small map-like API used by card handlers.
class RuntimeMemory {
public:
    using value_type = std::pair<std::string, std::string>;
    using container_type = std::vector<value_type>;
    using iterator = container_type::iterator;
    using const_iterator = container_type::const_iterator;

    [[nodiscard]] iterator begin() noexcept { return entries_.begin(); }
    [[nodiscard]] const_iterator begin() const noexcept { return entries_.begin(); }
    [[nodiscard]] iterator end() noexcept { return entries_.end(); }
    [[nodiscard]] const_iterator end() const noexcept { return entries_.end(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    [[nodiscard]] iterator find(std::string_view key) noexcept {
        return std::find_if(entries_.begin(), entries_.end(), [&](const value_type& entry) { return entry.first == key; });
    }
    [[nodiscard]] const_iterator find(std::string_view key) const noexcept {
        return std::find_if(entries_.begin(), entries_.end(), [&](const value_type& entry) { return entry.first == key; });
    }
    [[nodiscard]] std::size_t count(std::string_view key) const noexcept {
        return find(key) == end() ? 0U : 1U;
    }

    std::string& operator[](std::string key) {
        const auto it = find(key);
        if (it != end()) {
            return it->second;
        }
        entries_.emplace_back(std::move(key), std::string{});
        return entries_.back().second;
    }

    [[nodiscard]] const std::string& at(std::string_view key) const {
        const auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("RuntimeMemory key not found");
        }
        return it->second;
    }

    std::size_t erase(std::string_view key) {
        const auto it = find(key);
        if (it == end()) {
            return 0;
        }
        entries_.erase(it);
        return 1;
    }

private:
    container_type entries_;
};

struct CardRuntimeFlags {
    bool entered_this_turn = false;
    bool order_used = false;
    bool order_pending_consume = false;
};

struct RuntimeCard {
    EntityId entity_id = kInvalidEntityId;
    CardDefId definition_id = kInvalidCardDefId;

    // Non-owning cached view into GameState::card_catalog. CardCatalog uses
    // pointer-stable storage and is shared by state snapshots, so copying a
    // RuntimeCard no longer copies static strings/vectors/metadata.
    const CardDefinition* definition = nullptr;

    PlayerId owner_id = kPlayerZero;
    PlayerId controller_id = kPlayerZero;
    CardState state;
    CardRuntimeFlags runtime;
    RuntimeMemory memory;

    [[nodiscard]] bool is_token() const noexcept;
    [[nodiscard]] bool is_unit_card() const noexcept;
    [[nodiscard]] bool has_power() const noexcept;
    [[nodiscard]] bool has_category(std::string_view category) const noexcept;
    [[nodiscard]] std::vector<std::string> effective_categories() const;
    [[nodiscard]] std::string effective_main_category() const;

    static RuntimeCard create(
        EntityId entity_id,
        CardDefId definition_id,
        const CardDefinition& definition,
        PlayerId owner_id
    );

    // Compatibility overload for isolated tests/callers that construct a
    // RuntimeCard without a GameState catalog. The referenced definition must
    // outlive the RuntimeCard.
    static RuntimeCard create(EntityId entity_id, const CardDefinition& definition, PlayerId owner_id);
};

}  // namespace gwent
