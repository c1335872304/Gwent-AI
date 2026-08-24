#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gwent/core/performance.hpp"
#include "gwent/core/string_hash.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/task_queue.hpp"
#include "gwent/engine/resolution_frame.hpp"

namespace gwent {

struct KernelContext {
    GameState& state;
    TaskQueue& queue;
    std::vector<EventRecord>& events;
    PerformanceCounters* performance_counters = nullptr;
    std::shared_ptr<ResolutionFrame> resolution_frame;

    // Structured remainder of the current effect chain.  Effect handlers may
    // replace/clear/extend this before requesting a choice.  PendingChoice then
    // stores it and hands it to the next resolution step, including nested
    // choices.
    std::vector<ContinuationStep> continuation;

    // When an effect requests a choice, carry the root action that produced
    // that choice into PendingChoice so the Q-Transformer prefix represents
    // the already-selected action rather than the next target token.
    std::optional<ActionType> choice_origin_action_type;

    void emit(EventRecord event);
    void enqueue(Task task);
    void enqueue_spawn_card(PlayerId actor_id, CardDefinition definition, Location destination, EntityId source_entity_id = kInvalidEntityId);
    void set_continuation(std::vector<ContinuationStep> steps);
    void clear_continuation() noexcept;
    void append_continuation(ContinuationStep step);

    ListenerId add_listener(RuntimeListener listener);
    ListenerId add_listener(
        std::string event,
        std::string handler,
        std::optional<EntityId> source_id = std::nullopt,
        bool once = false,
        std::unordered_map<std::string, std::string> filters = {},
        std::unordered_map<std::string, std::string> data = {},
        bool require_source = true,
        bool source_must_be_on_board = true,
        bool disabled_by_lock = true
    );
    bool remove_listener(ListenerId listener_id);

    void request_card_choice(PlayerId player_id, EntityId source_entity_id, std::string_view effect_id, std::string prompt, std::vector<EntityId> legal_card_targets);
    void request_card_definition_choice(PlayerId player_id, EntityId source_entity_id, std::string_view effect_id, std::string prompt, std::vector<CardDefId> legal_definition_ids);
    void request_row_choice(PlayerId player_id, EntityId source_entity_id, std::string_view effect_id, std::string prompt, std::vector<Location> legal_row_targets);
    void request_play_row_choice(PlayerId player_id, EntityId source_entity_id, std::string prompt, std::vector<Location> legal_row_targets);

    // Python-parity FastRng helpers. They mutate GameState::rng_state and are
    // used by effects whose official behavior is random among legal candidates.
    [[nodiscard]] std::size_t random_index(std::size_t size);
    [[nodiscard]] std::optional<Location> random_open_battle_row(PlayerId player_id);
};

struct EffectCall {
    // Effect calls are synchronous. IDs and trigger context therefore borrow
    // stable storage from CardCatalog / ResolutionFrame / PendingChoice instead
    // of allocating temporary strings for every invocation.
    std::string_view effect_id;
    PlayerId actor_id = kPlayerZero;
    EntityId source_entity_id = kInvalidEntityId;
    ActionTarget target = ActionTarget::none();

    // Filled for listener/trigger dispatch. For normal deploy/order/leader
    // calls these stay empty/default.
    std::string_view event_name;
    EventKind event_kind = EventKind::Unknown;
    std::optional<ListenerId> listener_id;
    EntityId event_source_entity_id = kInvalidEntityId;
    EntityId event_target_entity_id = kInvalidEntityId;
    int amount = 0;
    std::string_view reason;
    const std::unordered_map<std::string, std::string>* data = nullptr;
};

using EffectHandler = std::function<void(KernelContext&, const EffectCall&)>;

class EffectRegistry {
public:
    void register_handler(std::string effect_id, EffectHandler handler);
    [[nodiscard]] bool contains(std::string_view effect_id) const;
    [[nodiscard]] bool invoke(KernelContext& context, const EffectCall& call) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::string, EffectHandler, TransparentStringHash, std::equal_to<>> handlers_;
};

}  // namespace gwent
