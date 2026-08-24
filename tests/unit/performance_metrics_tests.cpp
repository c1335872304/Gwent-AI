#include <cassert>
#include <iostream>
#include <string>

#include "gwent/api/core.hpp"
#include "gwent/core/performance.hpp"

using namespace gwent;

namespace {

void test_facade_records_profiled_operations() {
    PerformanceCounters counters;
    api::DeckAMatchConfig config;
    config.seed = 0;
    config.starting_player_id = kPlayerZero;
    config.kernel_config.performance_counters = &counters;
    config.kernel_config.invariant_policy = InvariantPolicy::AfterApply;

    api::DeckAGame game = api::DeckAGame::create(config);
    const auto actions = game.legal_actions();
    assert(!actions.empty());
    assert(counters.current_decision_calls >= 1);
    assert(counters.legal_actions_calls >= 1);
    // v6 hot path enumerates the two battlefield rows directly; the heavy
    // diagnostic EntityIndex is reserved for invariants/debug queries.
    assert(counters.entity_index_builds == 0);
    assert(counters.legal_actions_ns > 0);

    const auto before_apply_calls = counters.apply_calls;
    const auto result = game.apply(actions.front());
    assert(result.applied);
    assert(counters.apply_calls == before_apply_calls + 1);
    assert(counters.apply_ns > 0);
    assert(counters.kernel_tasks_processed > 0);
    assert(counters.invariant_checks >= 1);
    assert(counters.entity_index_builds >= 1);

    const auto snapshot = game.snapshot_json();
    assert(!snapshot.empty());
    assert(counters.snapshot_json_calls >= 1);
    assert(counters.snapshot_json_ns > 0);
}

void test_counters_can_be_serialized_and_reset() {
    PerformanceCounters counters;
    counters.apply_calls = 3;
    counters.legal_actions_calls = 5;
    counters.target_candidates_scanned = 13;

    const std::string json = performance_counters_to_json(counters);
    assert(json.find("\"apply_calls\": 3") != std::string::npos);
    assert(json.find("\"legal_actions_calls\": 5") != std::string::npos);
    assert(json.find("\"target_candidates_scanned\": 13") != std::string::npos);

    counters.reset();
    assert(counters.apply_calls == 0);
    assert(counters.legal_actions_calls == 0);
    assert(counters.target_candidates_scanned == 0);
}

}  // namespace

int main() {
    test_facade_records_profiled_operations();
    test_counters_can_be_serialized_and_reset();
    std::cout << "gwent_performance_metrics_tests: OK\n";
    return 0;
}
