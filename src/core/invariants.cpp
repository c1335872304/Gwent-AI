#include "gwent/core/invariants.hpp"

#include "gwent/core/entity_index.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gwent {
namespace {

std::string location_to_string(const Location& loc) {
    std::ostringstream out;
    out << "p" << loc.side << ":" << to_string(loc.zone) << ":" << loc.index;
    return out.str();
}

void add_issue(
    InvariantReport& report,
    InvariantSeverity severity,
    std::string code,
    std::string message,
    std::optional<EntityId> entity_id = std::nullopt,
    std::optional<Location> location = std::nullopt
) {
    report.issues.push_back(InvariantIssue{
        severity,
        std::move(code),
        std::move(message),
        entity_id,
        location,
    });
}

const std::vector<EntityId>* zone_vector(const GameState& state, const Location& loc) {
    if (loc.side < 0 || loc.side >= kPlayerCount) {
        return nullptr;
    }
    const auto& p = state.players[static_cast<std::size_t>(loc.side)];
    switch (loc.zone) {
        case Zone::Deck: return &p.deck;
        case Zone::Hand: return &p.hand;
        case Zone::Stay: return &p.stay;
        case Zone::Melee: return &p.rows[0];
        case Zone::Ranged: return &p.rows[1];
        case Zone::Cemetery: return &p.cemetery;
        case Zone::Banished: return &p.banished;
        case Zone::Leader: return nullptr;
    }
    return nullptr;
}

bool is_on_board_or_leader(const Location& loc) noexcept {
    return is_battle_zone(loc.zone) || loc.zone == Zone::Leader;
}

bool parse_int_strict(const std::string& text, int& value) {
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool is_non_negative_int_field(const RowEffect& effect, std::string_view key) {
    const auto it = effect.data.find(std::string(key));
    if (it == effect.data.end()) {
        return false;
    }
    int value = 0;
    return parse_int_strict(it->second, value) && value >= 0;
}

}  // namespace

bool InvariantReport::ok() const noexcept {
    return error_count() == 0;
}

std::size_t InvariantReport::error_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const InvariantIssue& issue) {
        return issue.severity == InvariantSeverity::Error;
    }));
}

std::size_t InvariantReport::warning_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const InvariantIssue& issue) {
        return issue.severity == InvariantSeverity::Warning;
    }));
}

std::string InvariantReport::summary() const {
    std::ostringstream out;
    out << error_count() << " error(s), " << warning_count() << " warning(s)";
    return out.str();
}

