#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "gwent/core/card_catalog.hpp"
#include "gwent/core/ids.hpp"
#include "gwent/core/location.hpp"
#include "gwent/core/player_state.hpp"
#include "gwent/core/runtime_card.hpp"
#include "gwent/engine/task.hpp"

namespace gwent {

struct ResolutionFrame;

enum class PendingChoiceKind : std::uint8_t {
    None,
    CardTarget,
    CardDefinitionChoice,
    RowTarget,
    InsertPosition,
};

// How a pending choice resumes.  Effect choices reinvoke the suspended effect;
// structural choices complete a partially specified player action (currently
// a PlayCard whose source has been chosen but row has not).
enum class PendingChoiceResumeKind : std::uint8_t {
    InvokeEffect,
    PlayCard,
};

// Structured continuation steps for effect chains that pause on a player
// choice.  A PendingChoice owns the remaining chain, so nested choices can
// carry the same continuation forward without a collection of ad-hoc booleans.
enum class ContinuationStepKind : std::uint8_t {
    ConsumeOrderSource,
    FinishSpecialCard,
    AdvanceTurnOrFinishRound,
    MarkIntraTurnFollowup,
    PlayStagedCard,
};

struct ContinuationStep {
    ContinuationStepKind kind = ContinuationStepKind::AdvanceTurnOrFinishRound;
    PlayerId actor_id = kPlayerZero;
    EntityId source_entity_id = kInvalidEntityId;
};

struct PendingChoice {
    PendingChoiceKind kind = PendingChoiceKind::None;
    PendingChoiceResumeKind resume_kind = PendingChoiceResumeKind::InvokeEffect;
    int choice_id = 0;
    PlayerId player_id = kPlayerZero;
    EntityId source_entity_id = kInvalidEntityId;
    std::string effect_id;
    std::string prompt;

    // Root/action-prefix information for the sequential RL decision grammar.
    // This is deliberately separate from `kind`: kind describes what must be
    // selected now, while origin_action_type describes the action prefix that
    // led to this choice (PlayCard / UseOrder / UseLeader when known).
    std::optional<ActionType> origin_action_type;

    std::vector<EntityId> legal_card_targets;
    std::vector<CardDefId> legal_card_definition_ids;
    std::vector<Location> legal_row_targets;

    // Remaining effect/action-chain work after the choice resolves.  This is
    // copied into any nested choice requested while resolving this one.
    std::vector<ContinuationStep> continuation;

    // Full FIFO tail of the action/effect queue that was still pending when
    // this choice interrupted resolution.  Keeping the actual tasks makes a
    // choice a true suspension point: queued primitive effects, triggers, and
    // outer action-chain work are resumed instead of being silently dropped.
    std::vector<Task> suspended_tasks;

    // Shared state for the complete root resolution chain. This carries the
    // trigger resume stack, ordered decision prefix, budgets and root rollback
    // snapshot across any number of staged choices.
    std::shared_ptr<ResolutionFrame> resolution_frame;
};

enum class RuntimeListenerOrigin : std::uint8_t {
    Normal,
    Infusion,
};

struct RuntimeListener {
    ListenerId listener_id = 0;
    std::string event;
    std::string handler;
    std::optional<EntityId> source_id;
    bool once = true;
    std::unordered_map<std::string, std::string> filters;
    std::unordered_map<std::string, std::string> data;
    bool require_source = true;
    bool source_must_be_on_board = true;

    // Normal card-text listeners are disabled while their source is locked.
    // System/rules listeners may opt out explicitly.
    bool disabled_by_lock = true;
    RuntimeListenerOrigin origin = RuntimeListenerOrigin::Normal;
};

// Per-player state for the current action window.  This is the single
// authoritative source of turn-local legality state.
struct TurnContext {
    bool used_intra_turn_action = false;  // leader/order used this action window
    bool consumed_hand_card = false;     // played or discarded the one card required this turn
    bool played_card_this_turn = false;
    bool discarded_card_this_turn = false;
    // Rows on the opponent's half that received Frost during this player's
    // current turn. Indexing follows row_index(Zone::Melee/Ranged).
    std::array<bool, 2> opponent_rows_frosted_this_turn{};
};

struct GameState {
    std::array<PlayerState, kPlayerCount> players{PlayerState{0}, PlayerState{1}};

