#include "gwent/engine/legal_actions.hpp"

#include "gwent/engine/query.hpp"
#include "gwent/engine/board_rules.hpp"
#include "gwent/engine/targets.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace gwent {
namespace {

void require_player_id(PlayerId player_id, const char* name) {
    if (!is_valid_player_id(player_id)) {
        throw std::out_of_range(std::string{name} + " must be 0 or 1");
    }
}


bool card_in_zone_for_player(const GameState& state, PlayerId player_id, EntityId entity_id, Zone zone) {
    const auto location = state.location_of(entity_id);
    return location.has_value() && location->side == player_id && location->zone == zone;
}


bool metadata_zone_allowed(const RuntimeCard& card, std::string_view key, Zone zone) {
    const auto it = card.definition->metadata.find(std::string(key));
    if (it == card.definition->metadata.end() || it->second.empty()) {
        return true;
    }

    std::string token;
    auto flush = [&]() -> bool {
        if (token == "melee" || token == "MELEE" || token == "Melee") {
            return zone == Zone::Melee;
        }
        if (token == "ranged" || token == "RANGED" || token == "Ranged") {
            return zone == Zone::Ranged;
        }
        return false;
    };

    for (char ch : it->second) {
        if (ch == ',' || ch == ';' || ch == '|' || ch == ' ') {
            if (!token.empty() && flush()) {
                return true;
            }
            token.clear();
        } else {
            token.push_back(ch);
        }
    }
    return !token.empty() && flush();
}

bool metadata_bool(const RuntimeCard& card, std::string_view key, bool default_value = false) {
    const auto it = card.definition->metadata.find(std::string(key));
    if (it == card.definition->metadata.end()) {
        return default_value;
    }
    return it->second == "true" || it->second == "1" || it->second == "yes" || it->second == "on";
}

bool is_runtime_order_card(const RuntimeCard& card) {
    if (card.runtime.order_used || card.runtime.order_pending_consume) {
        return false;
    }
    if (card.state.cooldown > 0 || card.state.timer > 0) {
        return false;
    }
    if (card.definition->card_type == CardType::Stratagem) {
        return true;
    }
    if (card.state.order_charges > 0) {
        return true;
    }
    if (card.definition->has_effect("order")) {
        return true;
    }
    for (const std::string& effect_id : card.definition->effect_ids) {
        if (effect_id.rfind("order:", 0) == 0) {
            return true;
        }
    }
    const auto it = card.definition->metadata.find("order");
    if (it != card.definition->metadata.end()) {
        return it->second == "true" || it->second == "1" || it->second == "yes" || it->second == "on";
    }
    return false;
}

void append_row_targets(
    std::vector<ActionTarget>& out,
    PlayerId side,
    bool melee,
    bool ranged
) {
    if (melee) {
        out.push_back(ActionTarget::row(side, Zone::Melee));
    }
    if (ranged) {
        out.push_back(ActionTarget::row(side, Zone::Ranged));
    }
}

std::optional<TargetSelector> legacy_requires_target_selector(const RuntimeCard& card) {
    const auto it = card.definition->metadata.find("requires_target");
    if (it == card.definition->metadata.end() || it->second.empty()) {
        return std::nullopt;
    }
    return target_selector_from_metadata_value(it->second);
}

std::optional<TargetSelector> leader_target_selector(const RuntimeCard& card) {
    if (auto selector = metadata_card_target_selector(card, "leader_target_selector")) {
        return selector;
    }
    return legacy_requires_target_selector(card);
}

std::optional<TargetSelector> order_target_selector(const RuntimeCard& card) {
    return metadata_card_target_selector(card, "order_target_selector");
}

std::vector<ActionTarget> legal_targets_for_optional_selector(
    const GameState& state,
    PlayerId actor_id,
    EntityId source_id,
    const std::optional<TargetSelector>& selector,
    PerformanceCounters* performance_counters
) {
    if (!selector.has_value()) {
        return {ActionTarget::none()};
    }

    const TargetContext context{actor_id, source_id, ActionTarget::none(), performance_counters};
    std::vector<ActionTarget> targets = select_action_card_targets(state, context, selector.value());
    if (!targets.empty()) {
        // Target-query helpers still expose the concrete legal target set for
        // diagnostics/rules.  The player-facing action surface itself is
        // factorized below and exposes only UseOrder/UseLeader(source).
        targets.insert(targets.begin(), ActionTarget::none());
    }
    return targets;
}

bool action_target_is_valid_for_optional_selector(
    const GameState& state,
    PlayerId actor_id,
    EntityId source_id,
    ActionTarget target,
    const std::optional<TargetSelector>& selector,
    PerformanceCounters* performance_counters
) {
    if (!selector.has_value()) {
        return target.kind == ActionTargetKind::None;
    }
    const TargetContext context{actor_id, source_id, target, performance_counters};
    if (target.kind == ActionTargetKind::None) {
        return !select_card_targets(state, context, selector.value()).empty();
    }
    return action_target_matches_card_selector(state, context, selector.value());
}

}  // namespace

