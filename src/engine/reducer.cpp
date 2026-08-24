#define GWENT_SUPPRESS_LEGACY_REDUCER_DEPRECATION
#include "gwent/engine/reducer.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "gwent/engine/legal_actions.hpp"

namespace gwent {
namespace {

ActionResult reject(Action action, ActionStatus status, std::string message) {
    ActionResult result;
    result.action = action;
    result.status = status;
    result.applied = false;
    result.message = std::move(message);
    return result;
}

bool is_current_turn_action(ActionType type) noexcept {
    return type == ActionType::Pass
        || type == ActionType::EndTurn
        || type == ActionType::PlayCard
        || type == ActionType::DiscardCard
        || type == ActionType::UseLeader
        || type == ActionType::UseOrder;
}

void finish_mulligan_for_player(GameState& state, PlayerId actor_id) {
    PlayerState& actor = state.player(actor_id);
    actor.mulligans_available = 0;
    if (!state.mulligan_player_id.has_value() || *state.mulligan_player_id != actor_id) {
        return;
    }
    if (actor_id == state.round_starting_player_id) {
        state.mulligan_player_id = state.opponent_id(actor_id);
    } else {
        state.mulligan_player_id.reset();
    }
}

void append_next_decision(GameState& state, ActionResult& result) {
    if (state.status == MatchStatus::Running && state.phase == MatchPhase::Playing) {
        result.next_decision = make_current_turn_decision(state);
    }
}

void switch_to_next_player_or_finish_round(GameState& state, ActionResult& result) {
    const PlayerId current = state.current_player_id;
    const PlayerId opponent = state.opponent_id(current);

    if (state.player(kPlayerZero).passed && state.player(kPlayerOne).passed) {
        const int score0 = state.board_score(kPlayerZero);
        const int score1 = state.board_score(kPlayerOne);
        if (score0 > score1) {
            state.player(kPlayerZero).round_wins += 1;
            result.events.push_back("round_finished:winner=0");
        } else if (score1 > score0) {
            state.player(kPlayerOne).round_wins += 1;
            result.events.push_back("round_finished:winner=1");
        } else {
            state.player(kPlayerZero).round_wins += 1;
            state.player(kPlayerOne).round_wins += 1;
            result.events.push_back("round_finished:winner=tie");
        }

        const bool p0_match = state.player(kPlayerZero).round_wins >= 2;
        const bool p1_match = state.player(kPlayerOne).round_wins >= 2;
        if (p0_match || p1_match) {
            state.status = MatchStatus::Finished;
            state.phase = MatchPhase::Finished;
            state.engine_phase = EnginePhase::Finished;
            if (p0_match != p1_match) {
                state.winner_id = p0_match ? kPlayerZero : kPlayerOne;
            } else {
                state.winner_id = std::nullopt;
            }
            result.events.push_back("match_finished");
        } else {
            // Round cleanup and next-round preparation will be a later milestone.
            state.phase = MatchPhase::Finished;
            state.engine_phase = EnginePhase::Finished;
        }
        return;
    }

    state.turn_contexts[static_cast<std::size_t>(current)] = {};
    state.current_player_id = opponent;
    state.turn_no += 1;
    state.engine_phase = EnginePhase::ActionWindow;
    result.events.push_back(state.player(opponent).passed ? "turn_advanced_to_passed_player" : "turn_advanced");
}

ActionResult apply_pass(GameState& state, const Action& action) {
    if (!is_legal_action(state, action)) {
        return reject(action, ActionStatus::IllegalAction, "pass is not legal in the current state");
    }

    ActionResult result;
    result.action = action;
    result.status = ActionStatus::Applied;
    result.applied = true;

    state.player(action.player_id).passed = true;
    state.player(action.player_id).pass_reason = PassReason::Explicit;
    result.events.push_back("pass");
    switch_to_next_player_or_finish_round(state, result);
    append_next_decision(state, result);
    return result;
}

ActionResult apply_play_card(GameState& state, const Action& action) {
    if (!is_executable_action(state, action)) {
        return reject(action, ActionStatus::IllegalAction, "play-card action is not executable in the current state");
    }
    if (action.target.kind != ActionTargetKind::Row || !is_battle_zone(action.target.zone)) {
        return reject(action, ActionStatus::InvalidTarget, "play-card action requires a battle-row target");
    }
    const auto& target_row = state.player(action.target.side).row(action.target.zone);
    if (action.insert_position < 0
        || static_cast<std::size_t>(action.insert_position) > target_row.size()) {
        return reject(action, ActionStatus::InvalidTarget, "play-card insert_position is outside the current row sequence");
    }

    RuntimeCard* card = state.find_card(action.source_entity_id);
    if (card == nullptr) {
        return reject(action, ActionStatus::InvalidSource, "play-card source does not exist");
    }

    ActionResult result;
    result.action = action;
    result.status = ActionStatus::Applied;
    result.applied = true;

    card->state.deploying = true;
    state.move_card(action.source_entity_id, Location{
        action.target.side,
        action.target.zone,
        static_cast<std::size_t>(action.insert_position)
    });
    card = state.find_card(action.source_entity_id);
    if (card != nullptr) {
        card->controller_id = action.target.side;
        card->state.deploying = false;
    }
    result.events.push_back("card_played:no_effects");
    state.turn_contexts[static_cast<std::size_t>(action.player_id)].consumed_hand_card = true;
    state.turn_contexts[static_cast<std::size_t>(action.player_id)].played_card_this_turn = true;
    card->runtime.entered_this_turn = true;
    append_next_decision(state, result);
    return result;
}

ActionResult apply_discard_card(GameState& state, const Action& action) {
    if (!is_legal_action(state, action)) {
        return reject(action, ActionStatus::IllegalAction, "discard-card action is not legal in the current state");
    }
    RuntimeCard* card = state.find_card(action.source_entity_id);
    if (card == nullptr) {
        return reject(action, ActionStatus::InvalidSource, "discard-card source does not exist");
    }
    ActionResult result;
    result.action = action;
    result.status = ActionStatus::Applied;
    result.applied = true;
    state.move_card(action.source_entity_id, Location{card->owner_id, Zone::Cemetery, state.player(card->owner_id).cemetery.size()});
    result.events.push_back("card_discarded:no_effects");
    state.turn_contexts[static_cast<std::size_t>(action.player_id)].consumed_hand_card = true;
    state.turn_contexts[static_cast<std::size_t>(action.player_id)].discarded_card_this_turn = true;
    append_next_decision(state, result);
    return result;
}

ActionResult apply_mulligan(GameState& state, const Action& action, const ReducerConfig& config) {
    if (!is_legal_action(state, action)) {
        return reject(action, ActionStatus::IllegalAction, "mulligan action is not legal in the current state");
    }
    if (config.hand_limit < 0) {
        return reject(action, ActionStatus::InvalidTarget, "hand_limit must be non-negative");
    }

    auto& player = state.player(action.player_id);
    if (player.deck.empty()) {
        return reject(action, ActionStatus::EmptyDeck, "cannot mulligan without a deck card to draw");
    }

    ActionResult result;
    result.action = action;
    result.status = ActionStatus::Applied;
    result.applied = true;

    // One-card manual mulligan, matching the Python setup behavior in shape:
    // selected hand card is held in Stay, one replacement is drawn, then the
    // selected card is returned to the bottom of the deck. We intentionally do
    // not shuffle here yet; deterministic shuffle/RNG belongs in a later random
    // action policy layer.
    state.move_card(action.source_entity_id, Location{action.player_id, Zone::Stay, player.stay.size()});

    if (static_cast<int>(player.hand.size()) < config.hand_limit && !player.deck.empty()) {
        const EntityId replacement = player.deck.front();
        state.move_card(replacement, Location{action.player_id, Zone::Hand, player.hand.size()});
        result.events.push_back("mulligan_replacement_drawn");
    }

    state.move_card(action.source_entity_id, Location{action.player_id, Zone::Deck, player.deck.size()});
    player.mulligans_available -= 1;
    result.events.push_back("mulligan");
    if (player.mulligans_available <= 0) {
        finish_mulligan_for_player(state, action.player_id);
        result.events.push_back("mulligan_allowance_exhausted");
    }
    append_next_decision(state, result);
    return result;
}

ActionResult apply_keep_hand(GameState& state, const Action& action) {
    if (!is_legal_action(state, action)) {
        return reject(action, ActionStatus::IllegalAction, "keep-hand action is not legal in the current state");
    }
    ActionResult result;
    result.action = action;
    result.status = ActionStatus::Applied;
    result.applied = true;
    finish_mulligan_for_player(state, action.player_id);
    result.events.push_back("mulligan_keep_hand");
    if (!state.mulligan_player_id.has_value()) {
        result.events.push_back("mulligan_phase_finished");
    }
    append_next_decision(state, result);
    return result;
}

ActionResult apply_use_leader(GameState& state, const Action& action, const ReducerConfig& config) {
    if (!is_executable_action(state, action)) {
        return reject(action, ActionStatus::IllegalAction, "leader action is not executable in the current turn state");
    }

    ActionResult result;
    result.action = action;
    result.status = ActionStatus::Applied;
    result.applied = true;

    state.player(action.player_id).leader_used = true;
    result.events.push_back("leader_used:no_effects");
    if (config.leader_action_consumes_turn) {
        switch_to_next_player_or_finish_round(state, result);
        append_next_decision(state, result);
    } else {
        state.turn_contexts[static_cast<std::size_t>(action.player_id)].used_intra_turn_action = true;
        append_next_decision(state, result);
    }
    return result;
}

ActionResult apply_use_order(GameState& state, const Action& action, const ReducerConfig& config) {
    if (!is_executable_action(state, action)) {
        return reject(action, ActionStatus::IllegalAction, "order action is not executable in the current turn state");
    }

    ActionResult result;
    result.action = action;
    result.status = ActionStatus::Applied;
    result.applied = true;

    RuntimeCard* card = state.find_card(action.source_entity_id);
    if (card == nullptr) {
        return reject(action, ActionStatus::InvalidSource, "order source does not exist");
    }

    // Stratagems are command entities. Until their real effect is registered,
    // consuming them means banishing the command marker after use. Unit orders
    // with explicit charges lose one charge; metadata/effect-only orders are
    // simply recorded as used for this minimal milestone.
    if (card->definition->card_type == CardType::Stratagem) {
        state.move_card(action.source_entity_id, Location{card->owner_id, Zone::Banished, state.player(card->owner_id).banished.size()});
        result.events.push_back("stratagem_order_used:no_effects");
    } else if (card->state.order_charges > 0) {
        card->state.order_charges -= 1;
        result.events.push_back("order_charge_used:no_effects");
    } else {
        card->runtime.order_used = true;
        result.events.push_back("order_used:no_effects");
    }

    if (config.order_action_consumes_turn) {
        switch_to_next_player_or_finish_round(state, result);
        append_next_decision(state, result);
    } else {
        state.turn_contexts[static_cast<std::size_t>(action.player_id)].used_intra_turn_action = true;
        append_next_decision(state, result);
    }
    return result;
}

}  // namespace

ActionResult apply_action(GameState& state, const Action& action, const ReducerConfig& config) {
    if (state.status == MatchStatus::Finished || state.phase == MatchPhase::Finished) {
        state.engine_phase = EnginePhase::Finished;
    } else if (state.pending_choice.has_value()) {
        state.engine_phase = EnginePhase::AwaitingChoice;
    } else if (state.status == MatchStatus::Running && state.phase == MatchPhase::Playing) {
        state.engine_phase = EnginePhase::ActionWindow;
    }

    if (!is_valid_player_id(action.player_id)) {
        return reject(action, ActionStatus::InvalidPlayer, "action.player_id must be 0 or 1");
    }

    if (is_current_turn_action(action.type)) {
        if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing) {
            return reject(action, ActionStatus::InvalidPhase, "turn action requires a running match in playing phase");
        }
        if (state.current_player_id != action.player_id) {
            return reject(action, ActionStatus::NotCurrentPlayer, "turn action must be submitted by current_player_id");
        }
    }

