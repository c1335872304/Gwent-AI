#include "kernel_internal.hpp"

namespace gwent::kernel_detail {

int config_int(const GameState& state, std::string_view key, int default_value) {
    const auto it = state.match_config.find(std::string(key));
    if (it == state.match_config.end()) {
        return default_value;
    }
    int value = default_value;
    const std::string& text = it->second;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return default_value;
    }
    return value;
}


int metadata_int(const CardDefinition& definition, std::string_view key, int default_value) {
    const auto it = definition.metadata.find(std::string(key));
    if (it == definition.metadata.end()) {
        return default_value;
    }
    int value = default_value;
    const std::string& text = it->second;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return default_value;
    }
    return value;
}


constexpr std::uint64_t kFastRngMask = 0xffffffffffffffffull;

std::uint64_t fast_rng_splitmix64(std::uint64_t x) {
    x = (x + 0x9E3779B97F4A7C15ull) & kFastRngMask;
    std::uint64_t z = x;
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull) & kFastRngMask;
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EBull) & kFastRngMask;
    return (z ^ (z >> 31)) & kFastRngMask;
}

std::uint64_t fast_rng_next_u64(GameState& state) {
    if (state.rng_state == 0) {
        state.rng_state = fast_rng_splitmix64(0);
        if (state.rng_state == 0) {
            state.rng_state = 0xA5A5A5A5A5A5A5A5ull;
        }
    }
    std::uint64_t x = state.rng_state;
    x ^= (x >> 12) & kFastRngMask;
    x ^= (x << 25) & kFastRngMask;
    x ^= (x >> 27) & kFastRngMask;
    state.rng_state = x & kFastRngMask;
    return (x * 0x2545F4914F6CDD1Dull) & kFastRngMask;
}

std::size_t fast_rng_index(GameState& state, std::size_t size) {
    if (size == 0) {
        throw std::invalid_argument("fast_rng_index requires size > 0");
    }
    const std::uint64_t n = static_cast<std::uint64_t>(size);
    const std::uint64_t limit = (kFastRngMask / n) * n;
    while (true) {
        const std::uint64_t x = fast_rng_next_u64(state);
        if (x < limit) {
            return static_cast<std::size_t>(x % n);
        }
    }
}

bool config_bool(const GameState& state, std::string_view key, bool default_value) {
    const auto it = state.match_config.find(std::string(key));
    if (it == state.match_config.end()) {
        return default_value;
    }
    return it->second == "true" || it->second == "1" || it->second == "yes" || it->second == "on";
}

std::vector<EntityId> battle_row_snapshot(const GameState& state, PlayerId player_id) {
    std::vector<EntityId> ids;
    const PlayerState& player = state.player(player_id);
    ids.reserve(player.row(Zone::Melee).size() + player.row(Zone::Ranged).size());
    for (Zone zone : kBattleZones) {
        for (EntityId entity_id : player.row(zone)) {
            ids.push_back(entity_id);
        }
    }
    return ids;
}

bool order_source_has_deferred_consumption(const RuntimeCard& card) {
    return card.runtime.order_pending_consume;
}

std::vector<EntityId> pending_order_sources_for_player(const GameState& state, PlayerId player_id) {
    std::vector<EntityId> ids;
    for (EntityId entity_id : battle_row_snapshot(state, player_id)) {
        const RuntimeCard* card = state.find_card(entity_id);
        if (card != nullptr && card->controller_id == player_id && order_source_has_deferred_consumption(*card)) {
            ids.push_back(entity_id);
        }
    }
    return ids;
}

bool has_post_play_pending_end(const GameState& state, PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        return false;
    }
    return state.turn_contexts[static_cast<std::size_t>(player_id)].consumed_hand_card;
}

void mark_post_play_pending_end(GameState& state, PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        return;
    }
    state.turn_contexts[static_cast<std::size_t>(player_id)].consumed_hand_card = true;
}

void clear_post_play_pending_end(GameState& state, PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        return;
    }
    TurnContext& ctx = state.turn_contexts[static_cast<std::size_t>(player_id)];
    ctx.consumed_hand_card = false;
    ctx.played_card_this_turn = false;
    ctx.discarded_card_this_turn = false;
    ctx.opponent_rows_frosted_this_turn.fill(false);
}

