#include "gwent/trace/state_snapshot.hpp"

#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace gwent::trace {
namespace {

std::string indent(int level) {
    return std::string(static_cast<std::size_t>(level) * 2U, ' ');
}

void write_indent(std::ostringstream& out, const SnapshotOptions& options, int level) {
    if (options.pretty) {
        out << indent(level);
    }
}

void write_newline(std::ostringstream& out, const SnapshotOptions& options) {
    if (options.pretty) {
        out << '\n';
    }
}

void write_sep(std::ostringstream& out, const SnapshotOptions& options) {
    out << ',';
    write_newline(out, options);
}

std::string json_escape(std::string_view value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20U) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch));
                } else {
                    out << ch;
                }
        }
    }
    return out.str();
}

void write_string(std::ostringstream& out, std::string_view value) {
    out << '"' << json_escape(value) << '"';
}

void write_key(std::ostringstream& out, const SnapshotOptions& options, int level, std::string_view key) {
    write_indent(out, options, level);
    write_string(out, key);
    out << (options.pretty ? ": " : ":");
}

std::string optional_player_to_json(std::optional<PlayerId> value) {
    if (!value.has_value()) {
        return "null";
    }
    return std::to_string(*value);
}

bool has_status(const RuntimeCard& card, CardStatus status) {
    switch (status) {
        case CardStatus::Shield: return card.state.shield;
        case CardStatus::Infiltration: return card.state.infiltration;
        case CardStatus::Spying: return card.state.spying;
        case CardStatus::Locked: return card.state.locked;
        case CardStatus::Resilience: return card.state.resilience;
        case CardStatus::Doomed: return card.state.doomed;
        case CardStatus::Veil: return card.state.veil;
        case CardStatus::Bounty: return card.state.bounty;
        case CardStatus::Immune: return card.state.immune;
        case CardStatus::Defender: return card.state.defender;
        case CardStatus::Rupture: return card.state.rupture;
        case CardStatus::Poison: return card.state.poison > 0;
        case CardStatus::Bleeding: return card.state.bleeding > 0;
        case CardStatus::Vitality: return card.state.vitality > 0;
    }
    return false;
}


std::string_view snapshot_status_name(CardStatus status) noexcept {
    switch (status) {
        case CardStatus::Shield: return "Shield";
        case CardStatus::Infiltration: return "Infiltration";
        case CardStatus::Spying: return "Spying";
        case CardStatus::Locked: return "Locked";
        case CardStatus::Resilience: return "Resilience";
        case CardStatus::Doomed: return "Doomed";
        case CardStatus::Veil: return "Veil";
        case CardStatus::Poison: return "Poison";
        case CardStatus::Bleeding: return "Bleeding";
        case CardStatus::Vitality: return "Vitality";
        case CardStatus::Bounty: return "Bounty";
        case CardStatus::Immune: return "Immune";
        case CardStatus::Defender: return "Defender";
        case CardStatus::Rupture: return "Rupture";
    }
    return "Unknown";
}

int status_amount(const RuntimeCard& card, CardStatus status) {
    switch (status) {
        case CardStatus::Poison: return card.state.poison;
        case CardStatus::Bleeding: return card.state.bleeding;
        case CardStatus::Vitality: return card.state.vitality;
        default: return has_status(card, status) ? 1 : 0;
    }
}

void write_statuses(std::ostringstream& out, const RuntimeCard& card, const SnapshotOptions& options, int level) {
    out << '{';
    write_newline(out, options);
    bool first = true;
    constexpr CardStatus statuses[] = {
        CardStatus::Shield, CardStatus::Infiltration, CardStatus::Spying, CardStatus::Locked,
        CardStatus::Resilience, CardStatus::Doomed, CardStatus::Veil, CardStatus::Poison,
        CardStatus::Bleeding, CardStatus::Vitality, CardStatus::Bounty, CardStatus::Immune,
        CardStatus::Defender, CardStatus::Rupture,
    };
    for (CardStatus status : statuses) {
        const int amount = status_amount(card, status);
        if (amount <= 0) {
            continue;
        }
        if (!first) {
            write_sep(out, options);
        }
        first = false;
        write_key(out, options, level + 1, snapshot_status_name(status));
        out << amount;
    }
    write_newline(out, options);
    write_indent(out, options, level);
    out << '}';
}