    // Static definitions are shared across GameState snapshots. RuntimeCard
    // stores only a compact CardDefId and a cached non-owning pointer.
    std::shared_ptr<CardCatalog> card_catalog = std::make_shared<CardCatalog>();
    // EntityId values are monotonic and never reused within a match, so runtime
    // entities use dense storage. Index == EntityId removes hash lookups from
    // the hottest core paths and makes state snapshots cache-friendly.
    std::vector<RuntimeCard> cards;
    std::vector<std::optional<Location>> locations;

    MatchStatus status = MatchStatus::NotStarted;
    MatchPhase phase = MatchPhase::NotStarted;
    EnginePhase engine_phase = EnginePhase::NotStarted;
    int round_no = 0;
    int turn_no = 0;
    PlayerId current_player_id = kPlayerZero;
    PlayerId starting_player_id = kPlayerZero;
    PlayerId round_starting_player_id = kPlayerZero;

    // 当前正在进行换牌决策的玩家。
    // 有值表示牌局仍处于 round-start Mulligan 决策窗口；为空才进入正常回合动作。
    // 这里保留在 GameState 中，确保规则核、RL facade、trace/replay 看到同一份事实。
    std::optional<PlayerId> mulligan_player_id;

    std::optional<PlayerId> winner_id;
    EntityId next_entity_id = 0;
    std::uint64_t rng_state = 0;

    std::optional<PendingChoice> pending_choice;
    int next_pending_choice_id = 1;

    std::unordered_map<ListenerId, RuntimeListener> listeners;
    ListenerId next_listener_id = 0;
    int next_row_effect_serial = 0;
    std::array<TurnContext, kPlayerCount> turn_contexts{};
    std::array<std::unordered_map<std::string, std::string>, kPlayerCount> turn_rule_modifiers{};
    std::unordered_map<std::string, std::string> match_config;

    [[nodiscard]] PlayerState& player(PlayerId player_id);
    [[nodiscard]] const PlayerState& player(PlayerId player_id) const;
    [[nodiscard]] PlayerId opponent_id(PlayerId player_id) const;

    [[nodiscard]] RuntimeCard* find_card(EntityId entity_id) noexcept;
    [[nodiscard]] const RuntimeCard* find_card(EntityId entity_id) const noexcept;
    [[nodiscard]] std::optional<Location> location_of(EntityId entity_id) const;

    // Creates a runtime entity and places it into the target zone.
    // This is intentionally small: it is a data-model helper, not the future engine primitive.
    EntityId add_card(const CardDefinition& definition, PlayerId owner_id, Location location);
    EntityId add_card(CardDefId definition_id, PlayerId owner_id, Location location);

    // Relocates an entity while keeping all zone indices normalized.
    void move_card(EntityId entity_id, Location destination);

    // Rewrites stored Location::index values after direct vector operations such
    // as deterministic deck shuffling during match setup. Normal gameplay code
    // should still prefer move_card/engine primitives.
    void normalize_zone_indices(PlayerId side, Zone zone);

    [[nodiscard]] int row_score(PlayerId side, Zone row_zone) const;
    [[nodiscard]] int board_score(PlayerId side) const;

private:
    [[nodiscard]] std::vector<EntityId>& mutable_zone(PlayerId side, Zone zone);
    [[nodiscard]] const std::vector<EntityId>& zone(PlayerId side, Zone zone) const;
    void remove_from_current_zone(EntityId entity_id);
    void insert_into_zone(EntityId entity_id, Location destination);
    void reindex(PlayerId side, Zone zone);
};

using CoreState = GameState;

}  // namespace gwent