std::vector<ActionTarget> legal_row_targets_for_card(
    const GameState& state,
    PlayerId player_id,
    const RuntimeCard& card
) {
    require_player_id(player_id, "player_id");
    const PlayerId enemy_id = state.opponent_id(player_id);

    std::vector<ActionTarget> targets;
    if (card.definition->card_type == CardType::Special) {
        targets.push_back(ActionTarget::none());
        return targets;
    }
    switch (card.definition->use_info) {
        case CardUseInfo::MyPlace:
        case CardUseInfo::MyRow:
            append_row_targets(targets, player_id, true, true);
            break;
        case CardUseInfo::EnemyPlace:
        case CardUseInfo::EnemyRow:
            append_row_targets(targets, enemy_id, true, true);
            break;
        case CardUseInfo::AnyPlace:
        case CardUseInfo::AnyRow:
            append_row_targets(targets, player_id, true, true);
            append_row_targets(targets, enemy_id, true, true);
            break;
    }
    targets.erase(
        std::remove_if(targets.begin(), targets.end(), [&](const ActionTarget& target) {
            return target.kind == ActionTargetKind::Row
                && !row_has_space(state, target.side, target.zone);
        }),
        targets.end()
    );

    // Important: row-condition metadata such as deploy_rows describes where a
    // deploy branch is allowed to trigger; it must not restrict where the unit
    // may be placed. A unit may be played to any legal own/enemy row according
    // to CardUseInfo, and the deploy handler decides whether the row-specific
    // text resolves. Order row metadata is different and is still enforced by
    // can_use_order_source(...).
    (void)card;
    return targets;
}

bool can_mulligan_card(const GameState& state, PlayerId player_id, EntityId hand_card_id) {
    require_player_id(player_id, "player_id");
    const auto& player = state.player(player_id);
    return state.status == MatchStatus::Running
        && state.phase == MatchPhase::Playing
        && state.mulligan_player_id.has_value()
        && *state.mulligan_player_id == player_id
        && player.mulligans_available > 0
        && !player.deck.empty()
        && card_in_zone_for_player(state, player_id, hand_card_id, Zone::Hand);
}

bool can_play_card_from_hand(const GameState& state, PlayerId player_id, EntityId hand_card_id) {
    require_player_id(player_id, "player_id");
    if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing) {
        return false;
    }
    const auto& player = state.player(player_id);
    if (player.passed) {
        return false;
    }
    if (!card_in_zone_for_player(state, player_id, hand_card_id, Zone::Hand)) {
        return false;
    }

    const RuntimeCard* card = state.find_card(hand_card_id);
    if (card == nullptr) {
        return false;
    }

    // Leaders are not hand cards. Stratagems are board command entities and
    // should be offered as UseOrder, never as PlayCard.
    if (card->definition->card_type == CardType::Leader || card->definition->card_type == CardType::Stratagem) {
        return false;
    }
    return true;
}

bool can_use_leader(const GameState& state, PlayerId player_id) {
    require_player_id(player_id, "player_id");
    if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing) {
        return false;
    }
    const auto& player = state.player(player_id);
    if (player.passed || !player.leader.has_value()) {
        return false;
    }
    const RuntimeCard* leader = state.find_card(player.leader.value());
    if (leader == nullptr || leader->definition->card_type != CardType::Leader) {
        return false;
    }
    const auto selector = leader_target_selector(*leader);
    if (selector.has_value()) {
        const TargetContext context{player_id, player.leader.value(), ActionTarget::none()};
        if (select_card_targets(state, context, selector.value()).empty()) {
            return false;
        }
    }
    if (leader->state.order_charges > 0) {
        return true;
    }
    return !player.leader_used;
}

