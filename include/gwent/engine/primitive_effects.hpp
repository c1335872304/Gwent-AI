#pragma once

#include <optional>
#include <string>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/enums.hpp"
#include "gwent/engine/effect_registry.hpp"
#include "gwent/engine/targets.hpp"

namespace gwent {

enum class PrimitiveEffectType : std::uint8_t {
    Boost,
    Damage,
    Destroy,
    Move,
    Spawn,
};

struct PrimitiveEffectSpec {
    PrimitiveEffectType type = PrimitiveEffectType::Boost;
    TargetSelector targets{};
    int amount = 0;
    std::string reason;

    // Used by Move. The side and row are resolved at runtime from the EffectCall.
    RowDestinationSpec move_destination{};

    // Used by Spawn.
    std::optional<CardDefinition> spawn_definition;
    RowDestinationSpec spawn_destination{};

    [[nodiscard]] static PrimitiveEffectSpec boost(TargetSelector targets, int amount);
    [[nodiscard]] static PrimitiveEffectSpec damage(TargetSelector targets, int amount);
    [[nodiscard]] static PrimitiveEffectSpec destroy(TargetSelector targets, std::string reason = {});
    [[nodiscard]] static PrimitiveEffectSpec move(TargetSelector targets, RowDestinationSpec destination);
    [[nodiscard]] static PrimitiveEffectSpec spawn(CardDefinition definition, RowDestinationSpec destination);
};

void enqueue_primitive_effect(
    KernelContext& context,
    const EffectCall& call,
    const PrimitiveEffectSpec& spec
);

[[nodiscard]] EffectHandler make_primitive_effect_handler(PrimitiveEffectSpec spec);

void register_primitive_effect(
    EffectRegistry& registry,
    std::string effect_id,
    PrimitiveEffectSpec spec
);

}  // namespace gwent
