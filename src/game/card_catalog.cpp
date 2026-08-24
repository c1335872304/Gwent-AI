#include "gwent/core/card_catalog.hpp"

#include <stdexcept>
#include <utility>

namespace gwent {

bool CardCatalog::add(CardDefinition definition) {
    const std::string key = definition.id;
    const auto existing = ids_by_card_id_.find(key);
    if (existing != ids_by_card_id_.end()) {
        definitions_[static_cast<std::size_t>(existing->second)] = std::move(definition);
        return false;
    }

    if (definitions_.size() >= static_cast<std::size_t>(kInvalidCardDefId)) {
        throw std::overflow_error("CardCatalog exhausted CardDefId range");
    }
    const CardDefId definition_id = static_cast<CardDefId>(definitions_.size());
    definitions_.push_back(std::move(definition));
    ids_by_card_id_.emplace(key, definition_id);
    return true;
}

CardDefId CardCatalog::intern(CardDefinition definition) {
    const auto existing = ids_by_card_id_.find(definition.id);
    if (existing != ids_by_card_id_.end()) {
        const CardDefId definition_id = existing->second;
        const CardDefinition& stored = definitions_[static_cast<std::size_t>(definition_id)];
        if (!(stored == definition)) {
            throw std::logic_error(
                "conflicting CardDefinition for id already interned in GameState catalog: " + definition.id
            );
        }
        return definition_id;
    }

    if (definitions_.size() >= static_cast<std::size_t>(kInvalidCardDefId)) {
        throw std::overflow_error("CardCatalog exhausted CardDefId range");
    }
    const CardDefId definition_id = static_cast<CardDefId>(definitions_.size());
    const std::string key = definition.id;
    definitions_.push_back(std::move(definition));
    ids_by_card_id_.emplace(key, definition_id);
    return definition_id;
}

bool CardCatalog::contains(std::string_view card_id) const {
    return ids_by_card_id_.find(card_id) != ids_by_card_id_.end();
}

const CardDefinition* CardCatalog::find(std::string_view card_id) const noexcept {
    const auto id = id_of(card_id);
    if (!id.has_value()) {
        return nullptr;
    }
    return &definitions_[static_cast<std::size_t>(id.value())];
}

const CardDefinition& CardCatalog::at(std::string_view card_id) const {
    const auto id = id_of(card_id);
    if (!id.has_value()) {
        throw std::out_of_range("card id not found in CardCatalog: " + std::string(card_id));
    }
    return at(id.value());
}

const CardDefinition& CardCatalog::at(CardDefId definition_id) const {
    if (definition_id == kInvalidCardDefId
        || static_cast<std::size_t>(definition_id) >= definitions_.size()) {
        throw std::out_of_range("CardDefId not found in CardCatalog");
    }
    return definitions_[static_cast<std::size_t>(definition_id)];
}

std::optional<CardDefId> CardCatalog::id_of(std::string_view card_id) const noexcept {
    const auto it = ids_by_card_id_.find(card_id);
    if (it == ids_by_card_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<CardDefinition> CardCatalog::all() const {
    return std::vector<CardDefinition>(definitions_.begin(), definitions_.end());
}

std::size_t CardCatalog::size() const noexcept {
    return definitions_.size();
}

}  // namespace gwent