bool can_use_order_source(const GameState& state, PlayerId player_id, EntityId source_id) {
    require_player_id(player_id, "player_id");
    if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing) {
        return false;
    }
    const auto& player = state.player(player_id);
    if (player.passed) {
        return false;
    }

    const RuntimeCard* card = state.find_card(source_id);
    if (card == nullptr || card->controller_id != player_id || card->state.locked) {
        return false;
    }

    const auto location = state.location_of(source_id);
    if (!location.has_value() || !is_battle_zone(location->zone)) {
        return false;
    }
    if (!metadata_zone_allowed(*card, "order_rows", location->zone)) {
        return false;
    }
    if (metadata_bool(*card, "order_requires_dominance") && !has_dominance(state, player_id)) {
        return false;
    }

    // Orders from units that entered this turn require Zeal.  Stratagems are
    // command entities and leaders are handled separately.
    if (card->definition->card_type != CardType::Stratagem
        && card->runtime.entered_this_turn
        && !metadata_bool(*card, "zeal")) {
        return false;
    }

    return is_runtime_order_card(*card);
}

std::vector<ActionTarget> legal_leader_targets(
    const GameState& state,
    PlayerId player_id,
    EntityId leader_id
) {
    require_player_id(player_id, "player_id");
    const RuntimeCard* leader = state.find_card(leader_id);
    if (leader == nullptr || leader->definition->card_type != CardType::Leader) {
        return {};
    }
    return legal_targets_for_optional_selector(state, player_id, leader_id, leader_target_selector(*leader), nullptr);
}

std::vector<ActionTarget> legal_order_targets(
    const GameState& state,
    PlayerId player_id,
    EntityId source_id
) {
    require_player_id(player_id, "player_id");
    const RuntimeCard* source = state.find_card(source_id);
    if (source == nullptr) {
        return {};
    }
    return legal_targets_for_optional_selector(state, player_id, source_id, order_target_selector(*source), nullptr);
}

bool leader_action_target_is_valid(
    const GameState& state,
    PlayerId player_id,
    EntityId leader_id,
    ActionTarget target
) {
    require_player_id(player_id, "player_id");
    const RuntimeCard* leader = state.find_card(leader_id);
    if (leader == nullptr || leader->definition->card_type != CardType::Leader) {
        return false;
    }
    return action_target_is_valid_for_optional_selector(state, player_id, leader_id, target, leader_target_selector(*leader), nullptr);
}

bool order_action_target_is_valid(
    const GameState& state,
    PlayerId player_id,
    EntityId source_id,
    ActionTarget target
) {
    require_player_id(player_id, "player_id");
    const RuntimeCard* source = state.find_card(source_id);
    if (source == nullptr) {
        return false;
    }
    return action_target_is_valid_for_optional_selector(state, player_id, source_id, target, order_target_selector(*source), nullptr);
}

