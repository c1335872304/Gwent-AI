#include "gwent/engine/primitive_effects.hpp"

#include <utility>

namespace gwent {

PrimitiveEffectSpec PrimitiveEffectSpec::boost(TargetSelector targets, int amount) {
    PrimitiveEffectSpec spec;
    spec.type = PrimitiveEffectType::Boost;
    spec.targets = targets;
    spec.amount = amount;
    return spec;
}

PrimitiveEffectSpec PrimitiveEffectSpec::damage(TargetSelector targets, int amount) {
    PrimitiveEffectSpec spec;
    spec.type = PrimitiveEffectType::Damage;
    spec.targets = targets;
    spec.amount = amount;
    return spec;
}

PrimitiveEffectSpec PrimitiveEffectSpec::destroy(TargetSelector targets, std::string reason) {
    PrimitiveEffectSpec spec;
    spec.type = PrimitiveEffectType::Destroy;
    spec.targets = targets;
    spec.reason = std::move(reason);
    return spec;
}

PrimitiveEffectSpec PrimitiveEffectSpec::move(TargetSelector targets, RowDestinationSpec destination) {
    PrimitiveEffectSpec spec;
    spec.type = PrimitiveEffectType::Move;
    spec.targets = targets;
    spec.move_destination = destination;
    return spec;
}

PrimitiveEffectSpec PrimitiveEffectSpec::spawn(CardDefinition definition, RowDestinationSpec destination) {
    PrimitiveEffectSpec spec;
    spec.type = PrimitiveEffectType::Spawn;
    spec.spawn_definition = std::move(definition);
    spec.spawn_destination = destination;
    return spec;
}

void enqueue_primitive_effect(
    KernelContext& context,
    const EffectCall& call,
    const PrimitiveEffectSpec& spec
) {
    const TargetContext target_context{call.actor_id, call.source_entity_id, call.target};

    switch (spec.type) {
        case PrimitiveEffectType::Boost: {
            for (const EntityId target_id : select_card_targets(context.state, target_context, spec.targets)) {
                context.enqueue(Task::boost_card(call.actor_id, target_id, spec.amount, call.source_entity_id));
            }
            break;
        }
        case PrimitiveEffectType::Damage: {
            for (const EntityId target_id : select_card_targets(context.state, target_context, spec.targets)) {
                context.enqueue(Task::damage_card(call.actor_id, target_id, spec.amount, call.source_entity_id));
            }
            break;
        }
        case PrimitiveEffectType::Destroy: {
            for (const EntityId target_id : select_card_targets(context.state, target_context, spec.targets)) {
                context.enqueue(Task::destroy_card(call.actor_id, target_id, spec.reason, call.source_entity_id));
            }
            break;
        }
        case PrimitiveEffectType::Move: {
            const auto destination = resolve_row_destination(context.state, target_context, spec.move_destination);
            if (!destination.has_value()) {
                context.emit(EventRecord{TaskType::MoveCard, "primitive_move:invalid_destination", call.actor_id, call.source_entity_id, kInvalidEntityId, 0, {}});
                break;
            }
            for (const EntityId target_id : select_card_targets(context.state, target_context, spec.targets)) {
                context.enqueue(Task::move_card(call.actor_id, target_id, destination.value(), call.source_entity_id));
            }
            break;
        }
        case PrimitiveEffectType::Spawn: {
            if (!spec.spawn_definition.has_value()) {
                context.emit(EventRecord{TaskType::SpawnCard, "primitive_spawn:missing_definition", call.actor_id, call.source_entity_id, kInvalidEntityId, 0, {}});
                break;
            }
            const auto destination = resolve_row_destination(context.state, target_context, spec.spawn_destination);
            if (!destination.has_value()) {
                context.emit(EventRecord{TaskType::SpawnCard, "primitive_spawn:invalid_destination", call.actor_id, call.source_entity_id, kInvalidEntityId, 0, {}});
                break;
            }
            context.enqueue_spawn_card(call.actor_id, spec.spawn_definition.value(), destination.value(), call.source_entity_id);
            break;
        }
    }
}

EffectHandler make_primitive_effect_handler(PrimitiveEffectSpec spec) {
    return [spec = std::move(spec)](KernelContext& context, const EffectCall& call) {
        enqueue_primitive_effect(context, call, spec);
    };
}

void register_primitive_effect(
    EffectRegistry& registry,
    std::string effect_id,
    PrimitiveEffectSpec spec
) {
    registry.register_handler(std::move(effect_id), make_primitive_effect_handler(std::move(spec)));
}

}  // namespace gwent