void write_memory_object(std::ostringstream& out, const RuntimeMemory& data, const SnapshotOptions& options, int level) {
    out << '{';
    write_newline(out, options);
    std::vector<std::string> keys;
    keys.reserve(data.size());
    for (const auto& [key, _] : data) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        write_key(out, options, level + 1, keys[i]);
        write_string(out, data.at(keys[i]));
        if (i + 1 < keys.size()) {
            write_sep(out, options);
        }
    }
    write_newline(out, options);
    write_indent(out, options, level);
    out << '}';
}

void write_card_ref(std::ostringstream& out, const GameState& state, EntityId entity_id, const SnapshotOptions& options, int level) {
    const RuntimeCard* card = state.find_card(entity_id);
    out << '{';
    write_newline(out, options);

    bool wrote = false;
    auto field_sep = [&]() {
        if (wrote) {
            write_sep(out, options);
        }
        wrote = true;
    };

    if (options.include_entity_ids) {
        field_sep();
        write_key(out, options, level + 1, "entity_id");
        out << entity_id;
    }

    field_sep();
    write_key(out, options, level + 1, "exists");
    out << (card != nullptr ? "true" : "false");

    if (card != nullptr) {
        field_sep();
        write_key(out, options, level + 1, "card_id");
        write_string(out, card->definition->id);

        if (options.include_card_names) {
            field_sep();
            write_key(out, options, level + 1, "name");
            write_string(out, card->definition->name);
        }

        field_sep();
        write_key(out, options, level + 1, "type");
        write_string(out, to_string(card->definition->card_type));

        field_sep();
        write_key(out, options, level + 1, "owner");
        out << card->owner_id;

        field_sep();
        write_key(out, options, level + 1, "controller");
        out << card->controller_id;

        field_sep();
        write_key(out, options, level + 1, "has_power");
        out << (card->has_power() ? "true" : "false");

        field_sep();
        write_key(out, options, level + 1, "base_power");
        out << card->state.base_power;

        field_sep();
        write_key(out, options, level + 1, "power");
        out << card->state.power;

        field_sep();
        write_key(out, options, level + 1, "armor");
        out << card->state.armor;

        field_sep();
        write_key(out, options, level + 1, "order_charges");
        out << card->state.order_charges;

        field_sep();
        write_key(out, options, level + 1, "timer");
        out << card->state.timer;

        field_sep();
        write_key(out, options, level + 1, "cooldown");
        out << card->state.cooldown;

        field_sep();
        write_key(out, options, level + 1, "countdown");
        out << card->state.countdown;

        field_sep();
        write_key(out, options, level + 1, "statuses");
        write_statuses(out, *card, options, level + 1);

        // Preserve the historical snapshot/trace contract while the hot
        // state-machine flags are stored as typed fields at runtime.
        auto snapshot_memory = card->memory;
        if (card->runtime.entered_this_turn) snapshot_memory["entered_this_turn"] = "true";
        if (card->runtime.order_used) snapshot_memory["order_used"] = "true";
        if (card->runtime.order_pending_consume) snapshot_memory["order_pending_consume"] = "true";
        if (!snapshot_memory.empty()) {
            field_sep();
            write_key(out, options, level + 1, "memory");
            write_memory_object(out, snapshot_memory, options, level + 1);
        }
    }

    write_newline(out, options);
    write_indent(out, options, level);
    out << '}';
}