namespace {

std::vector<ActionTarget> legal_leader_targets_with_metrics(
    const GameState& state,
    PlayerId player_id,
    EntityId leader_id,
    PerformanceCounters* performance_counters
) {
    const RuntimeCard* leader = state.find_card(leader_id);
    if (leader == nullptr || leader->definition->card_type != CardType::Leader) {
        return {};
    }
    return legal_targets_for_optional_selector(state, player_id, leader_id, leader_target_selector(*leader), performance_counters);
}

std::vector<ActionTarget> legal_order_targets_with_metrics(
    const GameState& state,
    PlayerId player_id,
    EntityId source_id,
    PerformanceCounters* performance_counters
) {
    const RuntimeCard* source = state.find_card(source_id);
    if (source == nullptr) {
        return {};
    }
    return legal_targets_for_optional_selector(state, player_id, source_id, order_target_selector(*source), performance_counters);
}

std::vector<Action> legal_leader_actions_with_metrics(
    const GameState& state,
    PlayerId player_id,
    PerformanceCounters* performance_counters
) {
    if (!can_use_leader(state, player_id)) {
        return {};
    }

    std::vector<Action> actions;
    const EntityId leader_id = state.player(player_id).leader.value();
    const auto targets = legal_leader_targets_with_metrics(state, player_id, leader_id, performance_counters);
    if (!targets.empty()) {
        // Sequential grammar: choose the leader source now; choose its required
        // target in the following PendingChoice.
        actions.push_back(Action::use_leader(player_id, leader_id));
    }
    return actions;
}

std::vector<Action> legal_order_actions_with_metrics(
    const GameState& state,
    PlayerId player_id,
    PerformanceCounters* performance_counters
) {
    std::vector<Action> actions;
    for (const EntityId entity_id : query_battlefield_entities(state, player_id)) {
        if (!can_use_order_source(state, player_id, entity_id)) {
            continue;
        }
        const auto targets = legal_order_targets_with_metrics(state, player_id, entity_id, performance_counters);
        if (!targets.empty()) {
            // Sequential grammar: one option per order source. Required targets
            // are selected in the next decision instead of source x target.
            actions.push_back(Action::use_order(player_id, entity_id));
        }
    }
    return actions;
}

bool has_deferred_order_to_close_with_metrics(
    const GameState& state,
    PlayerId player_id
) {
    for (const EntityId entity_id : query_battlefield_entities(state, player_id)) {
        const RuntimeCard* card = state.find_card(entity_id);
        if (card == nullptr || card->controller_id != player_id) {
            continue;
        }
        if (card->runtime.order_pending_consume) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<Action> legal_mulligan_actions(const GameState& state, PlayerId player_id) {
    require_player_id(player_id, "player_id");
    std::vector<Action> actions;
    if (state.status != MatchStatus::Running
        || state.phase != MatchPhase::Playing
        || !state.mulligan_player_id.has_value()
        || *state.mulligan_player_id != player_id) {
        return actions;
    }

    const auto& player = state.player(player_id);
    // KEEP_HAND 永远存在：玩家可以少换，也能在牌库为空/次数为 0 时确定性结束阶段。
    actions.reserve(player.hand.size() + 1);
    actions.push_back(Action::keep_hand(player_id));
    if (player.mulligans_available <= 0 || player.deck.empty()) {
        return actions;
    }
    for (const EntityId entity_id : player.hand) {
        if (can_mulligan_card(state, player_id, entity_id)) {
            actions.push_back(Action::mulligan(player_id, entity_id));
        }
    }
    return actions;
}

std::vector<Action> legal_play_card_actions(const GameState& state, PlayerId player_id) {
    require_player_id(player_id, "player_id");
    std::vector<Action> actions;
    const auto& player = state.player(player_id);
    actions.reserve(player.hand.size());
    for (const EntityId entity_id : player.hand) {
        if (!can_play_card_from_hand(state, player_id, entity_id)) {
            continue;
        }
        const RuntimeCard* card = state.find_card(entity_id);
        if (card == nullptr) {
            continue;
        }
        if (card->definition->card_type != CardType::Special
            && legal_row_targets_for_card(state, player_id, *card).empty()) {
            continue;
        }

        // Q-Transformer/sequential decision surface: choose the hand card first.
        // A non-special card then opens a RowTarget PendingChoice; we do not
        // flatten card x row into duplicate PlayCard candidates.
        actions.push_back(Action::play_card(player_id, entity_id));
    }
    return actions;
}

std::vector<Action> legal_discard_card_actions(const GameState& state, PlayerId player_id) {
    require_player_id(player_id, "player_id");
    std::vector<Action> actions;
    if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing || state.player(player_id).passed) {
        return actions;
    }
    for (const EntityId entity_id : state.player(player_id).hand) {
        if (card_in_zone_for_player(state, player_id, entity_id, Zone::Hand)) {
            const RuntimeCard* card = state.find_card(entity_id);
            if (card != nullptr && card->definition->card_type != CardType::Leader && card->definition->card_type != CardType::Stratagem) {
                actions.push_back(Action::discard_card(player_id, entity_id));
            }
        }
    }
    return actions;
}

std::vector<Action> legal_leader_actions(const GameState& state, PlayerId player_id) {
    require_player_id(player_id, "player_id");
    return legal_leader_actions_with_metrics(state, player_id, nullptr);
}

std::vector<Action> legal_order_actions(const GameState& state, PlayerId player_id) {
    require_player_id(player_id, "player_id");
    return legal_order_actions_with_metrics(state, player_id, nullptr);
}

bool has_deferred_order_to_close(const GameState& state, PlayerId player_id) {
    return has_deferred_order_to_close_with_metrics(state, player_id);
}

bool has_post_play_pending_end(const GameState& state, PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        return false;
    }
    return state.turn_contexts[static_cast<std::size_t>(player_id)].consumed_hand_card;
}

bool has_no_pass_after_intra_turn_action(const GameState& state, PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        return false;
    }
    return state.turn_contexts[static_cast<std::size_t>(player_id)].used_intra_turn_action;
}