InvariantReport validate_state_invariants(const GameState& state, const InvariantOptions& options) {
    PerformanceScope scope(options.performance_counters, PerformanceMetric::InvariantCheck);
    InvariantReport report;
    const EntityIndex index = build_entity_index(state, options.performance_counters);
    const auto& memberships = index.memberships;

    for (const auto& [entity_id, loc] : index.entries) {

        if (options.require_cards_for_all_locations && !state.find_card(entity_id)) {
            add_issue(
                report,
                InvariantSeverity::Error,
                "zone.card_missing",
                "zone contains an entity id that is absent from GameState::cards at " + location_to_string(loc),
                entity_id,
                loc
            );
            continue;
        }

        const auto* card = state.find_card(entity_id);
        if (card != nullptr) {
            if (options.require_leaders_only_in_leader_zone && card->definition->card_type == CardType::Leader && loc.zone != Zone::Leader) {
                add_issue(report, InvariantSeverity::Error, "leader.zone", "leader card exists outside the leader zone", entity_id, loc);
            }
            if (options.require_non_power_cards_are_powerless && !card->has_power()) {
                if (card->definition->base_power != 0 || card->state.base_power != 0 || card->state.power != 0) {
                    add_issue(report, InvariantSeverity::Error, "card.no_power_state", "non-unit card must not carry power or base-power state", entity_id, loc);
                }
            }
        }

        if (loc.zone == Zone::Leader) {
            if (options.require_leader_zone_type) {
                if (card && card->definition->card_type != CardType::Leader) {
                    add_issue(report, InvariantSeverity::Error, "leader.type", "leader zone contains a non-leader card", entity_id, loc);
                }
            }
            continue;
        }

        if (options.require_zone_index_matches_vector_index) {
            const auto* vec = zone_vector(state, loc);
            if (!vec || loc.index >= vec->size() || (*vec)[loc.index] != entity_id) {
                add_issue(report, InvariantSeverity::Error, "zone.index", "entity location index does not match its zone vector", entity_id, loc);
            }
        }

        if (options.require_battle_entity_types && is_battle_zone(loc.zone)) {
            if (card) {
                const auto type = card->definition->card_type;
                if (type != CardType::Unit && type != CardType::Artifact && type != CardType::Stratagem) {
                    add_issue(report, InvariantSeverity::Error, "battle.type", "battle row contains a card that cannot persist on a battle row", entity_id, loc);
                }
                if ((type == CardType::Artifact || type == CardType::Stratagem)
                    && (card->has_power() || card->definition->base_power != 0 || card->state.base_power != 0 || card->state.power != 0)) {
                    add_issue(report, InvariantSeverity::Error, "battle.no_power_entity", "artifact/stratagem must not have power or contribute score", entity_id, loc);
                }
            }
        }
    }

    if (options.require_single_zone_membership) {
        for (const auto& [entity_id, locs] : memberships) {
            if (locs.size() != 1) {
                add_issue(report, InvariantSeverity::Error, "entity.membership", "entity appears in more than one zone", entity_id, locs.empty() ? std::nullopt : std::optional<Location>{locs.front()});
            }
        }
    }

    if (options.require_locations_for_all_cards) {
        for (std::size_t i = 0; i < state.cards.size(); ++i) {
            const EntityId entity_id = static_cast<EntityId>(i);
            const auto membership_it = memberships.find(entity_id);
            const bool has_location = i < state.locations.size() && state.locations[i].has_value();
            if (membership_it == memberships.end()) {
                add_issue(report, InvariantSeverity::Error, "card.unplaced", "card exists but is not present in any player zone", entity_id);
            }
            if (!has_location) {
                add_issue(report, InvariantSeverity::Error, "location.missing", "card exists but has no GameState::locations entry", entity_id);
            }
        }
    }

    for (std::size_t i = 0; i < state.locations.size(); ++i) {
        if (!state.locations[i].has_value()) {
            continue;
        }
        const EntityId entity_id = static_cast<EntityId>(i);
        const Location& loc = *state.locations[i];
        const auto membership_it = memberships.find(entity_id);
        if (membership_it == memberships.end()) {
            add_issue(report, InvariantSeverity::Error, "location.stale", "location entry points to an entity that is not in any zone", entity_id, loc);
            continue;
        }
        if (membership_it->second.size() == 1 && !(membership_it->second.front() == loc)) {
            add_issue(report, InvariantSeverity::Error, "location.mismatch", "stored location does not match actual zone membership", entity_id, loc);
        }
    }

    if (options.require_pending_choices_are_well_formed && state.pending_choice.has_value()) {
        const auto& pending = *state.pending_choice;
        if (pending.kind == PendingChoiceKind::None) {
            add_issue(report, InvariantSeverity::Error, "pending.kind", "pending_choice is present but kind is None");
        }
        if (pending.player_id < 0 || pending.player_id >= kPlayerCount) {
            add_issue(report, InvariantSeverity::Error, "pending.player", "pending_choice has invalid player id");
        }
        if (pending.source_entity_id != kInvalidEntityId && !state.find_card(pending.source_entity_id)) {
            add_issue(report, InvariantSeverity::Error, "pending.source", "pending_choice source entity does not exist", pending.source_entity_id);
        }
        if (pending.resume_kind == PendingChoiceResumeKind::PlayCard) {
            const auto source_location = state.location_of(pending.source_entity_id);
            if (pending.kind != PendingChoiceKind::RowTarget
                && pending.kind != PendingChoiceKind::InsertPosition) {
                add_issue(report, InvariantSeverity::Error, "pending.play.kind", "PlayCard continuation must wait for a row or insert-position target", pending.source_entity_id);
            }
            const bool valid_play_source = source_location.has_value()
                && source_location->side == pending.player_id
                && (source_location->zone == Zone::Hand || source_location->zone == Zone::Stay);
            if (!valid_play_source) {
                add_issue(
                    report,
                    InvariantSeverity::Error,
                    "pending.play.source",
                    "PlayCard continuation source must remain in the acting player's hand or staging area",
                    pending.source_entity_id
                );
            }
            if (pending.origin_action_type != ActionType::PlayCard) {
                add_issue(report, InvariantSeverity::Error, "pending.play.origin", "PlayCard continuation must carry PlayCard as its origin action", pending.source_entity_id);
            }
        }
        if (pending.kind == PendingChoiceKind::CardTarget) {
            if (pending.legal_card_targets.empty()) {
                add_issue(report, InvariantSeverity::Error, "pending.card_targets.empty", "card target pending choice has no legal targets");
            }
            std::unordered_set<EntityId> seen_targets;
            for (const auto target_id : pending.legal_card_targets) {
                if (!state.find_card(target_id)) {
                    add_issue(report, InvariantSeverity::Error, "pending.card_target.missing", "pending legal card target does not exist", target_id);
                }
                if (!seen_targets.insert(target_id).second) {
                    add_issue(report, InvariantSeverity::Error, "pending.card_target.duplicate", "pending legal card target is duplicated", target_id);
                }
            }
        }
        for (const ContinuationStep& step : pending.continuation) {
            if (step.actor_id < 0 || step.actor_id >= kPlayerCount) {
                add_issue(report, InvariantSeverity::Error, "pending.continuation.actor", "pending continuation has invalid actor id");
            }
            if ((step.kind == ContinuationStepKind::ConsumeOrderSource
                    || step.kind == ContinuationStepKind::FinishSpecialCard
                    || step.kind == ContinuationStepKind::MarkIntraTurnFollowup
                    || step.kind == ContinuationStepKind::PlayStagedCard)
                && step.source_entity_id != kInvalidEntityId
                && !state.find_card(step.source_entity_id)) {
                add_issue(report, InvariantSeverity::Error, "pending.continuation.source", "pending continuation source entity does not exist", step.source_entity_id);
            }
        }
        if (pending.kind == PendingChoiceKind::RowTarget || pending.kind == PendingChoiceKind::InsertPosition) {
            if (pending.legal_row_targets.empty()) {
                add_issue(report, InvariantSeverity::Error, "pending.row_targets.empty", "row target pending choice has no legal rows");
            }
            std::unordered_set<int> seen_rows;
            for (const Location& row : pending.legal_row_targets) {
                if (row.side < 0 || row.side >= kPlayerCount || !is_battle_zone(row.zone)) {
                    add_issue(report, InvariantSeverity::Error, "pending.row_target.invalid", "pending legal row target is not a valid battle row", std::nullopt, row);
                    continue;
                }
                const int row_key = static_cast<int>(row.side) * 1000
                    + static_cast<int>(row.zone) * 100
                    + (pending.kind == PendingChoiceKind::InsertPosition ? static_cast<int>(row.index) : 0);
                if (!seen_rows.insert(row_key).second) {
                    add_issue(report, InvariantSeverity::Error, "pending.row_target.duplicate", "pending legal row target is duplicated", std::nullopt, row);
                }
            }
        }
    }

    if (options.require_row_effects_are_well_formed) {
        for (PlayerId pid = 0; pid < kPlayerCount; ++pid) {
            const auto& player = state.players[static_cast<std::size_t>(pid)];
            for (Zone zone : kBattleZones) {
                const auto& effects = player.effects_on_row(zone);
                for (const RowEffect& effect : effects) {
                    if (effect.id.empty()) {
                        add_issue(report, InvariantSeverity::Error, "row_effect.id", "row effect id must not be empty", std::nullopt, Location{pid, zone, 0});
                    }
                    if (!is_non_negative_int_field(effect, "duration")) {
                        add_issue(report, InvariantSeverity::Error, "row_effect.duration", "row effect duration must be a non-negative integer", std::nullopt, Location{pid, zone, 0});
                    }
                    const auto trigger_it = effect.data.find("trigger_player_id");
                    if (trigger_it != effect.data.end()) {
                        int trigger_player_id = -1;
                        if (!parse_int_strict(trigger_it->second, trigger_player_id) || trigger_player_id < 0 || trigger_player_id >= kPlayerCount) {
                            add_issue(report, InvariantSeverity::Error, "row_effect.trigger_player", "row effect trigger_player_id must be a valid player id", std::nullopt, Location{pid, zone, 0});
                        }
                    }
                }
            }
        }
    }

    if (options.require_turn_state_is_phase_consistent) {
        if (state.status == MatchStatus::Finished && state.phase != MatchPhase::Finished) {
            add_issue(report, InvariantSeverity::Error, "match.finished_phase", "finished match status must use Finished phase");
        }
        if (state.phase == MatchPhase::Playing && state.status != MatchStatus::Running) {
            add_issue(report, InvariantSeverity::Error, "match.playing_status", "Playing phase requires Running status");
        }
        if (state.status == MatchStatus::Running && !is_valid_player_id(state.current_player_id)) {
            add_issue(report, InvariantSeverity::Error, "turn.current_player", "running match must have a valid current player id");
        }
        if (state.mulligan_player_id.has_value()) {
            if (state.status != MatchStatus::Running || state.phase != MatchPhase::Playing) {
                add_issue(report, InvariantSeverity::Error, "mulligan.phase", "active mulligan requires a running match in Playing phase");
            }
            if (!is_valid_player_id(*state.mulligan_player_id)) {
                add_issue(report, InvariantSeverity::Error, "mulligan.player", "mulligan_player_id must be a valid player id");
            }
            if (state.pending_choice.has_value()) {
                add_issue(report, InvariantSeverity::Error, "mulligan.pending_choice", "mulligan phase cannot coexist with a pending target choice");
            }
        }
        if (state.status == MatchStatus::Finished && state.pending_choice.has_value()) {
            add_issue(report, InvariantSeverity::Error, "match.finished_pending", "finished match must not keep a pending choice");
        }
    }


    if (options.require_engine_phase_consistent) {
        if (state.status == MatchStatus::Finished || state.phase == MatchPhase::Finished) {
            if (state.engine_phase != EnginePhase::Finished) {
                add_issue(report, InvariantSeverity::Error, "engine.finished_phase", "finished match must expose Finished engine phase");
            }
        } else if (state.pending_choice.has_value()) {
            if (state.engine_phase != EnginePhase::AwaitingChoice) {
                add_issue(report, InvariantSeverity::Error, "engine.choice_phase", "pending choice requires AwaitingChoice engine phase");
            }
        } else if (state.status == MatchStatus::Running && state.phase == MatchPhase::Playing) {
            // Invariants run at stable player-facing boundaries. Resolving/
            // TurnClosing/RoundClosing are transient kernel phases and must not
            // leak out after an action completes.
            if (state.engine_phase != EnginePhase::ActionWindow) {
                add_issue(report, InvariantSeverity::Error, "engine.action_window", "stable running state without a pending choice must expose ActionWindow engine phase");
            }
        } else if (state.engine_phase != EnginePhase::NotStarted) {
            add_issue(report, InvariantSeverity::Error, "engine.not_started", "non-running non-finished state must expose NotStarted engine phase");
        }
    }

    if (options.require_turn_context_consistent) {
        for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
            const TurnContext& turn = state.turn_contexts[static_cast<std::size_t>(player_id)];
            if (!turn.consumed_hand_card && (turn.played_card_this_turn || turn.discarded_card_this_turn)) {
                add_issue(report, InvariantSeverity::Error, "turn.consume_flags", "played/discarded marker requires consumed_hand_card");
            }
            if (turn.played_card_this_turn && turn.discarded_card_this_turn) {
                add_issue(report, InvariantSeverity::Error, "turn.double_consumption", "one action window cannot both play and discard its mandatory hand card");
            }
            if (turn.consumed_hand_card && !turn.played_card_this_turn && !turn.discarded_card_this_turn) {
                add_issue(report, InvariantSeverity::Error, "turn.consume_origin", "consumed_hand_card must record whether the card was played or discarded");
            }
            if (turn.used_intra_turn_action && !turn.consumed_hand_card && state.player(player_id).hand.empty()) {
                add_issue(report, InvariantSeverity::Error, "turn.dead_end", "leader/order was used but no hand card remains to satisfy mandatory consumption");
            }
            if (state.player(player_id).passed
                && (turn.used_intra_turn_action || turn.consumed_hand_card || turn.played_card_this_turn || turn.discarded_card_this_turn
                    || std::any_of(turn.opponent_rows_frosted_this_turn.begin(), turn.opponent_rows_frosted_this_turn.end(), [](bool value) { return value; }))) {
                add_issue(report, InvariantSeverity::Error, "turn.passed_context", "passed player must not retain active TurnContext state");
            }
        }
    }

    if (options.require_pass_reason_consistent) {
        for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
            const PlayerState& player = state.player(player_id);
            if (player.passed && player.pass_reason == PassReason::None) {
                add_issue(report, InvariantSeverity::Error, "pass.reason_missing", "passed player must record a pass reason");
            }
            if (!player.passed && player.pass_reason != PassReason::None) {
                add_issue(report, InvariantSeverity::Error, "pass.reason_stale", "non-passed player must not retain a pass reason");
            }
            if (player.pass_reason == PassReason::HandExhausted && !player.hand.empty()) {
                add_issue(report, InvariantSeverity::Error, "pass.hand_exhausted_nonempty", "HAND_EXHAUSTED pass requires an empty hand");
            }
        }
    }

    if (options.require_listener_sources_are_valid) {
        for (const auto& [listener_id, listener] : state.listeners) {
            (void)listener_id;
            if (!listener.source_id.has_value()) {
                if (listener.require_source) {
                    add_issue(report, InvariantSeverity::Error, "listener.source.none", "listener requires a source but source_id is missing");
                }
                continue;
            }
            const auto* source_card = state.find_card(*listener.source_id);
            if (!source_card) {
                if (listener.require_source) {
                    add_issue(report, InvariantSeverity::Error, "listener.source.missing", "listener source entity does not exist", *listener.source_id);
                }
                continue;
            }
            const auto source_loc = state.location_of(*listener.source_id);
            if (source_loc.has_value() && source_loc->zone == Zone::Banished) {
                add_issue(report, InvariantSeverity::Error, "listener.source.banished", "banished entity must not keep an active listener", *listener.source_id, source_loc);
            }
            if (listener.source_must_be_on_board) {
                if (!source_loc.has_value() || !is_on_board_or_leader(*source_loc)) {
                    add_issue(report, InvariantSeverity::Error, "listener.source.zone", "listener source is not currently on board or leader zone", *listener.source_id, source_loc);
                }
            }
        }
    }

    return report;
}

std::string invariant_report_to_string(const InvariantReport& report) {
    std::ostringstream out;
    out << report.summary();
    for (const auto& issue : report.issues) {
        out << "\n[" << (issue.severity == InvariantSeverity::Error ? "error" : "warning") << "] " << issue.code << ": " << issue.message;
        if (issue.entity_id.has_value()) {
            out << " entity=" << *issue.entity_id;
        }
        if (issue.location.has_value()) {
            out << " loc=" << location_to_string(*issue.location);
        }
    }
    return out.str();
}

}  // namespace gwent
