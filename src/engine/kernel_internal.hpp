#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gwent/engine/kernel.hpp"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/engine/primitives.hpp"
#include "gwent/engine/row_effects.hpp"

namespace gwent::kernel_detail {

[[nodiscard]] KernelResult reject(Action action, ActionStatus status, std::string message);
[[nodiscard]] bool should_validate_before(InvariantPolicy policy) noexcept;
[[nodiscard]] bool should_validate_after(InvariantPolicy policy) noexcept;
[[nodiscard]] KernelResult reject_invariant_violation(Action action, std::string_view phase, const InvariantReport& report);
[[nodiscard]] bool apply_post_action_invariant_policy(KernelResult& result, const GameState& state, const KernelConfig& config);

void emit(
    std::vector<EventRecord>& events,
    TaskType task_type,
    std::string name,
    PlayerId actor_id,
    EntityId source_id = kInvalidEntityId,
    EntityId target_id = kInvalidEntityId,
    int amount = 0,
    std::string message = {}
);

[[nodiscard]] KernelResult validate_action(const GameState& state, const Action& action);
void enqueue_root_action(TaskQueue& queue, const Action& action);

void dispatch_trigger(
    KernelContext& context,
    const EffectRegistry& effects,
    TaskType task_type,
    std::string_view event_name,
    PlayerId actor_id,
    EntityId event_source_id = kInvalidEntityId,
    EntityId event_target_id = kInvalidEntityId,
    int amount = 0,
    std::string reason = {}
);

void resume_trigger_dispatch(
    KernelContext& context,
    const EffectRegistry& effects
);

void invoke_effects_for_card(
    KernelContext& context,
    const EffectRegistry& effects,
    TaskType task_type,
    const std::string& event_prefix,
    PlayerId actor_id,
    EntityId source_id,
    ActionTarget target
);

[[nodiscard]] int metadata_int(const CardDefinition& definition, std::string_view key, int default_value = 0);
[[nodiscard]] bool has_post_play_pending_end(const GameState& state, PlayerId player_id);
void mark_post_play_pending_end(GameState& state, PlayerId player_id);
void clear_post_play_pending_end(GameState& state, PlayerId player_id);
void mark_no_pass_after_intra_turn_action(GameState& state, PlayerId player_id);
void clear_no_pass_after_intra_turn_action(GameState& state, PlayerId player_id);
[[nodiscard]] bool source_is_leader_or_order(const GameState& state, EntityId source_id);
[[nodiscard]] bool has_pending_order_consumption(const GameState& state, PlayerId player_id);
void enqueue_pending_order_consumption(TaskQueue& queue, const GameState& state, PlayerId player_id);

void tick_turn_counters(KernelContext& context, PlayerId player_id);
void tick_turn_statuses(KernelContext& context, PlayerId player_id);
void dispatch_turn_start_row_effects(KernelContext& context, PlayerId trigger_player_id);
void cleanup_board_after_round(KernelContext& context);
void prepare_next_round_from_state(KernelContext& context, std::optional<PlayerId> winner_id);
void finish_round_or_advance_turn(
    KernelContext& context,
    KernelResult& result,
    const EffectRegistry& effects,
    TaskQueue& queue,
    PlayerId actor_id
);
void append_next_decision(GameState& state, KernelResult& result);

void process_task(
    GameState& state,
    TaskQueue& queue,
    KernelResult& result,
    const EffectRegistry& effects,
    const KernelConfig& config,
    const std::shared_ptr<ResolutionFrame>& resolution_frame,
    const Task& task
);

}  // namespace gwent::kernel_detail