void write_zone_cards(std::ostringstream& out, const GameState& state, const std::vector<EntityId>& cards, const SnapshotOptions& options, int level) {
    out << '[';
    write_newline(out, options);
    for (std::size_t i = 0; i < cards.size(); ++i) {
        write_indent(out, options, level + 1);
        write_card_ref(out, state, cards[i], options, level + 1);
        if (i + 1 < cards.size()) {
            write_sep(out, options);
        }
    }
    write_newline(out, options);
    write_indent(out, options, level);
    out << ']';
}

void write_player(std::ostringstream& out, const GameState& state, PlayerId player_id, const SnapshotOptions& options, int level) {
    const auto& player = state.player(player_id);
    out << '{';
    write_newline(out, options);

    write_key(out, options, level + 1, "player_id"); out << player_id; write_sep(out, options);
    write_key(out, options, level + 1, "passed"); out << (player.passed ? "true" : "false"); write_sep(out, options);
    write_key(out, options, level + 1, "pass_reason"); write_string(out, to_string(player.pass_reason)); write_sep(out, options);
    write_key(out, options, level + 1, "leader_used"); out << (player.leader_used ? "true" : "false"); write_sep(out, options);
    write_key(out, options, level + 1, "round_wins"); out << player.round_wins; write_sep(out, options);
    write_key(out, options, level + 1, "mulligans_available"); out << player.mulligans_available; write_sep(out, options);
    write_key(out, options, level + 1, "cards_drawn_this_round"); out << player.cards_drawn_this_round; write_sep(out, options);
    write_key(out, options, level + 1, "melee_score"); out << state.row_score(player_id, Zone::Melee); write_sep(out, options);
    write_key(out, options, level + 1, "ranged_score"); out << state.row_score(player_id, Zone::Ranged); write_sep(out, options);
    write_key(out, options, level + 1, "board_score"); out << state.board_score(player_id); write_sep(out, options);

    write_key(out, options, level + 1, "zones");
    out << '{'; write_newline(out, options);
    write_key(out, options, level + 2, "deck"); write_zone_cards(out, state, player.deck, options, level + 2); write_sep(out, options);
    write_key(out, options, level + 2, "hand"); write_zone_cards(out, state, player.hand, options, level + 2); write_sep(out, options);
    write_key(out, options, level + 2, "stay"); write_zone_cards(out, state, player.stay, options, level + 2); write_sep(out, options);
    write_key(out, options, level + 2, "melee"); write_zone_cards(out, state, player.rows[row_index(Zone::Melee)], options, level + 2); write_sep(out, options);
    write_key(out, options, level + 2, "ranged"); write_zone_cards(out, state, player.rows[row_index(Zone::Ranged)], options, level + 2); write_sep(out, options);
    write_key(out, options, level + 2, "cemetery"); write_zone_cards(out, state, player.cemetery, options, level + 2); write_sep(out, options);
    write_key(out, options, level + 2, "banished"); write_zone_cards(out, state, player.banished, options, level + 2); write_sep(out, options);
    write_key(out, options, level + 2, "leader");
    if (player.leader.has_value()) {
        write_card_ref(out, state, *player.leader, options, level + 2);
    } else {
        out << "null";
    }
    write_newline(out, options);
    write_indent(out, options, level + 1); out << '}';

    write_newline(out, options);
    write_indent(out, options, level);
    out << '}';
}