    switch (action.type) {
        case ActionType::Pass:
            return apply_pass(state, action);
        case ActionType::EndTurn: {
            if (!is_legal_action(state, action)) {
                return reject(action, ActionStatus::IllegalAction, "end-turn is not legal in the current state");
            }
            ActionResult result;
            result.action = action;
            result.status = ActionStatus::Applied;
            result.applied = true;
            PlayerState& actor = state.player(action.player_id);
            if (actor.hand.empty()) {
                actor.passed = true;
                actor.pass_reason = PassReason::HandExhausted;
                result.events.push_back("hand_exhausted_pass");
            } else {
                result.events.push_back("end_turn");
            }
            state.turn_contexts[static_cast<std::size_t>(action.player_id)] = {};
            switch_to_next_player_or_finish_round(state, result);
            append_next_decision(state, result);
            return result;
        }
        case ActionType::PlayCard:
            return apply_play_card(state, action);
        case ActionType::DiscardCard:
            return apply_discard_card(state, action);
        case ActionType::Mulligan:
            return apply_mulligan(state, action, config);
        case ActionType::KeepHand:
            return apply_keep_hand(state, action);
        case ActionType::UseLeader:
            return apply_use_leader(state, action, config);
        case ActionType::UseOrder:
            return apply_use_order(state, action, config);
        case ActionType::ChooseCardTarget:
        case ActionType::ChooseRowTarget:
        case ActionType::ChooseInsertPosition:
            return reject(action, ActionStatus::UnsupportedAction, "pending choices require the event kernel");
    }

    return reject(action, ActionStatus::UnsupportedAction, "unknown action type");
}

std::optional<Decision> current_decision_after_reducer(const GameState& state) {
    if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing) {
        return std::nullopt;
    }
    return make_current_turn_decision(state);
}

}  // namespace gwent
