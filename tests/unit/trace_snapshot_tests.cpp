#include <iostream>
#include <string>

#include "gwent/core/card_definition.hpp"
#include "gwent/game/setup.hpp"
#include "gwent/trace/state_snapshot.hpp"

using namespace gwent;

namespace {

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr << " at " << __FILE__ << ':' << __LINE__ << '\n'; return false; } } while (false)

DeckSpec make_test_deck(int owner) {
    DeckSpec deck;
    deck.leader = make_leader_definition("leader_" + std::to_string(owner), "Leader", Faction::Monsters);
    deck.stratagem = make_stratagem_definition("stratagem_" + std::to_string(owner), "Stratagem");
    for (int i = 0; i < 25; ++i) {
        auto unit = make_unit_definition("unit_" + std::to_string(owner) + "_" + std::to_string(i), "Unit", 4 + (i % 3), Faction::Monsters);
        deck.cards.push_back(DeckCardSpec{unit, 1});
    }
    return deck;
}

bool test_state_snapshot_is_stable_and_contains_core_fields() {
    MatchSetupConfig config;
    config.seed = 7;
    config.starting_player_id = 0;
    config.shuffle_decks = false;

    auto state = make_empty_standard_state(config);
    [[maybe_unused]] auto setup_result = setup_standard_match(state, make_test_deck(0), make_test_deck(1), config);

    trace::SnapshotOptions options;
    const auto json1 = trace::state_to_json(state, options);
    const auto json2 = trace::state_to_json(state, options);
    CHECK(json1 == json2);

    const auto checksum1 = trace::state_checksum(state, options);
    const auto checksum2 = trace::state_checksum(state, options);
    CHECK(checksum1 == checksum2);
    CHECK(checksum1.size() == 16);

    CHECK(json1.find("\"status\": \"RUNNING\"") != std::string::npos);
    CHECK(json1.find("\"round_no\": 1") != std::string::npos);
    CHECK(json1.find("\"current_player_id\": 0") != std::string::npos);
    CHECK(json1.find("\"mulligans_available\": 3") != std::string::npos);
    CHECK(json1.find("\"mulligans_available\": 2") != std::string::npos);
    CHECK(json1.find("\"type\": \"STRATAGEM\"") != std::string::npos);
    CHECK(json1.find("\"has_power\": false") != std::string::npos);
    return true;
}

bool test_trace_document_contains_step_checksums() {
    MatchSetupConfig config;
    config.seed = 0;
    config.starting_player_id = 0;
    config.shuffle_decks = false;

    auto state = make_empty_standard_state(config);
    [[maybe_unused]] auto setup_result = setup_standard_match(state, make_test_deck(0), make_test_deck(1), config);

    trace::SnapshotOptions options;
    trace::TraceDocument doc;
    doc.scenario = "unit";
    doc.seed = config.seed;
    doc.starting_player_id = 0;
    auto state_json = trace::state_to_json(state, options);
    doc.steps.push_back(trace::TraceStep{0, "initial", {}, {}, {}, state_json, trace::state_checksum(state, options)});

    const auto trace_json = trace::trace_to_json(doc, options);
    CHECK(trace_json.find("gwent-golden-trace-v1") != std::string::npos);
    CHECK(trace_json.find("\"checksum\"") != std::string::npos);
    CHECK(trace_json.find("\"scenario\": \"unit\"") != std::string::npos);
    return true;
}

bool test_action_json_contains_insert_position() {
    const Action action = Action::play_card(kPlayerZero, 42, kPlayerZero, Zone::Melee, 2);
    const std::string json = trace::action_to_json(action);
    CHECK(json.find("\"insert_position\": 2") != std::string::npos);
    return true;
}

}  // namespace

int main() {
    if (!test_state_snapshot_is_stable_and_contains_core_fields()) return 1;
    if (!test_trace_document_contains_step_checksums()) return 1;
    if (!test_action_json_contains_insert_position()) return 1;
    std::cout << "trace_snapshot_tests passed\n";
    return 0;
}
