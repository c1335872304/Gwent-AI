#include <cassert>
#include <iostream>
#include <vector>

#include "gwent/core/entity_index.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/query.hpp"

using namespace gwent;

namespace {

void test_index_tracks_unique_locations_and_duplicates() {
    GameState state;
    const EntityId unit = state.add_card(make_unit_definition("unit", "Unit", 4), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId hand = state.add_card(make_unit_definition("hand", "Hand", 3), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId leader = state.add_card(make_leader_definition("leader", "Leader"), kPlayerZero, Location{kPlayerZero, Zone::Leader, 0});

    EntityIndex index = build_entity_index(state);
    assert((index.unique_location_of(unit) == Location{kPlayerZero, Zone::Melee, 0}));
    assert((index.unique_location_of(hand) == Location{kPlayerZero, Zone::Hand, 0}));
    assert((index.unique_location_of(leader) == Location{kPlayerZero, Zone::Leader, 0}));
    assert(index.has_unique_location(unit));
    assert(index.by_definition_id.at("unit").size() == 1);

    // Direct vector corruption should be visible to the index before any
    // invariant checker tries to interpret it.
    state.player(kPlayerZero).hand.push_back(unit);
    index = build_entity_index(state);
    assert(!index.has_unique_location(unit));
    assert(index.memberships.at(unit).size() == 2);
}

void test_query_layer_preserves_zone_order_and_filters_units() {
    GameState state;
    const EntityId unit_a = state.add_card(make_unit_definition("unit_a", "A", 4), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId artifact = state.add_card(make_artifact_definition("artifact", "Artifact"), kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    const EntityId stratagem = state.add_card(make_stratagem_definition("stratagem", "Stratagem"), kPlayerZero, Location{kPlayerZero, Zone::Melee, 2});
    const EntityId unit_b = state.add_card(make_unit_definition("unit_b", "B", 5), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    const EntityId hand = state.add_card(make_unit_definition("hand", "Hand", 3), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    state.find_card(unit_b)->state.deploying = true;

    const EntityIndex index = build_entity_index(state);
    const auto battlefield = query_battlefield_entities(index, kPlayerZero);
    assert((battlefield == std::vector<EntityId>{unit_a, artifact, stratagem, unit_b}));

    const auto units = query_battlefield_units(index, kPlayerZero);
    assert((units == std::vector<EntityId>{unit_a, unit_b}));

    const auto hand_entities = query_zone_entities(state, kPlayerZero, Zone::Hand);
    assert((hand_entities == std::vector<EntityId>{hand}));

    const auto melee_units = query_units_on_row(state, kPlayerZero, Zone::Melee, true);
    assert((melee_units == std::vector<EntityId>{unit_a}));
    const auto ranged_units = query_units_on_row(state, kPlayerZero, Zone::Ranged, true);
    assert(ranged_units.empty());
    assert(has_battlefield_unit(state, kPlayerZero, true));
}

void test_query_layer_excludes_no_power_board_entities() {
    GameState state;
    state.add_card(make_artifact_definition("artifact", "Artifact"), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    state.add_card(make_stratagem_definition("stratagem", "Stratagem"), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});

    const EntityIndex index = build_entity_index(state);
    assert(query_battlefield_entities(index, kPlayerOne).size() == 2);
    assert(query_battlefield_units(index, kPlayerOne).empty());
    assert(!has_battlefield_unit(state, kPlayerOne, true));
}

}  // namespace

int main() {
    test_index_tracks_unique_locations_and_duplicates();
    test_query_layer_preserves_zone_order_and_filters_units();
    test_query_layer_excludes_no_power_board_entities();

    std::cout << "gwent_entity_index_query_tests: OK\n";
    return 0;
}
