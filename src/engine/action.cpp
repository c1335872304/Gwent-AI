#include "gwent/engine/action.hpp"

#include <stdexcept>

namespace gwent {

ActionTarget ActionTarget::none() noexcept {
    return ActionTarget{};
}

ActionTarget ActionTarget::row(PlayerId side, Zone row_zone) {
    if (!is_valid_player_id(side)) {
        throw std::out_of_range("row target side must be 0 or 1");
    }
    if (!is_battle_zone(row_zone)) {
        throw std::invalid_argument("row target requires Melee or Ranged");
    }
    ActionTarget target;
    target.kind = ActionTargetKind::Row;
    target.side = side;
    target.zone = row_zone;
    return target;
}

ActionTarget ActionTarget::card(EntityId entity_id) noexcept {
    ActionTarget target;
    target.kind = ActionTargetKind::Card;
    target.entity_id = entity_id;
    return target;
}

ActionTarget ActionTarget::card_definition(CardDefId definition_id) noexcept {
    ActionTarget target;
    target.kind = ActionTargetKind::CardDefinition;
    target.definition_id = definition_id;
    return target;
}

Action Action::pass(PlayerId player_id) noexcept {
    Action action;
    action.type = ActionType::Pass;
    action.player_id = player_id;
    return action;
}

Action Action::end_turn(PlayerId player_id) noexcept {
    Action action;
    action.type = ActionType::EndTurn;
    action.player_id = player_id;
    return action;
}

Action Action::play_card(PlayerId player_id, EntityId hand_card_id) noexcept {
    Action action;
    action.type = ActionType::PlayCard;
    action.player_id = player_id;
    action.source_entity_id = hand_card_id;
    action.target = ActionTarget::none();
    return action;
}

Action Action::play_card(PlayerId player_id, EntityId hand_card_id, PlayerId target_side, Zone target_row, int insert_position) {
    Action action;
    action.type = ActionType::PlayCard;
    action.player_id = player_id;
    action.source_entity_id = hand_card_id;
    action.target = ActionTarget::row(target_side, target_row);
    action.insert_position = insert_position;
    return action;
}

Action Action::discard_card(PlayerId player_id, EntityId hand_card_id) noexcept {
    Action action;
    action.type = ActionType::DiscardCard;
    action.player_id = player_id;
    action.source_entity_id = hand_card_id;
    action.target = ActionTarget::none();
    return action;
}

Action Action::mulligan(PlayerId player_id, EntityId hand_card_id) noexcept {
    Action action;
    action.type = ActionType::Mulligan;
    action.player_id = player_id;
    action.source_entity_id = hand_card_id;
    return action;
}

Action Action::keep_hand(PlayerId player_id) noexcept {
    Action action;
    action.type = ActionType::KeepHand;
    action.player_id = player_id;
    return action;
}

Action Action::use_leader(PlayerId player_id, EntityId leader_id, ActionTarget target) noexcept {
    Action action;
    action.type = ActionType::UseLeader;
    action.player_id = player_id;
    action.source_entity_id = leader_id;
    action.target = target;
    return action;
}

Action Action::use_order(PlayerId player_id, EntityId source_id, ActionTarget target) noexcept {
    Action action;
    action.type = ActionType::UseOrder;
    action.player_id = player_id;
    action.source_entity_id = source_id;
    action.target = target;
    return action;
}

Action Action::choose_card_target(PlayerId player_id, EntityId source_id, EntityId target_id) noexcept {
    Action action;
    action.type = ActionType::ChooseCardTarget;
    action.player_id = player_id;
    action.source_entity_id = source_id;
    action.target = ActionTarget::card(target_id);
    return action;
}

Action Action::choose_card_definition(PlayerId player_id, EntityId source_id, CardDefId definition_id) noexcept {
    Action action;
    action.type = ActionType::ChooseCardTarget;
    action.player_id = player_id;
    action.source_entity_id = source_id;
    action.target = ActionTarget::card_definition(definition_id);
    return action;
}

Action Action::choose_row_target(PlayerId player_id, EntityId source_id, PlayerId side, Zone zone, int insert_position) {
    Action action;
    action.type = ActionType::ChooseRowTarget;
    action.player_id = player_id;
    action.source_entity_id = source_id;
    action.target = ActionTarget::row(side, zone);
    action.insert_position = insert_position;
    return action;
}

Action Action::choose_insert_position(PlayerId player_id, EntityId source_id, PlayerId side, Zone zone, int insert_position) {
    Action action = choose_row_target(player_id, source_id, side, zone, insert_position);
    action.type = ActionType::ChooseInsertPosition;
    return action;
}

std::string_view to_string(ActionType type) noexcept {
    switch (type) {
        case ActionType::Pass: return "PASS";
        case ActionType::EndTurn: return "END_TURN";
        case ActionType::PlayCard: return "PLAY_CARD";
        case ActionType::DiscardCard: return "DISCARD_CARD";
        case ActionType::Mulligan: return "MULLIGAN";
        case ActionType::KeepHand: return "KEEP_HAND";
        case ActionType::UseLeader: return "USE_LEADER";
        case ActionType::UseOrder: return "USE_ORDER";
        case ActionType::ChooseCardTarget: return "CHOOSE_CARD_TARGET";
        case ActionType::ChooseRowTarget: return "CHOOSE_ROW_TARGET";
        case ActionType::ChooseInsertPosition: return "CHOOSE_INSERT_POSITION";
    }
    return "UNKNOWN";
}

std::string_view to_string(ActionTargetKind kind) noexcept {
    switch (kind) {
        case ActionTargetKind::None: return "NONE";
        case ActionTargetKind::Row: return "ROW";
        case ActionTargetKind::Card: return "CARD";
        case ActionTargetKind::CardDefinition: return "CARD_DEFINITION";
    }
    return "UNKNOWN";
}

}  // namespace gwent
