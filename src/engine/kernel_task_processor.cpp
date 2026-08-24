#include "kernel_internal.hpp"

namespace gwent::kernel_detail {
namespace {

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

}  // namespace

namespace {

ContinuationStep continuation_step(ContinuationStepKind kind, PlayerId actor_id, EntityId source_id = kInvalidEntityId) {
    ContinuationStep step;
    step.kind = kind;
    step.actor_id = actor_id;
    step.source_entity_id = source_id;
    return step;
}

void enqueue_continuation(TaskQueue& queue, const std::vector<ContinuationStep>& steps) {
    for (const ContinuationStep& step : steps) {
        switch (step.kind) {
            case ContinuationStepKind::ConsumeOrderSource:
                queue.push_back(Task::consume_order_source(step.actor_id, step.source_entity_id));
                break;
            case ContinuationStepKind::FinishSpecialCard:
                queue.push_back(Task::finish_special_card(step.actor_id, step.source_entity_id));
                break;
            case ContinuationStepKind::AdvanceTurnOrFinishRound:
                queue.push_back(Task::advance_turn_or_finish_round(step.actor_id));
                break;
            case ContinuationStepKind::MarkIntraTurnFollowup:
                queue.push_back(Task::mark_intra_turn_followup(step.actor_id, step.source_entity_id));
                break;
            case ContinuationStepKind::PlayStagedCard: {
                Task play = Task::play_card(step.actor_id, step.source_entity_id, ActionTarget::none());
                play.reason = "created";
                queue.push_back(std::move(play));
                break;
            }
        }
    }
}

}  // namespace

void process_task(
    GameState& state,
    TaskQueue& queue,
    KernelResult& result,
    const EffectRegistry& effects,
    const KernelConfig& config,
    const std::shared_ptr<ResolutionFrame>& resolution_frame,
    const Task& task
) {
    KernelContext context{state, queue, result.events, config.performance_counters, resolution_frame, {}, std::nullopt};

    switch (task.type) {
        case TaskType::EndTurn: {
            enqueue_pending_order_consumption(queue, state, task.actor_id);

            PlayerState& actor = state.player(task.actor_id);
            const bool hand_exhausted = actor.hand.empty();
            if (hand_exhausted) {
                // Playing or discarding the final hand card does not pass
                // immediately: the player may still use any legal leader/order
                // follow-ups.  Only when that player finally closes the action
                // window does EndTurn acquire PASS semantics.
                actor.passed = true;
                actor.pass_reason = PassReason::HandExhausted;
                emit(result.events, task.type, "hand_exhausted_pass", task.actor_id);
            } else {
                emit(result.events, task.type, "end_turn", task.actor_id);
            }

            // Keep the complete TurnContext alive through DispatchTurnEnd.
            // Turn-end card effects (for example Aen Elle Aristocrat) inspect
            // facts accumulated during this turn, such as which enemy rows
            // received Frost.  The transition clears these fields only after
            // all turn_end triggers have resolved.
            queue.push_back(Task::advance_turn_or_finish_round(task.actor_id));
            break;
        }
        case TaskType::Pass: {
            PlayerState& actor = state.player(task.actor_id);
            actor.passed = true;
            actor.pass_reason = PassReason::Explicit;
            emit(result.events, task.type, "pass", task.actor_id);
            queue.push_back(Task::advance_turn_or_finish_round(task.actor_id));
            break;
        }
        case TaskType::Mulligan: {
            if (config.hand_limit < 0) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "hand_limit must be non-negative";
                queue.clear();
                break;
            }
            auto& player = state.player(task.actor_id);
            if (player.deck.empty()) {
                result.status = ActionStatus::EmptyDeck;
                result.applied = false;
                result.message = "cannot mulligan without a deck card to draw";
                queue.clear();
                break;
            }

            primitive_move_card(state, task.source_entity_id, Location{task.actor_id, Zone::Stay, player.stay.size()});
            if (static_cast<int>(player.hand.size()) < config.hand_limit && !player.deck.empty()) {
                const EntityId replacement = player.deck.front();
                primitive_move_card(state, replacement, Location{task.actor_id, Zone::Hand, player.hand.size()});
                emit(result.events, task.type, "mulligan_replacement_drawn", task.actor_id, replacement);
            }
            primitive_move_card(state, task.source_entity_id, Location{task.actor_id, Zone::Deck, player.deck.size()});
            player.mulligans_available -= 1;
            emit(result.events, task.type, "mulligan", task.actor_id, task.source_entity_id);
            if (player.mulligans_available <= 0) {
                finish_mulligan_for_player(state, task.actor_id);
                emit(result.events, task.type, "mulligan_allowance_exhausted", task.actor_id);
            }
            break;
        }
        case TaskType::KeepHand: {
            finish_mulligan_for_player(state, task.actor_id);
            emit(result.events, task.type, "mulligan_keep_hand", task.actor_id);
            if (!state.mulligan_player_id.has_value()) {
                emit(result.events, task.type, "mulligan_phase_finished", task.actor_id);
            }
            break;
        }
        case TaskType::PlayCard: {
            RuntimeCard* card = state.find_card(task.source_entity_id);
            if (card == nullptr) {
                result.status = ActionStatus::InvalidSource;
                result.applied = false;
                result.message = "play-card source does not exist";
                queue.clear();
                break;
            }
            const auto source_location_before = state.location_of(task.source_entity_id);
            const bool played_from_hand = source_location_before.has_value()
                && source_location_before->side == task.actor_id
                && source_location_before->zone == Zone::Hand;

            if (card->definition->card_type == CardType::Special) {
                card->state.deploying = true;
                primitive_move_card(
                    state,
                    task.source_entity_id,
                    Location{task.actor_id, Zone::Stay, state.player(task.actor_id).stay.size()}
                );
                if (RuntimeCard* moved = state.find_card(task.source_entity_id)) {
                    moved->state.deploying = false;
                }
                emit(result.events, task.type, "card_played", task.actor_id, task.source_entity_id);
                emit(result.events, task.type, "special_played", task.actor_id, task.source_entity_id);
                if (played_from_hand) {
                    mark_post_play_pending_end(state, task.actor_id);
                    state.turn_contexts[static_cast<std::size_t>(task.actor_id)].played_card_this_turn = true;
                }
                {
                    Task resolve = Task::resolve_special(task.actor_id, task.source_entity_id, task.target);
                    resolve.reason = task.reason;
                    queue.push_back(std::move(resolve));
                }
                // Queue the post-trigger continuation before dispatch. If a
                // trigger requests a choice, request_*_choice() suspends this
                // task together with the rest of the FIFO tail.
                dispatch_trigger(context, effects, task.type, "card_played", task.actor_id, task.source_entity_id, task.source_entity_id);
                break;
            }

            if (task.target.kind == ActionTargetKind::None) {
                std::vector<Location> legal_rows;
                for (const ActionTarget& target : legal_row_targets_for_card(state, task.actor_id, *card)) {
                    if (target.kind == ActionTargetKind::Row && is_battle_zone(target.zone)) {
                        legal_rows.push_back(Location{target.side, target.zone, 0});
                    }
                }
                if (legal_rows.empty()) {
                    result.status = ActionStatus::InvalidTarget;
                    result.applied = false;
                    result.message = "non-special play-card has no legal deployment row";
                    queue.clear();
                    break;
                }
                context.request_play_row_choice(
                    task.actor_id,
                    task.source_entity_id,
                    "选择部署排",
                    std::move(legal_rows)
                );
                break;
            }

            if (task.target.kind != ActionTargetKind::Row || !is_battle_zone(task.target.zone)) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "non-special play-card task requires a battle-row target";
                queue.clear();
                break;
            }
            const auto& target_row = state.player(task.target.side).row(task.target.zone);
            if (task.insert_position < 0
                || static_cast<std::size_t>(task.insert_position) > target_row.size()) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "play-card insert_position is outside the current row sequence";
                queue.clear();
                break;
            }