void write_suspended_task(std::ostringstream& out, const GameState& state, const Task& task, const SnapshotOptions& options, int level) {
    out << '{'; write_newline(out, options);
    write_key(out, options, level + 1, "type"); write_string(out, to_string(task.type)); write_sep(out, options);
    write_key(out, options, level + 1, "actor_id"); out << task.actor_id; write_sep(out, options);
    write_key(out, options, level + 1, "source_entity_id"); out << task.source_entity_id; write_sep(out, options);
    write_key(out, options, level + 1, "target_entity_id"); out << task.target_entity_id; write_sep(out, options);
    write_key(out, options, level + 1, "target_kind"); write_string(out, to_string(task.target.kind)); write_sep(out, options);
    write_key(out, options, level + 1, "target_side"); out << task.target.side; write_sep(out, options);
    write_key(out, options, level + 1, "target_zone"); write_string(out, to_string(task.target.zone)); write_sep(out, options);
    write_key(out, options, level + 1, "target_entity"); out << task.target.entity_id; write_sep(out, options);
    write_key(out, options, level + 1, "amount"); out << task.amount; write_sep(out, options);
    write_key(out, options, level + 1, "reason"); write_string(out, task.reason); write_sep(out, options);
    write_key(out, options, level + 1, "destination");
    if (task.destination.has_value()) {
        out << '{';
        write_key(out, options, 0, "side"); out << task.destination->side; out << ',' << (options.pretty ? " " : "");
        write_key(out, options, 0, "zone"); write_string(out, to_string(task.destination->zone)); out << ',' << (options.pretty ? " " : "");
        write_key(out, options, 0, "index"); out << task.destination->index;
        out << '}';
    } else {
        out << "null";
    }
    write_sep(out, options);
    write_key(out, options, level + 1, "card_definition_id");
    if (task.card_definition_id != kInvalidCardDefId && state.card_catalog) write_string(out, state.card_catalog->at(task.card_definition_id).id); else out << "null";
    write_sep(out, options);
    write_key(out, options, level + 1, "round_winner_id");
    if (task.round_winner_id.has_value()) out << *task.round_winner_id; else out << "null";
    write_newline(out, options);
    write_indent(out, options, level); out << '}';
}

