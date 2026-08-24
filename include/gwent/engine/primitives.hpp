#pragma once

#include <optional>
#include <string>

#include "gwent/core/state.hpp"
#include "gwent/engine/task.hpp"

namespace gwent {

// Low-level, deterministic state mutations used by Kernel task handlers. These
// helpers intentionally do not decide whether an action is legal; legality is
// checked before tasks are enqueued.
bool primitive_move_card(GameState& state, EntityId entity_id, Location destination);
bool primitive_transform_card(GameState& state, EntityId entity_id, CardDefId definition_id);
bool primitive_reset_card(GameState& state, EntityId entity_id);
EntityId primitive_spawn_card(GameState& state, const CardDefinition& definition, PlayerId owner_id, Location destination);
EntityId primitive_spawn_card(GameState& state, CardDefId definition_id, PlayerId owner_id, Location destination);
bool primitive_boost_card(GameState& state, EntityId entity_id, int amount);
bool primitive_set_power(GameState& state, EntityId entity_id, int power);
bool primitive_damage_card(GameState& state, EntityId entity_id, int amount);
bool primitive_damage_card_ignoring_armor(GameState& state, EntityId entity_id, int amount);
bool primitive_destroy_card(GameState& state, EntityId entity_id, Zone destination = Zone::Cemetery);
bool primitive_summon_card(GameState& state, EntityId entity_id, Location destination);
bool primitive_banish_card(GameState& state, EntityId entity_id);
bool primitive_discard_card(GameState& state, EntityId entity_id);
int primitive_consume_card(GameState& state, EntityId source_entity_id, EntityId target_entity_id);
int primitive_drain_card(GameState& state, EntityId source_entity_id, EntityId target_entity_id, int amount);
bool primitive_add_armor(GameState& state, EntityId entity_id, int amount);
bool primitive_add_status(GameState& state, EntityId entity_id, CardStatus status, int amount);
bool primitive_purify_card(GameState& state, EntityId entity_id);
[[nodiscard]] int infusion_count(const GameState& state, EntityId entity_id);
[[nodiscard]] std::optional<int> primitive_modify_card_counter(
    GameState& state,
    EntityId entity_id,
    CardCounterField field,
    int amount,
    CardCounterMode mode = CardCounterMode::Add
);

}  // namespace gwent