void mark_no_pass_after_intra_turn_action(GameState& state, PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        return;
    }
    state.turn_contexts[static_cast<std::size_t>(player_id)].used_intra_turn_action = true;
}

void clear_no_pass_after_intra_turn_action(GameState& state, PlayerId player_id) {
    if (!is_valid_player_id(player_id)) {
        return;
    }
    state.turn_contexts[static_cast<std::size_t>(player_id)].used_intra_turn_action = false;
}

bool source_is_leader_or_order(const GameState& state, EntityId source_id) {
    const RuntimeCard* card = state.find_card(source_id);
    if (card == nullptr) {
        return false;
    }
    if (card->definition->card_type == CardType::Leader || card->definition->card_type == CardType::Stratagem) {
        return true;
    }
    const auto it = card->definition->metadata.find("order");
    const bool metadata_order = it != card->definition->metadata.end() && (it->second == "true" || it->second == "1" || it->second == "yes" || it->second == "on");
    return card->definition->has_effect("order") || metadata_order;
}

bool has_pending_order_consumption(const GameState& state, PlayerId player_id) {
    return !pending_order_sources_for_player(state, player_id).empty();
}

void enqueue_pending_order_consumption(TaskQueue& queue, const GameState& state, PlayerId player_id) {
    for (EntityId source_id : pending_order_sources_for_player(state, player_id)) {
        queue.push_back(Task::consume_order_source(player_id, source_id));
    }
}

void tick_turn_counters(KernelContext& context, PlayerId player_id) {
    std::vector<Task> mutations;
    for (EntityId entity_id : battle_row_snapshot(context.state, player_id)) {
        RuntimeCard* card = context.state.find_card(entity_id);
        if (card == nullptr || card->controller_id != player_id) {
            continue;
        }

        card->runtime.entered_this_turn = false;
        if (card->state.locked || card->state.deploying) {
            continue;
        }
        if (card->state.timer > 0) {
            mutations.push_back(Task::modify_card_counter(
                player_id, entity_id, CardCounterField::Timer, -1,
                CardCounterMode::Add, entity_id, TaskType::TickTurnCounters, "timer_ticked"
            ));
        }
        if (card->state.cooldown > 0) {
            mutations.push_back(Task::modify_card_counter(
                player_id, entity_id, CardCounterField::Cooldown, -1,
                CardCounterMode::Add, entity_id, TaskType::TickTurnCounters, "cooldown_ticked"
            ));
        }
        // Countdown is a per-effect quota and is mutated synchronously by card text.
    }

    // Mutations execute before row effects / turn_start. Reverse push_front keeps
    // the historical EntityId and timer-before-cooldown event order unchanged.
    for (auto it = mutations.rbegin(); it != mutations.rend(); ++it) {
        context.queue.push_front(std::move(*it));
    }
}

void tick_turn_statuses(KernelContext& context, PlayerId player_id) {
    for (EntityId entity_id : battle_row_snapshot(context.state, player_id)) {
        RuntimeCard* card = context.state.find_card(entity_id);
        if (card == nullptr || card->controller_id != player_id || card->state.deploying) {
            continue;
        }

        if (card->state.bleeding > 0 && card->state.vitality > 0) {
            const int cancelled = std::min(card->state.bleeding, card->state.vitality);
            (void)primitive_modify_card_counter(context.state, entity_id, CardCounterField::Bleeding, -cancelled);
            (void)primitive_modify_card_counter(context.state, entity_id, CardCounterField::Vitality, -cancelled);
            context.emit(EventRecord{TaskType::TickTurnStatuses, "bleeding_vitality_offset", player_id, entity_id, entity_id, cancelled, {}});
        }

        if (card->state.bleeding > 0) {
            (void)primitive_modify_card_counter(context.state, entity_id, CardCounterField::Bleeding, -1);
            context.emit(EventRecord{TaskType::TickTurnStatuses, "bleeding_ticked", player_id, entity_id, entity_id, 1, {}});
            context.enqueue(Task::damage_card(player_id, entity_id, 1, entity_id, "bleeding"));
        } else if (card->state.vitality > 0) {
            (void)primitive_modify_card_counter(context.state, entity_id, CardCounterField::Vitality, -1);
            context.emit(EventRecord{TaskType::TickTurnStatuses, "vitality_ticked", player_id, entity_id, entity_id, 1, {}});
            context.enqueue(Task::boost_card(player_id, entity_id, 1, entity_id));
        }

        if (card->state.rupture) {
            card->state.rupture = false;
            const int damage = std::max(0, card->state.base_power);
            context.emit(EventRecord{TaskType::TickTurnStatuses, "rupture_ticked", player_id, entity_id, entity_id, damage, {}});
            if (damage > 0) {
                context.enqueue(Task::damage_card(player_id, entity_id, damage, entity_id));
            }
        }
    }
}


