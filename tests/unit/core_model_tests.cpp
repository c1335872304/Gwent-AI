#include <cassert>
#include <iostream>
#include <stdexcept>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"

using namespace gwent;

namespace {

void test_enum_roundtrip() {
    assert(to_string(Zone::Melee) == "MELEE");
    assert(zone_from_string("MELEE") == Zone::Melee);
    assert(to_string(CardType::Stratagem) == "STRATAGEM");
    assert(card_type_from_string("STRATAGEM") == CardType::Stratagem);
    assert(to_string(CardType::Artifact) == "ARTIFACT");
    assert(card_type_from_string("ARTIFACT") == CardType::Artifact);
}

void test_unit_artifact_and_stratagem_power_contract() {
    CardDefinition unit = make_unit_definition("u1", "Test Unit", 5, Faction::NorthernRealms);
    CardDefinition artifact = make_artifact_definition("a1", "Test Artifact");
    CardDefinition stratagem = make_stratagem_definition("s1", "Test Stratagem");

    // Guard against bad imported data: even if an artifact/stratagem accidentally
    // has a base_power field, runtime logic still treats it as no-power.
    artifact.base_power = 77;
    stratagem.base_power = 99;

    assert(unit.is_unit_card());
    assert(unit.has_power());

    assert(artifact.card_type == CardType::Artifact);
    assert(!artifact.is_unit_card());
    assert(!artifact.has_power());

    assert(stratagem.card_type == CardType::Stratagem);
    assert(!stratagem.is_unit_card());
    assert(!stratagem.has_power());

    RuntimeCard unit_card = RuntimeCard::create(1, unit, 0);
    RuntimeCard artifact_card = RuntimeCard::create(2, artifact, 0);
    RuntimeCard stratagem_card = RuntimeCard::create(3, stratagem, 0);

    assert(unit_card.has_power());
    assert(unit_card.state.base_power == 5);
    assert(unit_card.state.power == 5);

    assert(!artifact_card.has_power());
    assert(artifact_card.state.base_power == 0);
    assert(artifact_card.state.power == 0);

    assert(!stratagem_card.has_power());
    assert(stratagem_card.state.base_power == 0);
    assert(stratagem_card.state.power == 0);
}

void test_game_state_zones_and_scores() {
    GameState state;
    CardDefinition unit_a = make_unit_definition("u1", "Five", 5);
    CardDefinition unit_b = make_unit_definition("u2", "Three", 3);
    CardDefinition artifact = make_artifact_definition("a1", "Artifact");
    CardDefinition stratagem = make_stratagem_definition("s1", "Tactical Advantage");

    const EntityId a = state.add_card(unit_a, 0, Location{0, Zone::Melee, 0});
    const EntityId art = state.add_card(artifact, 0, Location{0, Zone::Melee, 1});
    const EntityId s = state.add_card(stratagem, 0, Location{0, Zone::Melee, 2});
    const EntityId b = state.add_card(unit_b, 0, Location{0, Zone::Ranged, 0});

    assert((state.location_of(a) == Location{0, Zone::Melee, 0}));
    assert((state.location_of(art) == Location{0, Zone::Melee, 1}));
    assert((state.location_of(s) == Location{0, Zone::Melee, 2}));
    assert((state.location_of(b) == Location{0, Zone::Ranged, 0}));

    assert(state.row_score(0, Zone::Melee) == 5);
    assert(state.row_score(0, Zone::Ranged) == 3);
    assert(state.board_score(0) == 8);

    // Temporary boosts/damage do not follow a unit into the cemetery.
    state.find_card(a)->state.power = 9;
    state.move_card(a, Location{0, Zone::Cemetery, 0});
    assert((state.location_of(a) == Location{0, Zone::Cemetery, 0}));
    assert(state.find_card(a)->state.power == state.find_card(a)->state.base_power);
    assert(state.find_card(a)->state.power == 5);
    assert((state.location_of(art) == Location{0, Zone::Melee, 0}));
    assert((state.location_of(s) == Location{0, Zone::Melee, 1}));
    assert(state.row_score(0, Zone::Melee) == 0);
    assert(state.board_score(0) == 3);

    // "Base power" means the card's current runtime base power, so legitimate
    // base-power changes remain meaningful when the card enters the cemetery.
    state.find_card(b)->state.base_power = 6;
    state.find_card(b)->state.power = 1;
    state.move_card(b, Location{0, Zone::Cemetery, 1});
    assert(state.find_card(b)->state.power == 6);
}

void test_leader_zone() {
    GameState state;
    CardDefinition leader;
    leader.id = "leader1";
    leader.name = "Leader";
    leader.card_type = CardType::Leader;

    const EntityId entity_id = state.add_card(leader, 1, Location{1, Zone::Leader, 0});
    assert(state.player(1).leader == entity_id);
    assert((state.location_of(entity_id) == Location{1, Zone::Leader, 0}));

    state.move_card(entity_id, Location{1, Zone::Banished, 0});
    assert(!state.player(1).leader.has_value());
    assert((state.location_of(entity_id) == Location{1, Zone::Banished, 0}));
}

}  // namespace

int main() {
    test_enum_roundtrip();
    test_unit_artifact_and_stratagem_power_contract();
    test_game_state_zones_and_scores();
    test_leader_zone();

    std::cout << "gwent_core_model_tests: OK\n";
    return 0;
}
