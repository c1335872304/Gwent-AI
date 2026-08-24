#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/location.hpp"
#include "gwent/engine/action.hpp"

namespace gwent {

enum class EventKind : std::uint16_t {
    Unknown = 0,
    CardPlayed,
    CardDamaged,
    CardDrained,
    CardDestroyed,
    CardBanished,
    CardBoosted,
    ArmorAdded,
    StatusAdded,
    TurnStart,
    TurnEnd,
    RoundStart,
    RoundFinished,
    TimerTicked,
    CooldownTicked,
    BleedingTicked,
    VitalityTicked,
    BleedingVitalityOffset,
    RuptureTicked,
    ListenerRegistered,
    ListenerRemoved,
    InvariantViolation,
};

enum class CardCounterField : std::uint8_t {
    Cooldown,
    Countdown,
    Timer,
    Bleeding,
    Vitality,
};

enum class CardCounterMode : std::uint8_t {
    Add,
    Set,
};

enum class TaskType : std::uint8_t {
    Pass,
    EndTurn,
    Mulligan,
    KeepHand,
    PlayCard,
    ResolveDeploy,
    ResolveSpecial,
    FinishSpecialCard,
    UseLeader,
    UseOrder,
    ResolveOrder,
    ConsumeOrderSource,
    MarkIntraTurnFollowup,
    AdvanceTurnOrFinishRound,
    ApplyTurnTransition,
    FinalizeRoundEnd,
    DispatchTurnStartRowEffects,
    DispatchTurnStart,
    MoveCard,
    SpawnCard,
    BoostCard,
    SetPower,
    DamageCard,
    DestroyCard,
    SummonCard,
    BanishCard,
    DiscardCard,
    ConsumeCard,
    DrainCard,
    AddArmorCard,
    AddStatusCard,
    PurifyCard,
    ModifyCardCounter,
    ResolvePendingCardChoice,
    TickTurnCounters,
    TickTurnStatuses,
    DispatchTurnEnd,
    RoundCleanup,
    DispatchRoundStart,
};

struct Task {
    TaskType type = TaskType::Pass;
    PlayerId actor_id = kPlayerZero;
    EntityId source_entity_id = kInvalidEntityId;
    EntityId target_entity_id = kInvalidEntityId;
    ActionTarget target = ActionTarget::none();
    int insert_position = Action::kNoInsertPosition;
    std::optional<Location> destination;
    CardDefId card_definition_id = kInvalidCardDefId;
    std::optional<PlayerId> round_winner_id;
    int amount = 0;
    CardCounterField counter_field = CardCounterField::Cooldown;
    CardCounterMode counter_mode = CardCounterMode::Add;
    TaskType event_task_type = TaskType::Pass;
    std::string reason;

    [[nodiscard]] static Task pass(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task end_turn(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task mulligan(PlayerId actor_id, EntityId hand_card_id) noexcept;
    [[nodiscard]] static Task keep_hand(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task play_card(PlayerId actor_id, EntityId hand_card_id, ActionTarget row_target, int insert_position = Action::kNoInsertPosition);
    [[nodiscard]] static Task resolve_deploy(PlayerId actor_id, EntityId source_id, ActionTarget original_target = ActionTarget::none()) noexcept;
    [[nodiscard]] static Task resolve_special(PlayerId actor_id, EntityId source_id, ActionTarget original_target = ActionTarget::none()) noexcept;
    [[nodiscard]] static Task finish_special_card(PlayerId actor_id, EntityId source_id) noexcept;
    [[nodiscard]] static Task use_leader(PlayerId actor_id, EntityId leader_id, ActionTarget target = ActionTarget::none()) noexcept;
    [[nodiscard]] static Task use_order(PlayerId actor_id, EntityId source_id, ActionTarget target = ActionTarget::none()) noexcept;
    [[nodiscard]] static Task resolve_order(PlayerId actor_id, EntityId source_id, ActionTarget target = ActionTarget::none()) noexcept;
    [[nodiscard]] static Task consume_order_source(PlayerId actor_id, EntityId source_id) noexcept;
    [[nodiscard]] static Task mark_intra_turn_followup(PlayerId actor_id, EntityId source_id = kInvalidEntityId) noexcept;
    [[nodiscard]] static Task advance_turn_or_finish_round(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task apply_turn_transition(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task finalize_round_end(PlayerId actor_id, std::optional<PlayerId> winner_id) noexcept;
    [[nodiscard]] static Task dispatch_turn_start_row_effects(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task dispatch_turn_start(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task move_card(PlayerId actor_id, EntityId target_id, Location destination, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task spawn_card(PlayerId actor_id, CardDefId definition_id, Location destination, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task boost_card(PlayerId actor_id, EntityId target_id, int amount, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task set_power(PlayerId actor_id, EntityId target_id, int power, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task damage_card(PlayerId actor_id, EntityId target_id, int amount, EntityId source_id = kInvalidEntityId, std::string reason = {});
    [[nodiscard]] static Task destroy_card(PlayerId actor_id, EntityId target_id, std::string reason = {}, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task summon_card(PlayerId actor_id, EntityId target_id, Location destination, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task banish_card(PlayerId actor_id, EntityId target_id, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task discard_card(PlayerId actor_id, EntityId target_id, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task consume_card(PlayerId actor_id, EntityId source_id, EntityId target_id);
    [[nodiscard]] static Task drain_card(PlayerId actor_id, EntityId source_id, EntityId target_id, int amount);
    [[nodiscard]] static Task add_armor_card(PlayerId actor_id, EntityId target_id, int amount, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task add_status_card(PlayerId actor_id, EntityId target_id, CardStatus status, int amount, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task purify_card(PlayerId actor_id, EntityId target_id, EntityId source_id = kInvalidEntityId);
    [[nodiscard]] static Task modify_card_counter(
        PlayerId actor_id,
        EntityId target_id,
        CardCounterField field,
        int amount,
        CardCounterMode mode = CardCounterMode::Add,
        EntityId source_id = kInvalidEntityId,
        TaskType event_task_type = TaskType::Pass,
        std::string event_name = {}
    );
    [[nodiscard]] static Task resolve_pending_card_choice(PlayerId actor_id, EntityId source_id, EntityId target_id) noexcept;
    [[nodiscard]] static Task resolve_pending_choice(PlayerId actor_id, EntityId source_id, ActionTarget target, int insert_position = Action::kNoInsertPosition) noexcept;
    [[nodiscard]] static Task tick_turn_counters(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task tick_turn_statuses(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task dispatch_turn_end(PlayerId actor_id) noexcept;
    [[nodiscard]] static Task round_cleanup(PlayerId actor_id, std::optional<PlayerId> winner_id) noexcept;
    [[nodiscard]] static Task dispatch_round_start(PlayerId actor_id) noexcept;
};

struct EventRecord {
    TaskType task_type = TaskType::Pass;
    std::string name;
    PlayerId actor_id = kPlayerZero;
    EntityId source_entity_id = kInvalidEntityId;
    EntityId target_entity_id = kInvalidEntityId;
    int amount = 0;
    std::string message;
    EventKind kind = EventKind::Unknown;

    EventRecord() = default;
    EventRecord(
        TaskType task_type,
        std::string name,
        PlayerId actor_id,
        EntityId source_entity_id = kInvalidEntityId,
        EntityId target_entity_id = kInvalidEntityId,
        int amount = 0,
        std::string message = {}
    );
};

[[nodiscard]] EventKind event_kind_from_name(std::string_view name) noexcept;
std::string_view to_string(EventKind kind) noexcept;
std::string_view to_string(TaskType type) noexcept;

}  // namespace gwent
