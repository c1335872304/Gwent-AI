#include "gwent/engine/task.hpp"

#include <stdexcept>
#include <utility>

namespace gwent {

EventKind event_kind_from_name(std::string_view name) noexcept {
    if (name == "card_played") return EventKind::CardPlayed;
    if (name == "card_damaged") return EventKind::CardDamaged;
    if (name == "card_drained") return EventKind::CardDrained;
    if (name == "card_destroyed") return EventKind::CardDestroyed;
    if (name == "card_banished") return EventKind::CardBanished;
    if (name == "card_boosted") return EventKind::CardBoosted;
    if (name == "armor_added") return EventKind::ArmorAdded;
    if (name == "status_added") return EventKind::StatusAdded;
    if (name == "turn_start") return EventKind::TurnStart;
    if (name == "turn_end") return EventKind::TurnEnd;
    if (name == "round_start") return EventKind::RoundStart;
    if (name == "round_finished") return EventKind::RoundFinished;
    if (name == "timer_ticked") return EventKind::TimerTicked;
    if (name == "cooldown_ticked") return EventKind::CooldownTicked;
    if (name == "bleeding_ticked") return EventKind::BleedingTicked;
    if (name == "vitality_ticked") return EventKind::VitalityTicked;
    if (name == "bleeding_vitality_offset") return EventKind::BleedingVitalityOffset;
    if (name == "rupture_ticked") return EventKind::RuptureTicked;
    if (name == "listener_registered") return EventKind::ListenerRegistered;
    if (name == "listener_removed") return EventKind::ListenerRemoved;
    if (name == "invariant_violation") return EventKind::InvariantViolation;
    return EventKind::Unknown;
}

std::string_view to_string(EventKind kind) noexcept {
    switch (kind) {
        case EventKind::Unknown: return "unknown";
        case EventKind::CardPlayed: return "card_played";
        case EventKind::CardDamaged: return "card_damaged";
        case EventKind::CardDrained: return "card_drained";
        case EventKind::CardDestroyed: return "card_destroyed";
        case EventKind::CardBanished: return "card_banished";
        case EventKind::CardBoosted: return "card_boosted";
        case EventKind::ArmorAdded: return "armor_added";
        case EventKind::StatusAdded: return "status_added";
        case EventKind::TurnStart: return "turn_start";
        case EventKind::TurnEnd: return "turn_end";
        case EventKind::RoundStart: return "round_start";
        case EventKind::RoundFinished: return "round_finished";
        case EventKind::TimerTicked: return "timer_ticked";
        case EventKind::CooldownTicked: return "cooldown_ticked";
        case EventKind::BleedingTicked: return "bleeding_ticked";
        case EventKind::VitalityTicked: return "vitality_ticked";
        case EventKind::BleedingVitalityOffset: return "bleeding_vitality_offset";
        case EventKind::RuptureTicked: return "rupture_ticked";
        case EventKind::ListenerRegistered: return "listener_registered";
        case EventKind::ListenerRemoved: return "listener_removed";
        case EventKind::InvariantViolation: return "invariant_violation";
    }
    return "unknown";
}

EventRecord::EventRecord(
    TaskType task_type_value,
    std::string name_value,
    PlayerId actor_id_value,
    EntityId source_entity_id_value,
    EntityId target_entity_id_value,
    int amount_value,
    std::string message_value
)
    : task_type(task_type_value),
      name(std::move(name_value)),
      actor_id(actor_id_value),
      source_entity_id(source_entity_id_value),
      target_entity_id(target_entity_id_value),
      amount(amount_value),
      message(std::move(message_value)),
      kind(event_kind_from_name(name)) {}

Task Task::pass(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::Pass;
    task.actor_id = actor_id;
    return task;
}

Task Task::end_turn(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::EndTurn;
    task.actor_id = actor_id;
    return task;
}

Task Task::mulligan(PlayerId actor_id, EntityId hand_card_id) noexcept {
    Task task;
    task.type = TaskType::Mulligan;
    task.actor_id = actor_id;
    task.source_entity_id = hand_card_id;
    return task;
}

Task Task::keep_hand(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::KeepHand;
    task.actor_id = actor_id;
    return task;
}

Task Task::play_card(PlayerId actor_id, EntityId hand_card_id, ActionTarget row_target, int insert_position) {
    if (row_target.kind != ActionTargetKind::Row && row_target.kind != ActionTargetKind::None) {
        throw std::invalid_argument("PlayCard task requires a battle-row target or no target for a special card");
    }
    if (row_target.kind == ActionTargetKind::Row && !is_battle_zone(row_target.zone)) {
        throw std::invalid_argument("PlayCard row target requires a battle row");
    }
    Task task;
    task.type = TaskType::PlayCard;
    task.actor_id = actor_id;
    task.source_entity_id = hand_card_id;
    task.target = row_target;
    task.insert_position = insert_position;
    return task;
}

Task Task::resolve_deploy(PlayerId actor_id, EntityId source_id, ActionTarget original_target) noexcept {
    Task task;
    task.type = TaskType::ResolveDeploy;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target = original_target;
    return task;
}

Task Task::resolve_special(PlayerId actor_id, EntityId source_id, ActionTarget original_target) noexcept {
    Task task;
    task.type = TaskType::ResolveSpecial;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target = original_target;
    return task;
}

Task Task::finish_special_card(PlayerId actor_id, EntityId source_id) noexcept {
    Task task;
    task.type = TaskType::FinishSpecialCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    return task;
}

Task Task::use_leader(PlayerId actor_id, EntityId leader_id, ActionTarget target) noexcept {
    Task task;
    task.type = TaskType::UseLeader;
    task.actor_id = actor_id;
    task.source_entity_id = leader_id;
    task.target = target;
    return task;
}

Task Task::use_order(PlayerId actor_id, EntityId source_id, ActionTarget target) noexcept {
    Task task;
    task.type = TaskType::UseOrder;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target = target;
    return task;
}

Task Task::resolve_order(PlayerId actor_id, EntityId source_id, ActionTarget target) noexcept {
    Task task;
    task.type = TaskType::ResolveOrder;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target = target;
    return task;
}

Task Task::consume_order_source(PlayerId actor_id, EntityId source_id) noexcept {
    Task task;
    task.type = TaskType::ConsumeOrderSource;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    return task;
}

Task Task::mark_intra_turn_followup(PlayerId actor_id, EntityId source_id) noexcept {
    Task task;
    task.type = TaskType::MarkIntraTurnFollowup;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    return task;
}

Task Task::advance_turn_or_finish_round(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::AdvanceTurnOrFinishRound;
    task.actor_id = actor_id;
    return task;
}

Task Task::apply_turn_transition(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::ApplyTurnTransition;
    task.actor_id = actor_id;
    return task;
}

Task Task::finalize_round_end(PlayerId actor_id, std::optional<PlayerId> winner_id) noexcept {
    Task task;
    task.type = TaskType::FinalizeRoundEnd;
    task.actor_id = actor_id;
    task.round_winner_id = winner_id;
    return task;
}

Task Task::dispatch_turn_start_row_effects(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::DispatchTurnStartRowEffects;
    task.actor_id = actor_id;
    return task;
}

Task Task::dispatch_turn_start(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::DispatchTurnStart;
    task.actor_id = actor_id;
    return task;
}

Task Task::move_card(PlayerId actor_id, EntityId target_id, Location destination, EntityId source_id) {
    Task task;
    task.type = TaskType::MoveCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.destination = destination;
    return task;
}

Task Task::spawn_card(PlayerId actor_id, CardDefId definition_id, Location destination, EntityId source_id) {
    Task task;
    task.type = TaskType::SpawnCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.card_definition_id = definition_id;
    task.destination = destination;
    return task;
}

Task Task::boost_card(PlayerId actor_id, EntityId target_id, int amount, EntityId source_id) {
    Task task;
    task.type = TaskType::BoostCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.amount = amount;
    return task;
}

Task Task::set_power(PlayerId actor_id, EntityId target_id, int power, EntityId source_id) {
    Task task;
    task.type = TaskType::SetPower;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.amount = power;
    return task;
}

Task Task::damage_card(PlayerId actor_id, EntityId target_id, int amount, EntityId source_id, std::string reason) {
    Task task;
    task.type = TaskType::DamageCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.amount = amount;
    task.reason = std::move(reason);
    return task;
}

Task Task::destroy_card(PlayerId actor_id, EntityId target_id, std::string reason, EntityId source_id) {
    Task task;
    task.type = TaskType::DestroyCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.reason = std::move(reason);
    return task;
}

Task Task::summon_card(PlayerId actor_id, EntityId target_id, Location destination, EntityId source_id) {
    Task task;
    task.type = TaskType::SummonCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.destination = destination;
    return task;
}

Task Task::banish_card(PlayerId actor_id, EntityId target_id, EntityId source_id) {
    Task task;
    task.type = TaskType::BanishCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    return task;
}

Task Task::discard_card(PlayerId actor_id, EntityId target_id, EntityId source_id) {
    Task task;
    task.type = TaskType::DiscardCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    return task;
}

Task Task::consume_card(PlayerId actor_id, EntityId source_id, EntityId target_id) {
    Task task;
    task.type = TaskType::ConsumeCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    return task;
}

Task Task::drain_card(PlayerId actor_id, EntityId source_id, EntityId target_id, int amount) {
    Task task;
    task.type = TaskType::DrainCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.amount = amount;
    return task;
}

Task Task::add_armor_card(PlayerId actor_id, EntityId target_id, int amount, EntityId source_id) {
    Task task;
    task.type = TaskType::AddArmorCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.amount = amount;
    return task;
}

Task Task::add_status_card(PlayerId actor_id, EntityId target_id, CardStatus status, int amount, EntityId source_id) {
    Task task;
    task.type = TaskType::AddStatusCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.amount = amount;
    task.reason = std::string(to_string(status));
    return task;
}

Task Task::purify_card(PlayerId actor_id, EntityId target_id, EntityId source_id) {
    Task task;
    task.type = TaskType::PurifyCard;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    return task;
}


Task Task::modify_card_counter(
    PlayerId actor_id,
    EntityId target_id,
    CardCounterField field,
    int amount,
    CardCounterMode mode,
    EntityId source_id,
    TaskType event_task_type,
    std::string event_name
) {
    Task task;
    task.type = TaskType::ModifyCardCounter;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target_entity_id = target_id;
    task.counter_field = field;
    task.counter_mode = mode;
    task.amount = amount;
    task.event_task_type = event_task_type;
    task.reason = std::move(event_name);
    return task;
}

Task Task::resolve_pending_card_choice(PlayerId actor_id, EntityId source_id, EntityId target_id) noexcept {
    return resolve_pending_choice(actor_id, source_id, ActionTarget::card(target_id));
}

Task Task::resolve_pending_choice(PlayerId actor_id, EntityId source_id, ActionTarget target, int insert_position) noexcept {
    Task task;
    task.type = TaskType::ResolvePendingCardChoice;
    task.actor_id = actor_id;
    task.source_entity_id = source_id;
    task.target = target;
    task.insert_position = insert_position;
    if (target.kind == ActionTargetKind::Card) {
        task.target_entity_id = target.entity_id;
    }
    return task;
}

Task Task::tick_turn_counters(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::TickTurnCounters;
    task.actor_id = actor_id;
    return task;
}

Task Task::tick_turn_statuses(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::TickTurnStatuses;
    task.actor_id = actor_id;
    return task;
}

Task Task::dispatch_turn_end(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::DispatchTurnEnd;
    task.actor_id = actor_id;
    return task;
}

Task Task::round_cleanup(PlayerId actor_id, std::optional<PlayerId> winner_id) noexcept {
    Task task;
    task.type = TaskType::RoundCleanup;
    task.actor_id = actor_id;
    task.round_winner_id = winner_id;
    return task;
}

Task Task::dispatch_round_start(PlayerId actor_id) noexcept {
    Task task;
    task.type = TaskType::DispatchRoundStart;
    task.actor_id = actor_id;
    return task;
}

std::string_view to_string(TaskType type) noexcept {
    switch (type) {
        case TaskType::Pass: return "PASS";
        case TaskType::EndTurn: return "END_TURN";
        case TaskType::Mulligan: return "MULLIGAN";
        case TaskType::KeepHand: return "KEEP_HAND";
        case TaskType::PlayCard: return "PLAY_CARD";
        case TaskType::ResolveDeploy: return "RESOLVE_DEPLOY";
        case TaskType::ResolveSpecial: return "RESOLVE_SPECIAL";
        case TaskType::FinishSpecialCard: return "FINISH_SPECIAL_CARD";
        case TaskType::UseLeader: return "USE_LEADER";
        case TaskType::UseOrder: return "USE_ORDER";
        case TaskType::ResolveOrder: return "RESOLVE_ORDER";
        case TaskType::ConsumeOrderSource: return "CONSUME_ORDER_SOURCE";
        case TaskType::MarkIntraTurnFollowup: return "MARK_INTRA_TURN_FOLLOWUP";
        case TaskType::AdvanceTurnOrFinishRound: return "ADVANCE_TURN_OR_FINISH_ROUND";
        case TaskType::ApplyTurnTransition: return "APPLY_TURN_TRANSITION";
        case TaskType::FinalizeRoundEnd: return "FINALIZE_ROUND_END";
        case TaskType::DispatchTurnStartRowEffects: return "DISPATCH_TURN_START_ROW_EFFECTS";
        case TaskType::DispatchTurnStart: return "DISPATCH_TURN_START";
        case TaskType::MoveCard: return "MOVE_CARD";
        case TaskType::SpawnCard: return "SPAWN_CARD";
        case TaskType::BoostCard: return "BOOST_CARD";
        case TaskType::SetPower: return "SET_POWER";
        case TaskType::DamageCard: return "DAMAGE_CARD";
        case TaskType::DestroyCard: return "DESTROY_CARD";
        case TaskType::SummonCard: return "SUMMON_CARD";
        case TaskType::BanishCard: return "BANISH_CARD";
        case TaskType::DiscardCard: return "DISCARD_CARD";
        case TaskType::ConsumeCard: return "CONSUME_CARD";
        case TaskType::DrainCard: return "DRAIN_CARD";
        case TaskType::AddArmorCard: return "ADD_ARMOR_CARD";
        case TaskType::AddStatusCard: return "ADD_STATUS_CARD";
        case TaskType::PurifyCard: return "PURIFY_CARD";
        case TaskType::ModifyCardCounter: return "MODIFY_CARD_COUNTER";
        case TaskType::ResolvePendingCardChoice: return "RESOLVE_PENDING_CARD_CHOICE";
        case TaskType::TickTurnCounters: return "TICK_TURN_COUNTERS";
        case TaskType::TickTurnStatuses: return "TICK_TURN_STATUSES";
        case TaskType::DispatchTurnEnd: return "DISPATCH_TURN_END";
        case TaskType::RoundCleanup: return "ROUND_CLEANUP";
        case TaskType::DispatchRoundStart: return "DISPATCH_ROUND_START";
    }
    return "UNKNOWN";
}

}  // namespace gwent
