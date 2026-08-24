#include <cassert>
#include <iostream>
#include <string>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/board_rules.hpp"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/engine/primitives.hpp"
#include "gwent/trace/state_snapshot.hpp"

using namespace gwent;

namespace {

GameState playing_state() {
    GameState state;
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.engine_phase = EnginePhase::ActionWindow;
    state.round_no = 1;
    state.turn_no = 1;
    state.current_player_id = kPlayerZero;
    state.match_config["row_capacity"] = "2";
    return state;
}

void test_runtime_cards_share_catalog_definitions_across_snapshots() {
    GameState state;
    CardDefinition def = make_unit_definition("shared", "Shared", 5, Faction::Monsters);
    def.effect_ids = {"deploy:test"};
    def.metadata["cooldown"] = "2";

    const EntityId a = state.add_card(def, kPlayerZero, Location{kPlayerZero, Zone::Deck, 0});
    const EntityId b = state.add_card(def, kPlayerZero, Location{kPlayerZero, Zone::Deck, 1});
    const RuntimeCard* card_a = state.find_card(a);
    const RuntimeCard* card_b = state.find_card(b);
    assert(card_a != nullptr && card_b != nullptr);
    assert(card_a->definition_id == card_b->definition_id);
    assert(card_a->definition == card_b->definition);
    assert(card_a->definition == &state.card_catalog->at(card_a->definition_id));

    const GameState snapshot = state;
    const RuntimeCard* copied = snapshot.find_card(a);
    assert(copied != nullptr);
    assert(snapshot.card_catalog == state.card_catalog);
    assert(copied->definition == card_a->definition);
}

void test_row_capacity_is_shared_by_legality_and_primitives() {
    GameState state = playing_state();
    const CardDefinition filler = make_unit_definition("filler", "Filler", 1);
    state.add_card(filler, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(filler, kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    assert(!row_has_space(state, kPlayerZero, Zone::Melee));
    assert(row_has_space(state, kPlayerZero, Zone::Ranged));

    const EntityId hand = state.add_card(
        make_unit_definition("hand", "Hand", 4),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );
    const RuntimeCard* hand_card = state.find_card(hand);
    assert(hand_card != nullptr);
    const auto rows = legal_row_targets_for_card(state, kPlayerZero, *hand_card);
    assert(rows.size() == 1);
    assert(rows.front().side == kPlayerZero);
    assert(rows.front().zone == Zone::Ranged);

    const std::size_t before_cards = state.cards.size();
    const EntityId spawned = primitive_spawn_card(
        state,
        make_unit_definition("spawn", "Spawn", 2),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, state.player(kPlayerZero).row(Zone::Melee).size()}
    );
    assert(spawned == kInvalidEntityId);
    assert(state.cards.size() == before_cards);

    const EntityId summon = state.add_card(
        make_unit_definition("summon", "Summon", 3),
        kPlayerZero,
        Location{kPlayerZero, Zone::Deck, 0}
    );
    assert(!primitive_summon_card(
        state,
        summon,
        Location{kPlayerZero, Zone::Melee, state.player(kPlayerZero).row(Zone::Melee).size()}
    ));
    assert(state.location_of(summon)->zone == Zone::Deck);

    const EntityId mover = state.add_card(
        make_unit_definition("mover", "Mover", 3),
        kPlayerZero,
        Location{kPlayerZero, Zone::Ranged, 0}
    );
    assert(!primitive_move_card(
        state,
        mover,
        Location{kPlayerZero, Zone::Melee, state.player(kPlayerZero).row(Zone::Melee).size()}
    ));
    assert(state.location_of(mover)->zone == Zone::Ranged);
}

void test_typed_runtime_flags_preserve_snapshot_contract() {
    GameState state;
    const EntityId id = state.add_card(
        make_unit_definition("flags", "Flags", 3),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    RuntimeCard* card = state.find_card(id);
    assert(card != nullptr);
    card->runtime.entered_this_turn = true;
    card->runtime.order_used = true;
    card->runtime.order_pending_consume = true;

    trace::SnapshotOptions options;
    options.pretty = false;
    const std::string json = trace::state_to_json(state, options);
    assert(json.find("\"entered_this_turn\":\"true\"") != std::string::npos);
    assert(json.find("\"order_used\":\"true\"") != std::string::npos);
    assert(json.find("\"order_pending_consume\":\"true\"") != std::string::npos);
}

}  // namespace

int main() {
    test_runtime_cards_share_catalog_definitions_across_snapshots();
    test_row_capacity_is_shared_by_legality_and_primitives();
    test_typed_runtime_flags_preserve_snapshot_contract();
    std::cout << "v6_optimization_tests passed\n";
    return 0;
}