std::vector<Action> legal_turn_actions(
    const GameState& state,
    PlayerId player_id,
    const LegalActionConfig& config
) {
    PerformanceScope scope(config.performance_counters, PerformanceMetric::LegalActions);
    require_player_id(player_id, "player_id");
    std::vector<Action> actions;

    if (state.pending_choice.has_value() || state.mulligan_player_id.has_value()) {
        return actions;
    }
    if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing || state.player(player_id).passed) {
        return actions;
    }

    const bool has_deferred_order = has_deferred_order_to_close_with_metrics(state, player_id);
    const bool hand_consumed_this_turn = has_post_play_pending_end(state, player_id);
    const bool can_close_turn = hand_consumed_this_turn || has_deferred_order;
    const bool no_pass_after_intra_turn_action = has_no_pass_after_intra_turn_action(state, player_id);

    if (can_close_turn) {
        actions.push_back(Action::end_turn(player_id));
    }

    // A normal turn may contain any number of leader/order actions, but it must
    // consume exactly one hand card (play or discard) before EndTurn is legal.
    // Once a hand card has been consumed, no second play/discard is generated;
    // remaining legal actions are only leader/order follow-ups plus EndTurn.
    if (!hand_consumed_this_turn && config.include_play_card) {
        auto play_actions = legal_play_card_actions(state, player_id);
        actions.insert(actions.end(), play_actions.begin(), play_actions.end());
    }
    if (!hand_consumed_this_turn && config.include_discard_card) {
        auto discard_actions = legal_discard_card_actions(state, player_id);
        actions.insert(actions.end(), discard_actions.begin(), discard_actions.end());
    }
    // Before the mandatory hand card has been consumed, a leader/order action
    // is legal only if at least one hand card still exists.  Otherwise that
    // action would create an impossible state: PASS/EndTurn are forbidden after
    // the intra-turn action, but no card could be played or discarded.
    const bool can_satisfy_hand_consumption = hand_consumed_this_turn || !state.player(player_id).hand.empty();
    if (config.include_leader && can_satisfy_hand_consumption) {
        auto leader_actions = legal_leader_actions_with_metrics(state, player_id, config.performance_counters);
        actions.insert(actions.end(), leader_actions.begin(), leader_actions.end());
    }
    if (config.include_order && can_satisfy_hand_consumption) {
        auto order_actions = legal_order_actions_with_metrics(state, player_id, config.performance_counters);
        actions.insert(actions.end(), order_actions.begin(), order_actions.end());
    }

    // Pass means abandoning the current round.  Once a non-turn-consuming
    // leader/order action was used, PASS stays forbidden until a hand card is
    // consumed.  There is deliberately no "actions.empty()" escape hatch: the
    // engine prevents entering that dead-end state instead of weakening rules.
    if (!can_close_turn
        && config.include_pass
        && !no_pass_after_intra_turn_action) {
        actions.insert(actions.begin(), Action::pass(player_id));
    }
    return actions;
}


std::vector<Action> legal_pending_choice_actions(const GameState& state) {
    std::vector<Action> actions;
    if (!state.pending_choice.has_value()) {
        return actions;
    }

    const PendingChoice& choice = state.pending_choice.value();
    if (choice.kind == PendingChoiceKind::CardTarget) {
        actions.reserve(choice.legal_card_targets.size());
        for (const EntityId target_id : choice.legal_card_targets) {
            if (state.find_card(target_id) != nullptr) {
                actions.push_back(Action::choose_card_target(choice.player_id, choice.source_entity_id, target_id));
            }
        }
        return actions;
    }
    if (choice.kind == PendingChoiceKind::CardDefinitionChoice) {
        actions.reserve(choice.legal_card_definition_ids.size());
        for (CardDefId definition_id : choice.legal_card_definition_ids) {
            actions.push_back(Action::choose_card_definition(choice.player_id, choice.source_entity_id, definition_id));
        }
        return actions;
    }
    if (choice.kind == PendingChoiceKind::RowTarget) {
        actions.reserve(choice.legal_row_targets.size());
        for (const Location& row : choice.legal_row_targets) {
            actions.push_back(Action::choose_row_target(
                choice.player_id,
                choice.source_entity_id,
                row.side,
                row.zone,
                Action::kNoInsertPosition
            ));
        }
    }
    if (choice.kind == PendingChoiceKind::InsertPosition) {
        actions.reserve(choice.legal_row_targets.size());
        for (const Location& row : choice.legal_row_targets) {
            actions.push_back(Action::choose_insert_position(
                choice.player_id,
                choice.source_entity_id,
                row.side,
                row.zone,
                static_cast<int>(row.index)
            ));
        }
    }
    return actions;
}