            card->state.deploying = true;
            const bool moved_to_row = primitive_move_card(
                state,
                task.source_entity_id,
                Location{task.target.side, task.target.zone, static_cast<std::size_t>(task.insert_position)}
            );
            if (RuntimeCard* moved = state.find_card(task.source_entity_id)) {
                moved->state.deploying = false;
            }
            if (!moved_to_row) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "play-card target row is full";
                queue.clear();
                break;
            }
            emit(result.events, task.type, "card_played", task.actor_id, task.source_entity_id);
            if (RuntimeCard* entered = state.find_card(task.source_entity_id)) {
                entered->runtime.entered_this_turn = true;
            }
            if (played_from_hand) {
                mark_post_play_pending_end(state, task.actor_id);
                state.turn_contexts[static_cast<std::size_t>(task.actor_id)].played_card_this_turn = true;
            }
            {
                Task resolve = Task::resolve_deploy(task.actor_id, task.source_entity_id, ActionTarget::none());
                resolve.reason = task.reason;
                queue.push_back(std::move(resolve));
            }
            dispatch_trigger(context, effects, task.type, "card_played", task.actor_id, task.source_entity_id, task.source_entity_id);
            break;
        }
        case TaskType::ResolveDeploy: {
            context.clear_continuation();
            context.choice_origin_action_type = ActionType::PlayCard;
            invoke_effects_for_card(context, effects, task.type, "deploy", task.actor_id, task.source_entity_id, task.target);
            if (state.pending_choice.has_value()) {
                break;
            }
            enqueue_continuation(queue, context.continuation);
            break;
        }
        case TaskType::ResolveSpecial: {
            context.choice_origin_action_type = ActionType::PlayCard;
            context.set_continuation({
                continuation_step(ContinuationStepKind::FinishSpecialCard, task.actor_id, task.source_entity_id),
            });
            invoke_effects_for_card(context, effects, task.type, "special", task.actor_id, task.source_entity_id, task.target);
            if (state.pending_choice.has_value()) {
                break;
            }
            enqueue_continuation(queue, context.continuation);
            break;
        }
        case TaskType::FinishSpecialCard: {
            RuntimeCard* card = state.find_card(task.source_entity_id);
            if (card == nullptr) {
                result.status = ActionStatus::InvalidSource;
                result.applied = false;
                result.message = "special-card source does not exist";
                queue.clear();
                break;
            }
            const auto current = state.location_of(task.source_entity_id);
            if (current.has_value() && current->zone == Zone::Stay) {
                const Zone destination = card->state.doomed ? Zone::Banished : Zone::Cemetery;
                primitive_move_card(state, task.source_entity_id, Location{card->owner_id, destination, state.player(card->owner_id).zone_size(destination)});
                emit(result.events, task.type, destination == Zone::Banished ? "special_banished" : "special_moved_to_cemetery", task.actor_id, task.source_entity_id);
            } else {
                emit(result.events, task.type, "special_finish_skipped", task.actor_id, task.source_entity_id);
            }
            (void)task;
            break;
        }
        case TaskType::UseLeader: {
            RuntimeCard* leader = state.find_card(task.source_entity_id);
            if (leader == nullptr) {
                result.status = ActionStatus::InvalidSource;
                result.applied = false;
                result.message = "leader source does not exist";
                queue.clear();
                break;
            }
            if (leader->state.order_charges > 0) {
                leader->state.order_charges -= 1;
                state.player(task.actor_id).leader_used = leader->state.order_charges == 0;
                emit(result.events, task.type, "leader_charge_used", task.actor_id, task.source_entity_id, kInvalidEntityId, leader->state.order_charges);
            } else {
                state.player(task.actor_id).leader_used = true;
                emit(result.events, task.type, "leader_used", task.actor_id, task.source_entity_id);
            }
            context.set_continuation({
                continuation_step(
                    config.leader_action_consumes_turn
                        ? ContinuationStepKind::AdvanceTurnOrFinishRound
                        : ContinuationStepKind::MarkIntraTurnFollowup,
                    task.actor_id,
                    task.source_entity_id
                ),
            });
            context.choice_origin_action_type = ActionType::UseLeader;
            invoke_effects_for_card(context, effects, task.type, "leader", task.actor_id, task.source_entity_id, task.target);
            if (state.pending_choice.has_value()) {
                break;
            }
            enqueue_continuation(queue, context.continuation);
            break;
        }
        case TaskType::UseOrder: {
            emit(result.events, task.type, "order_requested", task.actor_id, task.source_entity_id);
            queue.push_back(Task::resolve_order(task.actor_id, task.source_entity_id, task.target));
            break;
        }
        case TaskType::ResolveOrder: {
            RuntimeCard* order_source = state.find_card(task.source_entity_id);
            if (order_source == nullptr) {
                result.status = ActionStatus::InvalidSource;
                result.applied = false;
                result.message = "order source does not exist";
                queue.clear();
                break;
            }

            // Consume an order only after its whole effect chain has resolved.
            // The continuation is copied into PendingChoice, so one or more
            // nested choices can pause/resume without losing the order consume
            // or the action-window follow-up semantics.
            context.set_continuation({
                continuation_step(ContinuationStepKind::ConsumeOrderSource, task.actor_id, task.source_entity_id),
                continuation_step(
                    config.order_action_consumes_turn
                        ? ContinuationStepKind::AdvanceTurnOrFinishRound
                        : ContinuationStepKind::MarkIntraTurnFollowup,
                    task.actor_id,
                    task.source_entity_id
                ),
            });
            context.choice_origin_action_type = ActionType::UseOrder;
            invoke_effects_for_card(context, effects, task.type, "order", task.actor_id, task.source_entity_id, task.target);
            if (state.pending_choice.has_value()) {
                break;
            }

            enqueue_continuation(queue, context.continuation);
            break;
        }
        case TaskType::ConsumeOrderSource: {
            RuntimeCard* card = state.find_card(task.source_entity_id);
            if (card == nullptr) {
                result.status = ActionStatus::InvalidSource;
                result.applied = false;
                result.message = "order source does not exist";
                queue.clear();
                break;
            }
            card->runtime.order_pending_consume = false;
            if (card->definition->card_type == CardType::Stratagem) {
                primitive_move_card(state, task.source_entity_id, Location{card->owner_id, Zone::Banished, state.player(card->owner_id).banished.size()});
                emit(result.events, task.type, "stratagem_consumed", task.actor_id, task.source_entity_id);
            } else if (card->state.order_charges > 0) {
                card->state.order_charges -= 1;
                if (card->state.order_charges == 0) {
                    card->runtime.order_used = true;
                }
                emit(result.events, task.type, "order_charge_consumed", task.actor_id, task.source_entity_id);
            } else {
                const int cooldown = std::max(0, metadata_int(*card->definition, "cooldown", 0));
                if (cooldown > 0) {
                    const auto updated = primitive_modify_card_counter(
                        state,
                        task.source_entity_id,
                        CardCounterField::Cooldown,
                        cooldown,
                        CardCounterMode::Set
                    );
                    card->runtime.order_used = false;
                    emit(result.events, task.type, "order_cooldown_started", task.actor_id, task.source_entity_id, kInvalidEntityId, updated.value_or(cooldown));
                } else {
                    card->runtime.order_used = true;
                    emit(result.events, task.type, "order_marked_used", task.actor_id, task.source_entity_id);
                }
            }
            break;
        }
        case TaskType::MarkIntraTurnFollowup: {
            mark_no_pass_after_intra_turn_action(state, task.actor_id);
            emit(result.events, task.type, "intra_turn_followup_required", task.actor_id, task.source_entity_id);
            break;
        }
        case TaskType::AdvanceTurnOrFinishRound: {
            // Statuses such as bleeding/vitality still resolve at the end of the
            // acting player's turn. Cooldown/timer counters tick on the owner's
            // next turn start, not here.
            queue.push_back(Task::tick_turn_statuses(task.actor_id));
            queue.push_back(Task::dispatch_turn_end(task.actor_id));
            queue.push_back(Task::apply_turn_transition(task.actor_id));
            break;
        }
        case TaskType::TickTurnCounters: {
            tick_turn_counters(context, task.actor_id);
            break;
        }
        case TaskType::TickTurnStatuses: {
            tick_turn_statuses(context, task.actor_id);
            break;
        }
        case TaskType::DispatchTurnEnd: {
            dispatch_trigger(context, effects, task.type, "turn_end", task.actor_id);
            break;
        }
        case TaskType::ApplyTurnTransition: {
            finish_round_or_advance_turn(context, result, effects, queue, task.actor_id);
            break;
        }
        case TaskType::FinalizeRoundEnd: {
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
                emit(result.events, task.type, "match_finished", task.actor_id);
            } else {
                queue.push_front(Task::round_cleanup(task.actor_id, task.round_winner_id));
            }
            break;
        }
        case TaskType::DispatchTurnStartRowEffects: {
            dispatch_turn_start_row_effects(context, task.actor_id);
            break;
        }
        case TaskType::DispatchTurnStart: {
            if (state.player(task.actor_id).passed) {
                queue.push_front(Task::advance_turn_or_finish_round(task.actor_id));
            }
            dispatch_trigger(context, effects, task.type, "turn_start", task.actor_id);
            break;
        }
        case TaskType::RoundCleanup: {
            cleanup_board_after_round(context);
            prepare_next_round_from_state(context, task.round_winner_id);
            if (!state.pending_choice.has_value()) {
                queue.push_back(Task::dispatch_round_start(state.current_player_id));
            }
            break;
        }
        case TaskType::DispatchRoundStart: {
            queue.push_front(Task::dispatch_turn_start(task.actor_id));
            queue.push_front(Task::dispatch_turn_start_row_effects(task.actor_id));
            queue.push_front(Task::tick_turn_counters(task.actor_id));
            dispatch_trigger(context, effects, task.type, "round_start", task.actor_id, kInvalidEntityId, kInvalidEntityId, state.round_no);
            break;
        }
        case TaskType::ResolvePendingCardChoice: {
            if (!state.pending_choice.has_value()) {
                result.status = ActionStatus::InvalidPhase;
                result.applied = false;
                result.message = "cannot resolve pending choice without a pending choice";
                queue.clear();
                break;
            }

            PendingChoice choice = std::move(state.pending_choice.value());
            state.pending_choice.reset();
            context.set_continuation(choice.continuation);
            const EntityId event_target_id = task.target.kind == ActionTargetKind::Card ? task.target.entity_id : kInvalidEntityId;
            const char* resolved_event = choice.kind == PendingChoiceKind::RowTarget
                ? "pending_row_choice_resolved"
                : (choice.kind == PendingChoiceKind::InsertPosition
                    ? "pending_insert_position_resolved"
                    : "pending_card_choice_resolved");
            emit(result.events, task.type, resolved_event, task.actor_id, choice.source_entity_id, event_target_id, static_cast<int>(choice.continuation.size()), choice.effect_id);

            if (choice.resume_kind == PendingChoiceResumeKind::PlayCard) {
                if (task.target.kind != ActionTargetKind::Row || !is_battle_zone(task.target.zone)) {
                    result.status = ActionStatus::InvalidTarget;
                    result.applied = false;
                    result.message = "play-card row choice requires a battle-row target";
                    queue.clear();
                    break;
                }
                if (choice.kind == PendingChoiceKind::RowTarget) {
                    PendingChoice insert_choice;
                    insert_choice.kind = PendingChoiceKind::InsertPosition;
                    insert_choice.resume_kind = PendingChoiceResumeKind::PlayCard;
                    insert_choice.choice_id = state.next_pending_choice_id++;
                    insert_choice.player_id = task.actor_id;
                    insert_choice.source_entity_id = choice.source_entity_id;
                    insert_choice.prompt = "选择部署位置";
                    insert_choice.origin_action_type = ActionType::PlayCard;
                    const std::size_t row_size = state.player(task.target.side).row(task.target.zone).size();
                    insert_choice.legal_row_targets.reserve(row_size + 1);
                    for (std::size_t position = 0; position <= row_size; ++position) {
                        insert_choice.legal_row_targets.push_back(Location{task.target.side, task.target.zone, position});
                    }
                    insert_choice.continuation = std::move(choice.continuation);
                    insert_choice.suspended_tasks = std::move(choice.suspended_tasks);
                    insert_choice.resolution_frame = std::move(choice.resolution_frame);
                    if (insert_choice.resolution_frame) {
                        consume_choice_budget(*insert_choice.resolution_frame);
                    }
                    state.pending_choice = std::move(insert_choice);
                    state.engine_phase = EnginePhase::AwaitingChoice;
                    emit(result.events, task.type, "play_insert_position_choice_requested", task.actor_id, choice.source_entity_id);
                    break;
                }
                Task resumed_play = Task::play_card(
                    task.actor_id,
                    choice.source_entity_id,
                    task.target,
                    task.insert_position
                );
                resumed_play.reason = "selected_play_row";
                queue.push_back(std::move(resumed_play));
                queue.append_all(std::move(choice.suspended_tasks));
                enqueue_continuation(queue, context.continuation);
                emit(result.events, task.type, "play_row_choice_resolved", task.actor_id, choice.source_entity_id, kInvalidEntityId, 0, {});
                break;
            }

            context.choice_origin_action_type = choice.origin_action_type;
            EffectCall call;
            call.effect_id = choice.effect_id;
            call.actor_id = task.actor_id;
            call.source_entity_id = choice.source_entity_id;
            call.target = task.target;

            if (effects.invoke(context, call)) {
                context.emit(EventRecord{task.type, "pending_choice:effect", task.actor_id, choice.source_entity_id, event_target_id, 0, choice.effect_id});
            } else {
                context.emit(EventRecord{task.type, "pending_choice:unregistered_effect", task.actor_id, choice.source_entity_id, event_target_id, 0, choice.effect_id});
            }

            if (state.pending_choice.has_value()) {
                // A nested choice captured tasks enqueued by the resumed effect.
                // The outer suspended tail must follow those tasks, preserving
                // FIFO semantics across arbitrarily deep choice chains.
                auto& nested = state.pending_choice.value();
                for (Task& suspended : choice.suspended_tasks) {
                    nested.suspended_tasks.push_back(std::move(suspended));
                }
                break;
            }

            // If the original choice interrupted a trigger dispatcher, resume
            // exactly after the trigger/effect that requested the choice. A
            // later trigger may itself request another choice; in that case it
            // inherits the same ResolutionFrame and must also retain the outer
            // suspended task tail.
            resume_trigger_dispatch(context, effects);
            if (state.pending_choice.has_value()) {
                auto& nested = state.pending_choice.value();
                for (Task& suspended : choice.suspended_tasks) {
                    nested.suspended_tasks.push_back(std::move(suspended));
                }
                break;
            }

            // Finish the resumed effect's newly-enqueued work first, then the
            // queue tail that existed before the choice, and finally the
            // explicit outer continuation (consume order, finish special, ...).
            queue.append_all(std::move(choice.suspended_tasks));
            enqueue_continuation(queue, context.continuation);
            break;
        }
        case TaskType::MoveCard: {
            if (!task.destination.has_value()) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "MoveCard task requires a destination";
                queue.clear();
                break;
            }
            const bool changed = primitive_move_card(state, task.target_entity_id, task.destination.value());
            emit(result.events, task.type, changed ? "card_moved" : "move_ignored", task.actor_id, task.source_entity_id, task.target_entity_id);
            break;
        }
        case TaskType::SpawnCard: {
            if (!task.destination.has_value() || task.card_definition_id == kInvalidCardDefId) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "SpawnCard task requires card_definition_id and destination";
                queue.clear();
                break;
            }
            const EntityId entity_id = primitive_spawn_card(state, task.card_definition_id, task.actor_id, task.destination.value());
            const bool changed = entity_id != kInvalidEntityId;
            if (changed && is_battle_zone(task.destination->zone)) {
                if (RuntimeCard* spawned = state.find_card(entity_id)) {
                    spawned->runtime.entered_this_turn = true;
                }
            }
            emit(result.events, task.type, changed ? "card_spawned" : "spawn_ignored", task.actor_id, task.source_entity_id, entity_id);
            break;
        }
        case TaskType::BoostCard: {
            const bool changed = primitive_boost_card(state, task.target_entity_id, task.amount);
            emit(result.events, task.type, changed ? "card_boosted" : "boost_ignored", task.actor_id, task.source_entity_id, task.target_entity_id, task.amount);
            break;
        }
        case TaskType::SetPower: {
            const bool changed = primitive_set_power(state, task.target_entity_id, task.amount);
            emit(result.events, task.type, changed ? "power_set" : "set_power_ignored", task.actor_id, task.source_entity_id, task.target_entity_id, task.amount);
            break;
        }
        case TaskType::DamageCard: {
            const RuntimeCard* before_card = state.find_card(task.target_entity_id);
            const int old_armor = before_card == nullptr ? 0 : before_card->state.armor;
            const int old_power = before_card == nullptr ? 0 : before_card->state.power;
            const PlayerId target_controller = before_card == nullptr ? static_cast<PlayerId>(-1) : before_card->controller_id;
            const bool changed = task.reason == "bleeding"
                ? primitive_damage_card_ignoring_armor(state, task.target_entity_id, task.amount)
                : primitive_damage_card(state, task.target_entity_id, task.amount);
            emit(result.events, task.type, changed ? "card_damaged" : "damage_ignored", task.actor_id, task.source_entity_id, task.target_entity_id, task.amount);
            if (changed) {
                const RuntimeCard* card = state.find_card(task.target_entity_id);
                if (task.reason == "frost" && card != nullptr && is_valid_player_id(target_controller)) {
                    const int actual_power_damage = std::max(0, old_power - card->state.power);
                    const PlayerId frost_owner = state.opponent_id(target_controller);
                    if (actual_power_damage > 0) {
                        for (RuntimeCard& tracked : state.cards) {
                            if (tracked.owner_id != frost_owner) continue;
                            const auto marker = tracked.definition->metadata.find("tracks_frost_damage_history");
                            if (marker == tracked.definition->metadata.end() || marker->second != "true") continue;
                            int current = 0;
                            const auto it = tracked.memory.find("frost_damage_4");
                            if (it != tracked.memory.end()) {
                                const auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), current);
                                if (ec != std::errc{} || ptr != it->second.data() + it->second.size()) current = 0;
                            }
                            tracked.memory["frost_damage_4"] = std::to_string(current + actual_power_damage);
                        }
                    }
                }
                if (card != nullptr && card->has_power() && card->state.is_dead()) {
                    // Resolve lethal damage before any already-queued follow-up on the same target.
                    // Example: Feast of Blood must not add Bleeding after its 3 damage killed the unit.
                    queue.push_front(Task::destroy_card(task.actor_id, task.target_entity_id, "damage", task.source_entity_id));
                }
                if (card != nullptr && old_armor > 0 && card->state.armor == 0) {
                    dispatch_trigger(context, effects, task.type, "armor_broken", task.actor_id, task.source_entity_id, task.target_entity_id, old_armor, "damage");
                }
            }
            break;
        }
        case TaskType::DestroyCard: {
            const RuntimeCard* target = state.find_card(task.target_entity_id);
            const Zone destination = (target != nullptr && target->state.doomed) ? Zone::Banished : Zone::Cemetery;
            const bool changed = primitive_destroy_card(state, task.target_entity_id, destination);
            emit(result.events, task.type, changed ? (destination == Zone::Banished ? "card_banished" : "card_destroyed") : "destroy_ignored", task.actor_id, task.source_entity_id, task.target_entity_id, 0, task.reason);
            if (changed) {
                dispatch_trigger(context, effects, task.type, "card_destroyed", task.actor_id, task.source_entity_id, task.target_entity_id, 0, task.reason);
            }
            break;
        }
        case TaskType::SummonCard: {
            if (!task.destination.has_value()) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "SummonCard task requires a destination";
                queue.clear();
                break;
            }
            const bool changed = primitive_summon_card(state, task.target_entity_id, task.destination.value());
            if (changed && is_battle_zone(task.destination->zone)) {
                if (RuntimeCard* summoned = state.find_card(task.target_entity_id)) {
                    summoned->runtime.entered_this_turn = true;
                }
            }
            emit(result.events, task.type, changed ? "card_summoned" : "summon_ignored", task.actor_id, task.source_entity_id, task.target_entity_id);
            if (changed) {
                dispatch_trigger(context, effects, task.type, "card_summoned", task.actor_id, task.source_entity_id, task.target_entity_id);
            }
            break;
        }
        case TaskType::BanishCard: {
            const bool changed = primitive_banish_card(state, task.target_entity_id);
            emit(result.events, task.type, changed ? "card_banished" : "banish_ignored", task.actor_id, task.source_entity_id, task.target_entity_id);
            break;
        }
        case TaskType::DiscardCard: {
            const bool changed = primitive_discard_card(state, task.target_entity_id);
            emit(result.events, task.type, changed ? "card_discarded" : "discard_ignored", task.actor_id, task.source_entity_id, task.target_entity_id);
            if (changed) {
                if (task.source_entity_id == task.target_entity_id && task.actor_id == state.current_player_id) {
                    mark_post_play_pending_end(state, task.actor_id);
                    if (is_valid_player_id(task.actor_id)) {
                        state.turn_contexts[static_cast<std::size_t>(task.actor_id)].discarded_card_this_turn = true;
                    }
                }
                dispatch_trigger(context, effects, task.type, "card_discarded", task.actor_id, task.source_entity_id, task.target_entity_id);
            }
            break;
        }
        case TaskType::ConsumeCard: {
            const auto target_location_before = state.location_of(task.target_entity_id);
            const int boost_amount = primitive_consume_card(state, task.source_entity_id, task.target_entity_id);
            emit(result.events, task.type, boost_amount > 0 ? "card_consumed" : "consume_ignored", task.actor_id, task.source_entity_id, task.target_entity_id, boost_amount);
            if (boost_amount > 0 && target_location_before.has_value()) {
                if (target_location_before->zone == Zone::Cemetery) {
                    dispatch_trigger(context, effects, task.type, "card_banished", task.actor_id, task.source_entity_id, task.target_entity_id, boost_amount, "consume");
                } else if (is_battle_zone(target_location_before->zone)) {
                    dispatch_trigger(context, effects, task.type, "card_destroyed", task.actor_id, task.source_entity_id, task.target_entity_id, boost_amount, "consume");
                }
            }
            break;
        }
        case TaskType::DrainCard: {
            const RuntimeCard* before_card = state.find_card(task.target_entity_id);
            const int old_armor = before_card == nullptr ? 0 : before_card->state.armor;
            const int drained = primitive_drain_card(state, task.source_entity_id, task.target_entity_id, task.amount);
            emit(result.events, task.type, drained > 0 ? "card_drained" : "drain_ignored", task.actor_id, task.source_entity_id, task.target_entity_id, drained);
            const RuntimeCard* target = state.find_card(task.target_entity_id);
            if (drained > 0 && target != nullptr && target->has_power() && target->state.is_dead()) {
                queue.push_back(Task::destroy_card(task.actor_id, task.target_entity_id, "drain", task.source_entity_id));
            }
            if (drained > 0 && target != nullptr && old_armor > 0 && target->state.armor == 0) {
                dispatch_trigger(context, effects, task.type, "armor_broken", task.actor_id, task.source_entity_id, task.target_entity_id, old_armor, "drain");
            }
            break;
        }
        case TaskType::AddArmorCard: {
            const bool changed = primitive_add_armor(state, task.target_entity_id, task.amount);
            emit(result.events, task.type, changed ? "armor_added" : "armor_ignored", task.actor_id, task.source_entity_id, task.target_entity_id, task.amount);
            break;
        }
        case TaskType::AddStatusCard: {
            const auto status = [&]() -> std::optional<CardStatus> {
                if (task.reason == "bleeding") return CardStatus::Bleeding;
                if (task.reason == "vitality") return CardStatus::Vitality;
                if (task.reason == "shield") return CardStatus::Shield;
                if (task.reason == "locked") return CardStatus::Locked;
                if (task.reason == "veil") return CardStatus::Veil;
                if (task.reason == "poison") return CardStatus::Poison;
                if (task.reason == "doomed") return CardStatus::Doomed;
                if (task.reason == "immune") return CardStatus::Immune;
                if (task.reason == "defender") return CardStatus::Defender;
                if (task.reason == "rupture") return CardStatus::Rupture;
                if (task.reason == "spying") return CardStatus::Spying;
                if (task.reason == "bounty") return CardStatus::Bounty;
                if (task.reason == "resilience") return CardStatus::Resilience;
                if (task.reason == "infiltration") return CardStatus::Infiltration;
                return std::nullopt;
            }();
            if (!status.has_value()) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "AddStatusCard task has unknown status: " + task.reason;
                queue.clear();
                break;
            }
            const bool changed = primitive_add_status(state, task.target_entity_id, status.value(), task.amount);
            emit(result.events, task.type, changed ? "status_added" : "status_ignored", task.actor_id, task.source_entity_id, task.target_entity_id, task.amount, task.reason);
            if (changed) {
                const RuntimeCard* card = state.find_card(task.target_entity_id);
                if (card != nullptr && status.value() == CardStatus::Poison && card->state.poison >= 2) {
                    queue.push_back(Task::destroy_card(task.actor_id, task.target_entity_id, "poison", task.source_entity_id));
                }
                dispatch_trigger(context, effects, task.type, "status_added", task.actor_id, task.source_entity_id, task.target_entity_id, task.amount, task.reason);
            }
            break;
        }
        case TaskType::ModifyCardCounter: {
            const auto updated = primitive_modify_card_counter(
                state,
                task.target_entity_id,
                task.counter_field,
                task.amount,
                task.counter_mode
            );
            if (!updated.has_value()) {
                result.status = ActionStatus::InvalidTarget;
                result.applied = false;
                result.message = "counter mutation target does not exist";
                queue.clear();
                break;
            }
            if (!task.reason.empty()) {
                emit(
                    result.events,
                    task.event_task_type == TaskType::Pass ? task.type : task.event_task_type,
                    task.reason,
                    task.actor_id,
                    task.source_entity_id,
                    task.target_entity_id,
                    *updated
                );
            }
            break;
        }
        case TaskType::PurifyCard: {
            const bool changed = primitive_purify_card(state, task.target_entity_id);
            emit(result.events, task.type, changed ? "card_purified" : "purify_ignored", task.actor_id, task.source_entity_id, task.target_entity_id);
            break;
        }
    }
}

}  // namespace gwent::kernel_detail
