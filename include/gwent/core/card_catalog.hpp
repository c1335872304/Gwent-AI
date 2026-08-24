#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/ids.hpp"
#include "gwent/core/string_hash.hpp"

namespace gwent {

// Stable numeric handle into immutable-ish static card data. Runtime entities
// carry only this compact id (plus a non-owning cached pointer), while the
// catalog is shared by GameState snapshots and parallel match state copies.
class CardCatalog {
public:
    // Legacy/catalog-building API: inserts a new definition or replaces the
    // existing definition with the same textual card id while keeping its
    // numeric CardDefId stable.
    bool add(CardDefinition definition);

    // Runtime interning API. Reuses an identical definition; conflicting
    // definitions under the same textual id are rejected instead of mutating
    // static data already referenced by live RuntimeCards.
    [[nodiscard]] CardDefId intern(CardDefinition definition);

    [[nodiscard]] bool contains(std::string_view card_id) const;
    [[nodiscard]] const CardDefinition* find(std::string_view card_id) const noexcept;
    [[nodiscard]] const CardDefinition& at(std::string_view card_id) const;
    [[nodiscard]] const CardDefinition& at(CardDefId definition_id) const;
    [[nodiscard]] std::optional<CardDefId> id_of(std::string_view card_id) const noexcept;
    [[nodiscard]] std::vector<CardDefinition> all() const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    // deque keeps references/pointers to existing definitions stable when new
    // definitions are appended. Numeric ids are direct deque indices.
    std::deque<CardDefinition> definitions_;
    std::unordered_map<std::string, CardDefId, TransparentStringHash, std::equal_to<>> ids_by_card_id_;
};

}  // namespace gwent