void tick_blood_moon_row_effect(KernelContext& context, PlayerId trigger_player_id, PlayerId row_side, Zone zone, RowEffect& effect) {
    const int configured_trigger = row_effect_int(effect, "trigger_player_id", row_side);
    if (configured_trigger != trigger_player_id) {
        return;
    }

    const std::vector<EntityId>& row = context.state.player(row_side).row(zone);
    std::vector<EntityId> candidates;
    candidates.reserve(row.size());
    for (EntityId entity_id : row) {
        const RuntimeCard* candidate = context.state.find_card(entity_id);
        if (candidate != nullptr && candidate->has_power() && !candidate->state.deploying) {
            candidates.push_back(entity_id);
        }
    }

    if (!candidates.empty()) {
        const EntityId target_id = candidates[fast_rng_index(context.state, candidates.size())];
        const RuntimeCard* target = context.state.find_card(target_id);
        if (target != nullptr) {
            if (target->state.bleeding > 0) {
                const int damage = std::max(0, row_effect_int(effect, "damage", 2));
                if (damage > 0) {
                    context.enqueue(Task::damage_card(trigger_player_id, target_id, damage, kInvalidEntityId));
                }
                context.emit(EventRecord{TaskType::DispatchTurnEnd, "blood_moon_damage", trigger_player_id, kInvalidEntityId, target_id, damage, {}});
            } else {
                const int bleeding_turns = std::max(0, row_effect_int(effect, "bleeding_turns", 2));
                if (bleeding_turns > 0) {
                    context.enqueue(Task::add_status_card(trigger_player_id, target_id, CardStatus::Bleeding, bleeding_turns, kInvalidEntityId));
                }
                context.emit(EventRecord{TaskType::DispatchTurnEnd, "blood_moon_bleeding", trigger_player_id, kInvalidEntityId, target_id, bleeding_turns, {}});
            }
        }
    }

    const int duration = std::max(0, row_effect_int(effect, "duration", 0) - 1);
    row_effect_set_int(effect, "duration", duration);
    context.emit(EventRecord{TaskType::DispatchTurnEnd, "row_effect_duration_changed", trigger_player_id, kInvalidEntityId, kInvalidEntityId, duration, effect.id});
}

