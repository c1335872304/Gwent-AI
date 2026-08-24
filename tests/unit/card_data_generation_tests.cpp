#include <cassert>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "gwent/cards/deck_a.hpp"
#include "gwent/generated/supported_card_data.hpp"

namespace {

const std::string& require_metadata(const gwent::CardDefinition& definition, const std::string& key) {
    const auto it = definition.metadata.find(key);
    assert(it != definition.metadata.end());
    return it->second;
}

void generated_manifest_has_expected_shape() {
    assert(gwent::generated::supported_card_data_schema_version() == "gwent-card-data-v1");
    assert(gwent::generated::supported_card_data_source_path() == "data/cards/supported_cards.json");
    assert(gwent::generated::deck_a_deck_data_source_path() == "data/decks/deck_a.json");
    assert(gwent::generated::deck_b_deck_data_source_path() == "data/decks/deck_b.json");

    const auto definitions = gwent::generated::make_supported_card_definitions();
    assert(definitions.size() == 47);

    std::unordered_set<std::string> ids;
    int leaders = 0;
    int stratagems = 0;
    int tokens = 0;
    for (const auto& definition : definitions) {
        assert(!definition.id.empty());
        assert(!definition.name.empty());
        assert(ids.insert(definition.id).second);
        if (definition.card_type == gwent::CardType::Leader) {
            ++leaders;
            assert(definition.metadata.find("leader") != definition.metadata.end());
        }
        if (definition.card_type == gwent::CardType::Stratagem) {
            ++stratagems;
            assert(definition.metadata.find("stratagem") != definition.metadata.end());
        }
        if (definition.is_token) {
            ++tokens;
            assert(definition.doomed);
        }
        if (!definition.categories.empty()) {
            assert(definition.main_category == definition.categories.front());
        }
        if (definition.card_type != gwent::CardType::Unit) {
            assert(definition.base_power == 0);
        }
    }
    assert(leaders == 2);
    assert(stratagems == 2);
    assert(tokens == 4);
}

void deck_spec_is_generated_from_card_data() {
    const auto deck = gwent::deck_a::make_deck_spec();
    assert(deck.leader.id == gwent::deck_a::kBloodScentId);
    assert(deck.stratagem.id == gwent::deck_a::kArmorerWorkshopId);

    int total_deck_cards = 0;
    for (const auto& [definition, count] : deck.cards) {
        assert(count > 0);
        assert(!definition.is_token);
        assert(definition.card_type != gwent::CardType::Leader);
        assert(definition.card_type != gwent::CardType::Stratagem);
        total_deck_cards += count;
    }
    assert(total_deck_cards == 25);

    const auto catalog = gwent::deck_a::make_catalog();
    assert(catalog.size() == 47);
    assert(catalog.contains(gwent::deck_a::kBloodScentId));
    assert(catalog.contains(gwent::deck_a::kEkimmaraId));
}

void deck_b_spec_is_generated_from_card_data() {
    const auto deck = gwent::generated::make_deck_b_deck_spec();
    assert(deck.leader.id == "200055");
    assert(deck.stratagem.id == "202493");
    int total_deck_cards = 0;
    for (const auto& [definition, count] : deck.cards) {
        assert(count > 0);
        assert(!definition.is_token);
        assert(definition.card_type != gwent::CardType::Leader);
        assert(definition.card_type != gwent::CardType::Stratagem);
        total_deck_cards += count;
    }
    assert(total_deck_cards == 25);
}

void generated_metadata_preserves_targeting_contracts() {
    const auto blood_scent = gwent::deck_a::make_blood_scent_definition();
    assert(blood_scent.card_type == gwent::CardType::Leader);
    assert(require_metadata(blood_scent, "leader_target_selector") == "enemy_unit");
    assert(require_metadata(blood_scent, "charges") == "3");

    const auto armorer = gwent::deck_a::make_armorer_workshop_definition();
    assert(armorer.card_type == gwent::CardType::Stratagem);
    assert(require_metadata(armorer, "order_target_selector") == "own_hand_unit");

    const auto dettlaff = gwent::deck_a::make_dettlaff_aep_definition();
    assert(dettlaff.base_power == 7);
    assert(require_metadata(dettlaff, "order_target_selector") == "bleeding_enemy_unit");
    assert(require_metadata(dettlaff, "cooldown") == "1");

    const auto garkain = gwent::deck_a::make_garkain_definition();
    assert(require_metadata(garkain, "order_rows") == "melee");
    assert(require_metadata(garkain, "order_target_selector") == "enemy_unit");

    const auto ekimmara = gwent::deck_a::make_ekimmara_definition();
    assert(ekimmara.is_token);
    assert(ekimmara.doomed);
    assert(ekimmara.provision == 0);
}

void unknown_card_id_is_rejected() {
    bool threw = false;
    try {
        (void)gwent::generated::make_supported_card_definition("missing-card-id");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main() {
    generated_manifest_has_expected_shape();
    deck_spec_is_generated_from_card_data();
    deck_b_spec_is_generated_from_card_data();
    generated_metadata_preserves_targeting_contracts();
    unknown_card_id_is_rejected();
    return 0;
}