void write_pending(std::ostringstream& out, const GameState& state, const SnapshotOptions& options, int level) {
    if (!state.pending_choice.has_value()) {
        out << "null";
        return;
    }
    const auto& pending = *state.pending_choice;
    out << '{'; write_newline(out, options);
    write_key(out, options, level + 1, "kind");
    write_string(out, pending.kind == PendingChoiceKind::CardTarget ? "CardTarget" : (pending.kind == PendingChoiceKind::CardDefinitionChoice ? "CardDefinitionChoice" : (pending.kind == PendingChoiceKind::RowTarget ? "RowTarget" : (pending.kind == PendingChoiceKind::InsertPosition ? "InsertPosition" : "None"))));
    write_sep(out, options);
    write_key(out, options, level + 1, "resume_kind");
    write_string(out, pending.resume_kind == PendingChoiceResumeKind::PlayCard ? "PlayCard" : "InvokeEffect");
    write_sep(out, options);
    write_key(out, options, level + 1, "origin_action_type");
    if (pending.origin_action_type.has_value()) write_string(out, to_string(*pending.origin_action_type)); else out << "null";
    write_sep(out, options);
    write_key(out, options, level + 1, "choice_id"); out << pending.choice_id; write_sep(out, options);
    write_key(out, options, level + 1, "player_id"); out << pending.player_id; write_sep(out, options);
    write_key(out, options, level + 1, "source_entity_id"); out << pending.source_entity_id; write_sep(out, options);
    write_key(out, options, level + 1, "effect_id"); write_string(out, pending.effect_id); write_sep(out, options);
    write_key(out, options, level + 1, "legal_card_targets");
    out << '[';
    for (std::size_t i = 0; i < pending.legal_card_targets.size(); ++i) {
        if (i != 0) out << ',' << (options.pretty ? " " : "");
        out << pending.legal_card_targets[i];
    }
    out << ']'; write_sep(out, options);
    write_key(out, options, level + 1, "legal_card_definition_ids");
    out << '[';
    for (std::size_t i = 0; i < pending.legal_card_definition_ids.size(); ++i) {
        if (i != 0) out << ',' << (options.pretty ? " " : "");
        if (state.card_catalog) write_string(out, state.card_catalog->at(pending.legal_card_definition_ids[i]).id); else out << "null";
    }
    out << ']'; write_sep(out, options);
    write_key(out, options, level + 1, "legal_row_targets");
    out << '[';
    for (std::size_t i = 0; i < pending.legal_row_targets.size(); ++i) {
        if (i != 0) out << ',' << (options.pretty ? " " : "");
        out << '{';
        write_key(out, options, 0, "side"); out << pending.legal_row_targets[i].side; out << ',' << (options.pretty ? " " : "");
        write_key(out, options, 0, "zone"); write_string(out, to_string(pending.legal_row_targets[i].zone)); out << ',' << (options.pretty ? " " : "");
        write_key(out, options, 0, "insert_position"); out << pending.legal_row_targets[i].index;
        out << '}';
    }
    out << ']'; write_sep(out, options);
    write_key(out, options, level + 1, "continuation");
    out << '[';
    for (std::size_t i = 0; i < pending.continuation.size(); ++i) {
        if (i != 0) out << ',' << (options.pretty ? " " : "");
        const ContinuationStep& step = pending.continuation[i];
        out << '{';
        write_key(out, options, 0, "kind");
        switch (step.kind) {
            case ContinuationStepKind::ConsumeOrderSource: write_string(out, "ConsumeOrderSource"); break;
            case ContinuationStepKind::FinishSpecialCard: write_string(out, "FinishSpecialCard"); break;
            case ContinuationStepKind::AdvanceTurnOrFinishRound: write_string(out, "AdvanceTurnOrFinishRound"); break;
            case ContinuationStepKind::MarkIntraTurnFollowup: write_string(out, "MarkIntraTurnFollowup"); break;
            case ContinuationStepKind::PlayStagedCard: write_string(out, "PlayStagedCard"); break;
        }
        out << ',' << (options.pretty ? " " : "");
        write_key(out, options, 0, "actor_id"); out << step.actor_id;
        out << ',' << (options.pretty ? " " : "");
        write_key(out, options, 0, "source_entity_id"); out << step.source_entity_id;
        out << '}';
    }
    out << ']'; write_sep(out, options);
    write_key(out, options, level + 1, "suspended_tasks");
    out << '[';
    if (!pending.suspended_tasks.empty()) write_newline(out, options);
    for (std::size_t i = 0; i < pending.suspended_tasks.size(); ++i) {
        write_indent(out, options, level + 2);
        write_suspended_task(out, state, pending.suspended_tasks[i], options, level + 2);
        if (i + 1 < pending.suspended_tasks.size()) write_sep(out, options);
    }
    if (!pending.suspended_tasks.empty()) {
        write_newline(out, options);
        write_indent(out, options, level + 1);
    }
    out << ']';
    write_newline(out, options);
    write_indent(out, options, level); out << '}';
}

void write_listeners(std::ostringstream& out, const GameState& state, const SnapshotOptions& options, int level) {
    out << '['; write_newline(out, options);
    std::vector<ListenerId> ids;
    ids.reserve(state.listeners.size());
    for (const auto& [id, _] : state.listeners) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& listener = state.listeners.at(ids[i]);
        write_indent(out, options, level + 1); out << '{'; write_newline(out, options);
        write_key(out, options, level + 2, "listener_id"); out << listener.listener_id; write_sep(out, options);
        write_key(out, options, level + 2, "event"); write_string(out, listener.event); write_sep(out, options);
        write_key(out, options, level + 2, "handler"); write_string(out, listener.handler); write_sep(out, options);
        write_key(out, options, level + 2, "source_id");
        if (listener.source_id.has_value()) out << *listener.source_id; else out << "null";
        write_sep(out, options);
        write_key(out, options, level + 2, "once"); out << (listener.once ? "true" : "false");
        if (listener.origin == RuntimeListenerOrigin::Infusion) {
            write_sep(out, options);
            write_key(out, options, level + 2, "origin"); write_string(out, "Infusion");
        }
        write_newline(out, options);
        write_indent(out, options, level + 1); out << '}';
        if (i + 1 < ids.size()) write_sep(out, options);
    }
    write_newline(out, options);
    write_indent(out, options, level); out << ']';
}

