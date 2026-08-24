#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gwent/core/state.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/task.hpp"

namespace gwent::trace {

struct SnapshotOptions {
    bool include_entity_ids = true;
    bool include_card_names = true;
    bool include_listeners = true;
    bool pretty = true;
};

struct TraceStep {
    int step_index = 0;
    std::string label;
    std::string action_json;
    std::string result_json;
    std::string legal_surface_json;
    std::string state_json;
    std::string checksum;
};

struct TraceDocument {
    std::string engine = "cpp";
    std::string scenario;
    std::uint64_t seed = 0;
    PlayerId starting_player_id = kPlayerZero;
    std::vector<TraceStep> steps;
};

[[nodiscard]] std::string action_to_json(const Action& action, const SnapshotOptions& options = {});
[[nodiscard]] std::string event_to_json(const EventRecord& event, const SnapshotOptions& options = {});
[[nodiscard]] std::string events_to_json(const std::vector<EventRecord>& events, const SnapshotOptions& options = {});
[[nodiscard]] std::string state_to_json(const GameState& state, const SnapshotOptions& options = {});
[[nodiscard]] std::string trace_to_json(const TraceDocument& trace, const SnapshotOptions& options = {});
[[nodiscard]] std::string checksum_json(std::string_view json);
[[nodiscard]] std::string state_checksum(const GameState& state, const SnapshotOptions& options = {});

}  // namespace gwent::trace