void tick_frost_row_effect(KernelContext& context, PlayerId trigger_player_id, PlayerId row_side, Zone zone, RowEffect& effect) {
    const int configured_trigger = row_effect_int(effect, "trigger_player_id", row_side);
    if (configured_trigger != trigger_player_id) {
        return;
    }

    int highest_power = 0;
    std::vector<EntityId> candidates;
    for (EntityId entity_id : context.state.player(row_side).row(zone)) {
        const RuntimeCard* candidate = context.state.find_card(entity_id);
        if (candidate == nullptr || !candidate->has_power() || candidate->state.deploying) {
            continue;
        }
        if (candidates.empty() || candidate->state.power > highest_power) {
            highest_power = candidate->state.power;
            candidates.clear();
            candidates.push_back(entity_id);
        } else if (candidate->state.power == highest_power) {
            candidates.push_back(entity_id);
        }
    }

    if (!candidates.empty()) {
        const EntityId target_id = candidates[fast_rng_index(context.state, candidates.size())];
        int bonus = 0;
        const PlayerId frost_owner = context.state.opponent_id(row_side);
        int own_highest = 0;
        int enemy_highest = 0;
        for (Zone battle_zone : kBattleZones) {
            for (EntityId id : context.state.player(frost_owner).row(battle_zone)) {
                const RuntimeCard* card = context.state.find_card(id);
                if (card == nullptr || !card->has_power()) continue;
                own_highest = std::max(own_highest, card->state.power);
                const auto marker = card->definition->metadata.find("frost_damage_bonus_dominance");
                if (!card->state.locked && marker != card->definition->metadata.end()) {
                    // Each active Eredin contributes independently. Multiple
                    // copies therefore stack (+1 Frost damage each) as long as
                    // their controller has Dominance.
                    bonus += std::max(0, metadata_int(*card->definition, "frost_damage_bonus_dominance", 0));
                }
            }
            for (EntityId id : context.state.player(row_side).row(battle_zone)) {
                const RuntimeCard* card = context.state.find_card(id);
                if (card != nullptr && card->has_power()) enemy_highest = std::max(enemy_highest, card->state.power);
            }
        }
        // Keep Eredin's Dominance check aligned with has_dominance(): ties
        // count as Dominance when the owner's strongest unit is non-zero.
        if (own_highest <= 0 || own_highest < enemy_highest) bonus = 0;
        const int damage = std::max(0, row_effect_int(effect, "damage", 2) + bonus);
        if (damage > 0) {
            Task frost_damage = Task::damage_card(trigger_player_id, target_id, damage, kInvalidEntityId);
            frost_damage.reason = "frost";
            context.enqueue(std::move(frost_damage));
        }
        context.emit(EventRecord{TaskType::DispatchTurnStartRowEffects, "frost_damage", trigger_player_id, kInvalidEntityId, target_id, damage, {}});
    }

    const int duration = std::max(0, row_effect_int(effect, "duration", 0) - 1);
    row_effect_set_int(effect, "duration", duration);
    context.emit(EventRecord{TaskType::DispatchTurnStartRowEffects, "row_effect_duration_changed", trigger_player_id, kInvalidEntityId, kInvalidEntityId, duration, effect.id});
}

void dispatch_turn_start_row_effects(KernelContext& context, PlayerId trigger_player_id) {
    const PlayerId enemy = context.state.opponent_id(trigger_player_id);
    for (RuntimeCard& card : context.state.cards) {
        if (card.owner_id != enemy) continue;
        const auto marker = card.definition->metadata.find("tracks_frost_damage_history");
        if (marker == card.definition->metadata.end() || marker->second != "true") continue;
        for (int index = 0; index < 4; ++index) {
            const std::string next_key = "frost_damage_" + std::to_string(index + 1);
            card.memory["frost_damage_" + std::to_string(index)] = card.memory[next_key];
        }
        card.memory["frost_damage_4"] = "0";
    }
    const std::array<std::pair<PlayerId, Zone>, 4> order{{
        {trigger_player_id, Zone::Melee},
        {enemy, Zone::Melee},
        {trigger_player_id, Zone::Ranged},
        {enemy, Zone::Ranged},
    }};

    for (const auto& [side, zone] : order) {
        auto& effects = context.state.player(side).effects_on_row(zone);
        for (RowEffect& effect : effects) {
            if (effect.id == "blood_moon") {
                tick_blood_moon_row_effect(context, trigger_player_id, side, zone, effect);
            } else if (effect.id == "frost") {
                tick_frost_row_effect(context, trigger_player_id, side, zone, effect);
            }
        }
        effects.erase(std::remove_if(effects.begin(), effects.end(), [](const RowEffect& effect) {
            return row_effect_int(effect, "duration", 0) <= 0;
        }), effects.end());
    }
}

void banish_round_one_stratagems(KernelContext& context) {
    if (context.state.round_no != 1) {
        return;
    }
    for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
        std::vector<EntityId> candidates = context.state.player(player_id).stay;
        const auto board = battle_row_snapshot(context.state, player_id);
        candidates.insert(candidates.end(), board.begin(), board.end());
        for (EntityId entity_id : candidates) {
            RuntimeCard* card = context.state.find_card(entity_id);
            if (card == nullptr || card->definition->card_type != CardType::Stratagem) {
                continue;
            }
            primitive_move_card(context.state, entity_id, Location{card->owner_id, Zone::Banished, context.state.player(card->owner_id).banished.size()});
            context.emit(EventRecord{TaskType::RoundCleanup, "round_one_stratagem_banished", player_id, entity_id, entity_id, 0, {}});
        }
    }
}