std::string target_to_json(const ActionTarget& target, const SnapshotOptions& options, int level) {
    std::ostringstream out;
    out << '{'; write_newline(out, options);
    write_key(out, options, level + 1, "kind"); write_string(out, to_string(target.kind)); write_sep(out, options);
    write_key(out, options, level + 1, "side"); out << target.side; write_sep(out, options);
    write_key(out, options, level + 1, "zone"); write_string(out, to_string(target.zone)); write_sep(out, options);
    write_key(out, options, level + 1, "entity_id"); out << target.entity_id;
    if (target.kind == ActionTargetKind::CardDefinition) {
        write_sep(out, options);
        write_key(out, options, level + 1, "definition_id"); out << target.definition_id;
    }
    write_newline(out, options);
    write_indent(out, options, level); out << '}';
    return out.str();
}

}  // namespace

std::string action_to_json(const Action& action, const SnapshotOptions& options) {
    std::ostringstream out;
    out << '{'; write_newline(out, options);
    write_key(out, options, 1, "type"); write_string(out, to_string(action.type)); write_sep(out, options);
    write_key(out, options, 1, "player_id"); out << action.player_id; write_sep(out, options);
    write_key(out, options, 1, "source_entity_id"); out << action.source_entity_id; write_sep(out, options);
    write_key(out, options, 1, "insert_position"); out << action.insert_position; write_sep(out, options);
    write_key(out, options, 1, "target"); out << target_to_json(action.target, options, 1);
    write_newline(out, options);
    out << '}';
    return out.str();
}

std::string event_to_json(const EventRecord& event, const SnapshotOptions& options) {
    std::ostringstream out;
    out << '{'; write_newline(out, options);
    write_key(out, options, 1, "task_type"); write_string(out, to_string(event.task_type)); write_sep(out, options);
    write_key(out, options, 1, "name"); write_string(out, event.name); write_sep(out, options);
    write_key(out, options, 1, "actor_id"); out << event.actor_id; write_sep(out, options);
    write_key(out, options, 1, "source_entity_id"); out << event.source_entity_id; write_sep(out, options);
    write_key(out, options, 1, "target_entity_id"); out << event.target_entity_id; write_sep(out, options);
    write_key(out, options, 1, "amount"); out << event.amount; write_sep(out, options);
    write_key(out, options, 1, "message"); write_string(out, event.message);
    write_newline(out, options);
    out << '}';
    return out.str();
}

std::string events_to_json(const std::vector<EventRecord>& events, const SnapshotOptions& options) {
    std::ostringstream out;
    out << '['; write_newline(out, options);
    for (std::size_t i = 0; i < events.size(); ++i) {
        write_indent(out, options, 1);
        out << event_to_json(events[i], SnapshotOptions{options.include_entity_ids, options.include_card_names, options.include_listeners, options.pretty});
        if (i + 1 < events.size()) write_sep(out, options);
    }
    write_newline(out, options);
    out << ']';
    return out.str();
}

