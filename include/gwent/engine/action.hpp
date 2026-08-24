#pragma once

#include <string_view>

#include "gwent/core/enums.hpp"
#include "gwent/core/ids.hpp"

namespace gwent {

enum class ActionType : std::uint8_t {
    Pass,
    EndTurn,
    PlayCard,
    DiscardCard,
    Mulligan,
    KeepHand,
    UseLeader,
    UseOrder,
    ChooseCardTarget,
    ChooseRowTarget,
    ChooseInsertPosition,
};

enum class ActionTargetKind : std::uint8_t {
    None,
    Row,
    Card,
    CardDefinition,
};

struct ActionTarget {
    ActionTargetKind kind = ActionTargetKind::None;
    PlayerId side = kPlayerZero;
    Zone zone = Zone::Melee;
    EntityId entity_id = kInvalidEntityId;
    CardDefId definition_id = kInvalidCardDefId;

    [[nodiscard]] static ActionTarget none() noexcept;
    [[nodiscard]] static ActionTarget row(PlayerId side, Zone row_zone);
    [[nodiscard]] static ActionTarget card(EntityId entity_id) noexcept;
    [[nodiscard]] static ActionTarget card_definition(CardDefId definition_id) noexcept;

    friend bool operator==(const ActionTarget&, const ActionTarget&) = default;
};

struct Action {
    static constexpr int kNoInsertPosition = -1;

    ActionType type = ActionType::Pass;
    PlayerId player_id = kPlayerZero;

    // For PlayCard/Mulligan this is the hand card. For UseLeader/UseOrder this
    // is the board/leader source. Pass leaves it as kInvalidEntityId.
    EntityId source_entity_id = kInvalidEntityId;
    ActionTarget target = ActionTarget::none();
    // Dynamic index into the target row sequence at execution time. Only a
    // fully specified unit PlayCard and CHOOSE_INSERT_POSITION use this value;
    // the preceding CHOOSE_ROW and every unrelated action carry the sentinel.
    int insert_position = kNoInsertPosition;

    [[nodiscard]] static Action pass(PlayerId player_id) noexcept;
    [[nodiscard]] static Action end_turn(PlayerId player_id) noexcept;
    [[nodiscard]] static Action play_card(PlayerId player_id, EntityId hand_card_id) noexcept;
    [[nodiscard]] static Action discard_card(PlayerId player_id, EntityId hand_card_id) noexcept;
    [[nodiscard]] static Action play_card(PlayerId player_id, EntityId hand_card_id, PlayerId target_side, Zone target_row, int insert_position);
    [[nodiscard]] static Action mulligan(PlayerId player_id, EntityId hand_card_id) noexcept;
    [[nodiscard]] static Action keep_hand(PlayerId player_id) noexcept;
    [[nodiscard]] static Action use_leader(PlayerId player_id, EntityId leader_id, ActionTarget target = ActionTarget::none()) noexcept;
    [[nodiscard]] static Action use_order(PlayerId player_id, EntityId source_id, ActionTarget target = ActionTarget::none()) noexcept;
    [[nodiscard]] static Action choose_card_target(PlayerId player_id, EntityId source_id, EntityId target_id) noexcept;
    [[nodiscard]] static Action choose_card_definition(PlayerId player_id, EntityId source_id, CardDefId definition_id) noexcept;
    [[nodiscard]] static Action choose_row_target(PlayerId player_id, EntityId source_id, PlayerId side, Zone zone, int insert_position = kNoInsertPosition);
    [[nodiscard]] static Action choose_insert_position(PlayerId player_id, EntityId source_id, PlayerId side, Zone zone, int insert_position);

    [[nodiscard]] bool is_pass() const noexcept { return type == ActionType::Pass; }
    [[nodiscard]] bool is_end_turn() const noexcept { return type == ActionType::EndTurn; }
    [[nodiscard]] bool is_play_card() const noexcept { return type == ActionType::PlayCard; }
    [[nodiscard]] bool is_discard_card() const noexcept { return type == ActionType::DiscardCard; }
    [[nodiscard]] bool is_mulligan() const noexcept { return type == ActionType::Mulligan; }
    [[nodiscard]] bool is_keep_hand() const noexcept { return type == ActionType::KeepHand; }
    [[nodiscard]] bool is_use_leader() const noexcept { return type == ActionType::UseLeader; }
    [[nodiscard]] bool is_use_order() const noexcept { return type == ActionType::UseOrder; }
    [[nodiscard]] bool is_choose_card_target() const noexcept { return type == ActionType::ChooseCardTarget; }
    [[nodiscard]] bool is_choose_row_target() const noexcept { return type == ActionType::ChooseRowTarget; }
    [[nodiscard]] bool is_choose_insert_position() const noexcept { return type == ActionType::ChooseInsertPosition; }

    friend bool operator==(const Action&, const Action&) = default;
};

std::string_view to_string(ActionType type) noexcept;
std::string_view to_string(ActionTargetKind kind) noexcept;

}  // namespace gwent