void normalize_resilient_unit_for_next_round(RuntimeCard& card) noexcept {
    card.state.resilience = false;

    // Resilience keeps the unit on the battlefield, but it must not carry boosts
    // into the next round. Damage is preserved: a unit at or below its base
    // power keeps that current power rather than being healed back to base.
    if (card.state.power > card.state.base_power) {
        card.state.power = card.state.base_power;
    }

    card.state.armor = std::max(0, card.definition->base_armor);
}

void cleanup_board_after_round(KernelContext& context) {
    banish_round_one_stratagems(context);

    for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
        PlayerState& player = context.state.player(player_id);
        for (Zone zone : kBattleZones) {
            const std::vector<EntityId> row_cards = player.row(zone);
            for (EntityId entity_id : row_cards) {
                RuntimeCard* card = context.state.find_card(entity_id);
                if (card == nullptr) {
                    continue;
                }
                const auto location = context.state.location_of(entity_id);
                if (!location.has_value() || location->side != player_id || location->zone != zone) {
                    continue;
                }
                if (card->state.resilience) {
                    normalize_resilient_unit_for_next_round(*card);
                    context.emit(EventRecord{TaskType::RoundCleanup, "resilience_preserved", player_id, entity_id, entity_id, 0, {}});
                    continue;
                }

                const Zone destination = card->state.doomed ? Zone::Banished : Zone::Cemetery;
                const PlayerId owner = card->owner_id;
                primitive_move_card(context.state, entity_id, Location{owner, destination, context.state.player(owner).zone_size(destination)});
                context.emit(EventRecord{TaskType::RoundCleanup, destination == Zone::Banished ? "round_card_banished" : "round_card_moved_to_cemetery", player_id, entity_id, entity_id, 0, {}});
            }
        }

        const std::vector<EntityId> cemetery_cards = player.cemetery;
        for (auto it = cemetery_cards.rbegin(); it != cemetery_cards.rend(); ++it) {
            RuntimeCard* card = context.state.find_card(*it);
            if (card == nullptr || card->owner_id != player_id) continue;
            const auto echo = card->definition->metadata.find("echo");
            if (echo == card->definition->metadata.end()
                || (echo->second != "true" && echo->second != "1" && echo->second != "yes" && echo->second != "on")) continue;
            if (primitive_move_card(context.state, *it, Location{player_id, Zone::Deck, 0})) {
                context.emit(EventRecord{TaskType::RoundCleanup, "echo_returned_to_deck_top", player_id, *it, *it, 0, card->definition->id});
            }
        }

        player.coins /= 2;
        for (Zone zone : kBattleZones) {
            player.effects_on_row(zone).clear();
        }
        player.passed = false;
        player.pass_reason = PassReason::None;
    }
}

void prepare_next_round_from_state(KernelContext& context, std::optional<PlayerId> winner_id) {
    const int next_round_no = context.state.round_no + 1;
    const PlayerId starter = winner_id.has_value()
        ? winner_id.value()
        : context.state.opponent_id(context.state.round_starting_player_id);

    const bool standard = config_bool(context.state, "standard_deck_game", false);
    const int hand_limit = std::max(0, config_int(context.state, "hand_limit", 10));
    const int wanted = std::max(0, config_int(context.state, "between_round_draw", 3));
    const int base_mulligans = std::max(0, config_int(context.state, "round_mulligan_base", 2));

    if (standard) {
        for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
            PlayerState& player = context.state.player(player_id);
            int drawn = 0;
            for (int i = 0; i < wanted; ++i) {
                if (player.deck.empty() || static_cast<int>(player.hand.size()) >= hand_limit) {
                    break;
                }
                const EntityId entity_id = player.deck.front();
                primitive_move_card(context.state, entity_id, Location{player_id, Zone::Hand, player.hand.size()});
                ++drawn;
                context.emit(EventRecord{TaskType::RoundCleanup, "round_card_drawn", player_id, entity_id, entity_id, 0, {}});
            }
            const int extra_mulligans = std::max(0, wanted - drawn);
            player.cards_drawn_this_round = drawn;
            player.mulligans_available = base_mulligans + extra_mulligans;
            context.emit(EventRecord{TaskType::RoundCleanup, "round_hand_prepared", player_id, kInvalidEntityId, kInvalidEntityId, player.mulligans_available, {}});
        }
    }

    context.state.round_no = next_round_no;
    context.state.turn_no = 1;
    context.state.current_player_id = starter;
    context.state.round_starting_player_id = starter;
    context.state.mulligan_player_id = starter;
    context.state.phase = MatchPhase::Playing;
    context.state.engine_phase = EnginePhase::ActionWindow;
    if (context.state.status != MatchStatus::Finished) {
        context.state.status = MatchStatus::Running;
    }
    context.state.turn_contexts = {};
    for (auto& modifiers : context.state.turn_rule_modifiers) {
        modifiers.clear();
    }
    context.emit(EventRecord{TaskType::RoundCleanup, "next_round_prepared", starter, kInvalidEntityId, kInvalidEntityId, next_round_no, {}});
}

