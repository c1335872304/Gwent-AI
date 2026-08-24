#include "kernel_internal.hpp"

namespace gwent::kernel_detail {

std::optional<std::string_view> effect_id_for_event(
    std::string_view raw_effect_id,
    std::string_view event_prefix,
    bool allow_unscoped_effect_id
) {
    const auto separator = raw_effect_id.find(':');
    if (separator == std::string::npos) {
        // Unscoped effect ids are a legacy shorthand for the direct card action
        // currently being resolved (deploy / special / leader / order). They
        // must not be treated as definition triggers for unrelated events such
        // as card_played, turn_start, or turn_end; otherwise a deploy handler
        // can accidentally fire multiple times during a single action.
        return allow_unscoped_effect_id ? std::optional<std::string_view>{raw_effect_id} : std::nullopt;
    }

    if (raw_effect_id.substr(0, separator) == event_prefix) {
        return raw_effect_id.substr(separator + 1);
    }
    return std::nullopt;
}
std::string filter_int(int value) {
    return std::to_string(value);
}

std::optional<std::string> filter_value_for_event(
    const GameState& state,
    std::string_view key,
    PlayerId actor_id,
    EntityId event_source_id,
    EntityId event_target_id,
    int amount,
    std::string_view reason
) {
    if (key == "actor_id") return filter_int(actor_id);
    if (key == "source_id") return filter_int(event_source_id);
    if (key == "target_id") return filter_int(event_target_id);
    if (key == "amount") return filter_int(amount);
    if (key == "reason" || key == "status") return std::string(reason);

    const auto describe_card = [&](EntityId entity_id, std::string_view suffix) -> std::optional<std::string> {
        const RuntimeCard* card = state.find_card(entity_id);
        if (card == nullptr) {
            return std::nullopt;
        }
        if (suffix == "owner_id") return filter_int(card->owner_id);
        if (suffix == "controller_id") return filter_int(card->controller_id);
        if (suffix == "card_id") return card->definition->id;
        if (suffix == "card_type") return std::string(to_string(card->definition->card_type));
        return std::nullopt;
    };

    if (key.rfind("source_", 0) == 0) {
        return describe_card(event_source_id, key.substr(7));
    }
    if (key.rfind("target_", 0) == 0) {
        if (key == "target_zone") {
            const auto location = state.location_of(event_target_id);
            return location.has_value() ? std::optional<std::string>{std::string(to_string(location->zone))} : std::nullopt;
        }
        if (key == "target_side") {
            const auto location = state.location_of(event_target_id);
            return location.has_value() ? std::optional<std::string>{filter_int(location->side)} : std::nullopt;
        }
        return describe_card(event_target_id, key.substr(7));
    }

    return std::nullopt;
}

bool listener_source_is_available(const GameState& state, const RuntimeListener& listener) {
    if (!listener.require_source) {
        return true;
    }
    if (!listener.source_id.has_value()) {
        return false;
    }
    const RuntimeCard* source = state.find_card(listener.source_id.value());
    if (source == nullptr) {
        return false;
    }
    if (listener.disabled_by_lock && source->state.locked) {
        return false;
    }
    if (listener.source_must_be_on_board) {
        const auto location = state.location_of(listener.source_id.value());
        return location.has_value() && is_battle_zone(location->zone);
    }
    return true;
}

bool listener_matches_event(
    const GameState& state,
    const RuntimeListener& listener,
    std::string_view event_name,
    PlayerId actor_id,
    EntityId event_source_id,
    EntityId event_target_id,
    int amount,
    std::string_view reason
) {
    if (listener.event != event_name) {
        return false;
    }
    if (!listener_source_is_available(state, listener)) {
        return false;
    }

    for (const auto& [key, expected] : listener.filters) {
        const auto actual = filter_value_for_event(state, key, actor_id, event_source_id, event_target_id, amount, reason);
        if (!actual.has_value() || actual.value() != expected) {
            return false;
        }
    }
    return true;
}

namespace {

bool definition_source_can_trigger(const RuntimeCard* card) {
    // Definition-scoped card text follows the same default lock rule as runtime
    // listeners. Direct deploy/order/leader invocation is handled separately by
    // invoke_effects_for_card() and is not affected by this passive-trigger gate.
    if (card == nullptr || !card->state.locked) {
        return card != nullptr;
    }
    const auto it = card->definition->metadata.find("trigger_ignores_lock");
    return it != card->definition->metadata.end()
        && (it->second == "true" || it->second == "1" || it->second == "yes" || it->second == "on");
}

void emit_runtime_trigger_result(
    KernelContext& context,
    const TriggerDispatchFrame& frame,
    const RuntimeListener& listener,
    bool invoked
) {
    context.emit(EventRecord{
        frame.task_type,
        invoked ? "trigger:effect" : "trigger:unregistered_handler",
        frame.actor_id,
        listener.source_id.value_or(kInvalidEntityId),
        frame.event_target_id,
        frame.amount,
        frame.event_name + ":" + listener.handler
    });
}

void emit_definition_trigger_result(
    KernelContext& context,
    const TriggerDispatchFrame& frame,
    EntityId source_id,
    std::string_view effect_id,
    bool invoked
) {
    context.emit(EventRecord{
        frame.task_type,
        invoked ? "definition_trigger:effect" : "definition_trigger:unregistered_handler",
        frame.actor_id,
        source_id,
        frame.event_target_id,
        frame.amount,
        frame.event_name + ":" + std::string(effect_id)
    });
}

}  // namespace

void resume_trigger_dispatch(
    KernelContext& context,
    const EffectRegistry& effects
) {
    if (!context.resolution_frame) {
        return;
    }

    auto& stack = context.resolution_frame->trigger_stack;
    while (!stack.empty()) {
        TriggerDispatchFrame& frame = stack.back();

        while (frame.next_listener_index < frame.listener_ids.size()) {
            const ListenerId listener_id = frame.listener_ids[frame.next_listener_index++];
            const auto it = context.state.listeners.find(listener_id);
            if (it == context.state.listeners.end()) {
                continue;
            }
            const RuntimeListener listener = it->second;
            if (!listener_matches_event(
                    context.state,
                    listener,
                    frame.event_name,
                    frame.actor_id,
                    frame.event_source_id,
                    frame.event_target_id,
                    frame.amount,
                    frame.reason)) {
                continue;
            }

            consume_trigger_budget(*context.resolution_frame);

            EffectCall call;
            call.effect_id = listener.handler;
            call.actor_id = frame.actor_id;
            call.source_entity_id = listener.source_id.value_or(kInvalidEntityId);
            call.target = frame.event_target_id == kInvalidEntityId
                ? ActionTarget::none()
                : ActionTarget::card(frame.event_target_id);
            call.event_name = frame.event_name;
            call.event_kind = frame.event_kind;
            call.listener_id = listener.listener_id;
            call.event_source_entity_id = frame.event_source_id;
            call.event_target_entity_id = frame.event_target_id;
            call.amount = frame.amount;
            call.reason = frame.reason;
            call.data = &listener.data;

            const bool invoked = effects.invoke(context, call);
            emit_runtime_trigger_result(context, frame, listener, invoked);

            if (listener.once) {
                context.state.listeners.erase(listener_id);
                context.emit(EventRecord{
                    frame.task_type,
                    "listener_removed",
                    frame.actor_id,
                    call.source_entity_id,
                    kInvalidEntityId,
                    listener_id,
                    frame.event_name
                });
            }

            if (context.state.pending_choice.has_value()) {
                return;
            }
        }

        while (frame.next_definition_index < frame.definition_trigger_sources.size()) {
            const EntityId entity_id = frame.definition_trigger_sources[frame.next_definition_index];
            const RuntimeCard* card = context.state.find_card(entity_id);
            if (!definition_source_can_trigger(card)) {
                ++frame.next_definition_index;
                frame.next_definition_effect_index = 0;
                continue;
            }

            const auto& effect_ids = card->definition->effect_ids;
            bool advanced_source = true;
            while (frame.next_definition_effect_index < effect_ids.size()) {
                const std::string& raw_effect_id = effect_ids[frame.next_definition_effect_index++];
                const auto resolved_effect_id = effect_id_for_event(raw_effect_id, frame.event_name, false);
                if (!resolved_effect_id.has_value()) {
                    continue;
                }

                consume_trigger_budget(*context.resolution_frame);

                EffectCall call;
                call.effect_id = resolved_effect_id.value();
                call.actor_id = frame.actor_id;
                call.source_entity_id = entity_id;
                call.target = frame.event_target_id == kInvalidEntityId
                    ? ActionTarget::none()
                    : ActionTarget::card(frame.event_target_id);
                call.event_name = frame.event_name;
                call.event_kind = frame.event_kind;
                call.event_source_entity_id = frame.event_source_id;
                call.event_target_entity_id = frame.event_target_id;
                call.amount = frame.amount;
                call.reason = frame.reason;

                const bool invoked = effects.invoke(context, call);
                emit_definition_trigger_result(context, frame, entity_id, call.effect_id, invoked);

                if (context.state.pending_choice.has_value()) {
                    advanced_source = false;
                    return;
                }
            }

            if (advanced_source) {
                ++frame.next_definition_index;
                frame.next_definition_effect_index = 0;
            }
        }

        stack.pop_back();
    }
}

void dispatch_trigger(
    KernelContext& context,
    const EffectRegistry& effects,
    TaskType task_type,
    std::string_view event_name,
    PlayerId actor_id,
    EntityId event_source_id,
    EntityId event_target_id,
    int amount,
    std::string reason
) {
    // A normal kernel action always supplies a frame. Keeping the null fallback
    // as a no-op protects ad-hoc low-level callers from dereferencing null; such
    // callers should execute through apply_action_with_kernel for real rules.
    if (!context.resolution_frame) {
        return;
    }

    TriggerDispatchFrame frame;
    frame.task_type = task_type;
    frame.event_kind = event_kind_from_name(event_name);
    frame.event_name = std::string(event_name);
    frame.actor_id = actor_id;
    frame.event_source_id = event_source_id;
    frame.event_target_id = event_target_id;
    frame.amount = amount;
    frame.reason = std::move(reason);

    frame.listener_ids.reserve(context.state.listeners.size());
    for (const auto& [listener_id, listener] : context.state.listeners) {
        if (listener_matches_event(
                context.state,
                listener,
                frame.event_name,
                actor_id,
                event_source_id,
                event_target_id,
                amount,
                frame.reason)) {
            frame.listener_ids.push_back(listener_id);
        }
    }
    std::sort(frame.listener_ids.begin(), frame.listener_ids.end());

    frame.definition_trigger_sources.reserve(context.state.cards.size());
    for (std::size_t i = 0; i < context.state.cards.size(); ++i) {
        frame.definition_trigger_sources.push_back(static_cast<EntityId>(i));
    }

    context.resolution_frame->trigger_stack.push_back(std::move(frame));
    observe_resolution_depth(
        *context.resolution_frame,
        context.resolution_frame->trigger_stack.size());
    resume_trigger_dispatch(context, effects);
}

void invoke_effects_for_card(
    KernelContext& context,
    const EffectRegistry& effects,
    TaskType task_type,
    const std::string& event_prefix,
    PlayerId actor_id,
    EntityId source_id,
    ActionTarget target
) {
    const RuntimeCard* card = context.state.find_card(source_id);
    if (card == nullptr) {
        context.emit(EventRecord{task_type, event_prefix + ":missing_source", actor_id, source_id, kInvalidEntityId, 0, {}});
        return;
    }

    if (card->definition->effect_ids.empty()) {
        context.emit(EventRecord{task_type, event_prefix + ":no_effects", actor_id, source_id, kInvalidEntityId, 0, {}});
        return;
    }

    bool matched_any = false;
    for (const std::string& raw_effect_id : card->definition->effect_ids) {
        const auto resolved_effect_id = effect_id_for_event(raw_effect_id, event_prefix, true);
        if (!resolved_effect_id.has_value()) {
            continue;
        }
        matched_any = true;

        EffectCall call;
        call.effect_id = resolved_effect_id.value();
        call.actor_id = actor_id;
        call.source_entity_id = source_id;
        call.target = target;

        if (effects.invoke(context, call)) {
            context.emit(EventRecord{task_type, event_prefix + ":effect", actor_id, source_id, kInvalidEntityId, 0, std::string(call.effect_id)});
        } else {
            context.emit(EventRecord{task_type, event_prefix + ":unregistered_effect", actor_id, source_id, kInvalidEntityId, 0, std::string(call.effect_id)});
        }
    }

    if (!matched_any) {
        context.emit(EventRecord{task_type, event_prefix + ":no_matching_effects", actor_id, source_id, kInvalidEntityId, 0, {}});
    }
}

}  // namespace gwent::kernel_detail