std::string state_to_json(const GameState& state, const SnapshotOptions& options) {
    std::ostringstream out;
    out << '{'; write_newline(out, options);
    write_key(out, options, 1, "status"); write_string(out, to_string(state.status)); write_sep(out, options);
    write_key(out, options, 1, "phase"); write_string(out, to_string(state.phase)); write_sep(out, options);
    write_key(out, options, 1, "engine_phase"); write_string(out, to_string(state.engine_phase)); write_sep(out, options);
    write_key(out, options, 1, "round_no"); out << state.round_no; write_sep(out, options);
    write_key(out, options, 1, "turn_no"); out << state.turn_no; write_sep(out, options);
    write_key(out, options, 1, "current_player_id"); out << state.current_player_id; write_sep(out, options);
    write_key(out, options, 1, "starting_player_id"); out << state.starting_player_id; write_sep(out, options);
    write_key(out, options, 1, "round_starting_player_id"); out << state.round_starting_player_id; write_sep(out, options);
    write_key(out, options, 1, "mulligan_player_id"); out << optional_player_to_json(state.mulligan_player_id); write_sep(out, options);
    write_key(out, options, 1, "winner_id"); out << optional_player_to_json(state.winner_id); write_sep(out, options);
    write_key(out, options, 1, "next_entity_id"); out << state.next_entity_id; write_sep(out, options);

    write_key(out, options, 1, "pending_choice"); write_pending(out, state, options, 1); write_sep(out, options);

    write_key(out, options, 1, "players");
    out << '['; write_newline(out, options);
    for (PlayerId pid = 0; pid < kPlayerCount; ++pid) {
        write_indent(out, options, 2);
        write_player(out, state, pid, options, 2);
        if (pid + 1 < kPlayerCount) write_sep(out, options);
    }
    write_newline(out, options);
    write_indent(out, options, 1); out << ']';

    if (options.include_listeners) {
        write_sep(out, options);
        write_key(out, options, 1, "listeners");
        write_listeners(out, state, options, 1);
    }

    write_newline(out, options);
    out << '}';
    return out.str();
}

std::string checksum_json(std::string_view json) {
    // FNV-1a 64-bit. This is not a security hash; it is a compact regression
    // fingerprint for deterministic trace comparisons.
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : json) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string state_checksum(const GameState& state, const SnapshotOptions& options) {
    SnapshotOptions stable = options;
    stable.pretty = false;
    return checksum_json(state_to_json(state, stable));
}

std::string trace_to_json(const TraceDocument& trace, const SnapshotOptions& options) {
    std::ostringstream out;
    out << '{'; write_newline(out, options);
    write_key(out, options, 1, "schema_version"); write_string(out, "gwent-golden-trace-v1"); write_sep(out, options);
    write_key(out, options, 1, "engine"); write_string(out, trace.engine); write_sep(out, options);
    write_key(out, options, 1, "scenario"); write_string(out, trace.scenario); write_sep(out, options);
    write_key(out, options, 1, "seed"); out << trace.seed; write_sep(out, options);
    write_key(out, options, 1, "starting_player_id"); out << trace.starting_player_id; write_sep(out, options);
    write_key(out, options, 1, "steps");
    out << '['; write_newline(out, options);
    for (std::size_t i = 0; i < trace.steps.size(); ++i) {
        const auto& step = trace.steps[i];
        write_indent(out, options, 2); out << '{'; write_newline(out, options);
        write_key(out, options, 3, "step_index"); out << step.step_index; write_sep(out, options);
        write_key(out, options, 3, "label"); write_string(out, step.label); write_sep(out, options);
        write_key(out, options, 3, "checksum"); write_string(out, step.checksum); write_sep(out, options);
        write_key(out, options, 3, "action");
        if (step.action_json.empty()) out << "null"; else out << step.action_json;
        write_sep(out, options);
        write_key(out, options, 3, "result");
        if (step.result_json.empty()) out << "null"; else out << step.result_json;
        write_sep(out, options);
        if (!step.legal_surface_json.empty()) {
            write_key(out, options, 3, "legal_surface"); out << step.legal_surface_json;
            write_sep(out, options);
        }
        write_key(out, options, 3, "state"); out << step.state_json;
        write_newline(out, options);
        write_indent(out, options, 2); out << '}';
        if (i + 1 < trace.steps.size()) write_sep(out, options);
    }
    write_newline(out, options);
    write_indent(out, options, 1); out << ']';
    write_newline(out, options);
    out << '}';
    return out.str();
}

}  // namespace gwent::trace