void finish_round_or_advance_turn(
    KernelContext& context,
    KernelResult& result,
    const EffectRegistry& effects,
    TaskQueue& queue,
    PlayerId actor_id
) {
    // ApplyTurnTransition runs only after DispatchTurnEnd has finished.  Clear
    // turn-scoped decision/event memory here (not in EndTurn), so turn_end
    // passives can still inspect everything that happened during the turn.
    const PlayerId previous = context.state.current_player_id;
    clear_post_play_pending_end(context.state, previous);
    clear_no_pass_after_intra_turn_action(context.state, previous);

    if (context.state.player(kPlayerZero).passed && context.state.player(kPlayerOne).passed) {
        const int score0 = context.state.board_score(kPlayerZero);
        const int score1 = context.state.board_score(kPlayerOne);
        std::optional<PlayerId> round_winner;
        if (score0 > score1) {
            round_winner = kPlayerZero;
            context.state.player(kPlayerZero).round_wins += 1;
            emit(result.events, TaskType::ApplyTurnTransition, "round_finished", actor_id, kInvalidEntityId, kInvalidEntityId, 0, "winner=0");
        } else if (score1 > score0) {
            round_winner = kPlayerOne;
            context.state.player(kPlayerOne).round_wins += 1;
            emit(result.events, TaskType::ApplyTurnTransition, "round_finished", actor_id, kInvalidEntityId, kInvalidEntityId, 0, "winner=1");
        } else {
            context.state.player(kPlayerZero).round_wins += 1;
            context.state.player(kPlayerOne).round_wins += 1;
            emit(result.events, TaskType::ApplyTurnTransition, "round_finished", actor_id, kInvalidEntityId, kInvalidEntityId, 0, "winner=tie");
        }

        // Persist the continuation as a task before dispatching round_end. If
        // any round-end trigger requests a choice, the task is captured in the
        // PendingChoice FIFO tail and executes after the trigger stack resumes.
        queue.push_front(Task::finalize_round_end(actor_id, round_winner));
        dispatch_trigger(context, effects, TaskType::ApplyTurnTransition, "round_end", actor_id, kInvalidEntityId, kInvalidEntityId, round_winner.value_or(kInvalidEntityId));
        return;
    }

    context.state.turn_rule_modifiers[previous].clear();
    const PlayerId opponent = context.state.opponent_id(previous);
    context.state.current_player_id = opponent;
    context.state.turn_no += 1;
    emit(result.events, TaskType::ApplyTurnTransition, context.state.player(opponent).passed ? "turn_advanced_to_passed_player" : "turn_advanced", actor_id);
    // Turn-start work is represented as explicit tasks rather than C++ call
    // stack continuation, so a choice inside a turn-start trigger cannot drop
    // the remaining transition steps.
    queue.push_front(Task::dispatch_turn_start(opponent));
    queue.push_front(Task::dispatch_turn_start_row_effects(opponent));
    queue.push_front(Task::tick_turn_counters(opponent));
}

void append_next_decision(GameState& state, KernelResult& result) {
    if (state.status == MatchStatus::Running && state.phase == MatchPhase::Playing) {
        result.next_decision = make_current_turn_decision(state);
    }
}

}  // namespace gwent::kernel_detail
