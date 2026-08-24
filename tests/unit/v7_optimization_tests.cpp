#include <cassert>
#include <iostream>
#include <string_view>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/effect_registry.hpp"
#include "gwent/engine/task.hpp"

using namespace gwent;

namespace {

void test_dense_entity_storage_tracks_entity_ids_directly() {
    GameState state;
    const CardDefinition a = make_unit_definition("dense_a", "Dense A", 3);
    const CardDefinition b = make_unit_definition("dense_b", "Dense B", 4);
    const CardDefinition c = make_unit_definition("dense_c", "Dense C", 5);

    const EntityId id0 = state.add_card(a, kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    const EntityId id1 = state.add_card(b, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId id2 = state.add_card(c, kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    assert(id0 == 0 && id1 == 1 && id2 == 2);
    assert(state.cards.size() == 3);
    assert(state.locations.size() == 3);
    assert(state.find_card(id1) == &state.cards[1]);
    assert(state.location_of(id2).has_value());
    assert(state.location_of(id2)->zone == Zone::Melee);
    assert(state.find_card(kInvalidEntityId) == nullptr);
    assert(!state.location_of(9999).has_value());

    const GameState snapshot = state;
    assert(snapshot.card_catalog == state.card_catalog);
    assert(snapshot.cards.size() == state.cards.size());
    assert(snapshot.find_card(id2)->definition == state.find_card(id2)->definition);
}

void test_runtime_memory_flat_map_contract() {
    RuntimeMemory memory;
    assert(memory.empty());
    assert(memory.count("alpha") == 0);

    memory["alpha"] = "1";
    memory["beta"] = "2";
    memory["alpha"] = "3";
    assert(memory.size() == 2);
    assert(memory.count("alpha") == 1);
    assert(memory.at("alpha") == "3");
    assert(memory.find(std::string_view{"beta"}) != memory.end());
    assert(memory.erase("alpha") == 1);
    assert(memory.erase("alpha") == 0);
    assert(memory.count("alpha") == 0);
}

void test_spawn_task_uses_catalog_id_not_definition_copy() {
    GameState state;
    CardDefinition token = make_unit_definition("spawn_token", "Spawn Token", 2);
    const CardDefId definition_id = state.card_catalog->intern(token);
    const Task task = Task::spawn_card(
        kPlayerZero,
        definition_id,
        Location{kPlayerZero, Zone::Melee, 0},
        kInvalidEntityId
    );
    assert(task.type == TaskType::SpawnCard);
    assert(task.card_definition_id == definition_id);
    assert(state.card_catalog->at(task.card_definition_id).id == "spawn_token");

    // Guard the intended compact representation. These bounds leave ABI room
    // across common 32/64-bit standard-library implementations while catching
    // accidental reintroduction of CardDefinition/unordered_map payloads.
    static_assert(sizeof(Task) <= 128);
    static_assert(sizeof(RuntimeCard) <= 128);
}

void test_transparent_string_lookup_for_catalog_and_effect_registry() {
    GameState state;
    const CardDefinition def = make_unit_definition("lookup_card", "Lookup", 1);
    const CardDefId card_id = state.card_catalog->intern(def);
    (void)card_id;
    const std::string_view id = "lookup_card";
    assert(state.card_catalog->contains(id));
    assert(state.card_catalog->id_of(id).has_value());
    assert(state.card_catalog->find(id) != nullptr);

    EffectRegistry effects;
    effects.register_handler("lookup.effect", [](KernelContext&, const EffectCall&) {});
    assert(effects.contains(std::string_view{"lookup.effect"}));
}

}  // namespace

int main() {
    test_dense_entity_storage_tracks_entity_ids_directly();
    test_runtime_memory_flat_map_contract();
    test_spawn_task_uses_catalog_id_not_definition_copy();
    test_transparent_string_lookup_for_catalog_and_effect_registry();
    std::cout << "v7_optimization_tests passed\n";
    return 0;
}