Decision make_pending_choice_decision(const GameState& state) {
    Decision decision;
    if (!state.pending_choice.has_value()) {
        return decision;
    }
    decision.type = DecisionType::ChooseTarget;
    decision.player_id = state.pending_choice->player_id;
    decision.legal_actions = legal_pending_choice_actions(state);
    return decision;
}

bool is_legal_action(
    const GameState& state,
    const Action& action,
    const LegalActionConfig& config
) {
    if (!is_valid_player_id(action.player_id)) {
        return false;
    }

    auto contains = [&](const std::vector<Action>& actions) {
        return std::find(actions.begin(), actions.end(), action) != actions.end();
    };

    switch (action.type) {
        case ActionType::ChooseCardTarget:
        case ActionType::ChooseRowTarget:
        case ActionType::ChooseInsertPosition:
            return contains(legal_pending_choice_actions(state));
        case ActionType::Mulligan:
        case ActionType::KeepHand:
            return contains(legal_mulligan_actions(state, action.player_id));
        case ActionType::Pass:
        case ActionType::EndTurn:
        case ActionType::PlayCard:
        case ActionType::DiscardCard:
        case ActionType::UseLeader:
        case ActionType::UseOrder:
            return contains(legal_turn_actions(state, action.player_id, config));
    }
    return false;
}

bool is_executable_action(
    const GameState& state,
    const Action& action,
    const LegalActionConfig& config
) {
    if (is_legal_action(state, action, config)) {
        return true;
    }
    if (!is_valid_player_id(action.player_id) || state.pending_choice.has_value()) {
        return false;
    }

    if (action.type == ActionType::PlayCard && action.target.kind == ActionTargetKind::Row) {
        const Action prefix = Action::play_card(action.player_id, action.source_entity_id);
        if (!is_legal_action(state, prefix, config)) {
            return false;
        }
        const RuntimeCard* card = state.find_card(action.source_entity_id);
        if (card == nullptr || card->definition->card_type == CardType::Special) {
            return false;
        }
        const auto rows = legal_row_targets_for_card(state, action.player_id, *card);
        if (std::find(rows.begin(), rows.end(), action.target) == rows.end()) {
            return false;
        }
        const auto& row = state.player(action.target.side).row(action.target.zone);
        return action.insert_position >= 0
            && static_cast<std::size_t>(action.insert_position) <= row.size();
    }

    if (action.type == ActionType::UseLeader && action.target.kind != ActionTargetKind::None) {
        const Action prefix = Action::use_leader(action.player_id, action.source_entity_id);
        return is_legal_action(state, prefix, config)
            && leader_action_target_is_valid(state, action.player_id, action.source_entity_id, action.target);
    }

    if (action.type == ActionType::UseOrder && action.target.kind != ActionTargetKind::None) {
        const Action prefix = Action::use_order(action.player_id, action.source_entity_id);
        return is_legal_action(state, prefix, config)
            && order_action_target_is_valid(state, action.player_id, action.source_entity_id, action.target);
    }
    return false;
}

Decision make_mulligan_decision(const GameState& state, PlayerId player_id) {
    require_player_id(player_id, "player_id");
    Decision decision;
    decision.type = DecisionType::Mulligan;
    decision.player_id = player_id;
    decision.legal_actions = legal_mulligan_actions(state, player_id);
    return decision;
}

Decision make_turn_decision(
    const GameState& state,
    PlayerId player_id,
    const LegalActionConfig& config
) {
    require_player_id(player_id, "player_id");
    Decision decision;
    decision.type = DecisionType::TurnAction;
    decision.player_id = player_id;
    decision.legal_actions = legal_turn_actions(state, player_id, config);
    return decision;
}

Decision make_current_turn_decision(
    const GameState& state,
    const LegalActionConfig& config
) {
    PerformanceScope scope(config.performance_counters, PerformanceMetric::CurrentDecision);
    if (state.pending_choice.has_value()) {
        return make_pending_choice_decision(state);
    }
    if (state.mulligan_player_id.has_value()) {
        PerformanceScope legal_scope(config.performance_counters, PerformanceMetric::LegalActions);
        return make_mulligan_decision(state, *state.mulligan_player_id);
    }
    return make_turn_decision(state, state.current_player_id, config);
}

}  // namespace gwent
