#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/cards/deck_a.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/game/setup.hpp"
#include "gwent/trace/state_snapshot.hpp"

namespace {

using namespace gwent;

std::string trim(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

std::vector<std::string> split_ws(std::string_view line) {
    std::istringstream in{std::string(line)};
    std::vector<std::string> tokens;
    std::string token;
    while (in >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::map<std::string, std::string> parse_kv(const std::vector<std::string>& tokens, std::size_t from = 1) {
    std::map<std::string, std::string> result;
    for (std::size_t i = from; i < tokens.size(); ++i) {
        const auto pos = tokens[i].find('=');
        if (pos == std::string::npos) {
            throw std::runtime_error("expected key=value token: " + tokens[i]);
        }
        result[tokens[i].substr(0, pos)] = tokens[i].substr(pos + 1);
    }
    return result;
}

int int_arg(const std::map<std::string, std::string>& kv, std::string_view primary, std::string_view fallback, int default_value) {
    if (auto it = kv.find(std::string(primary)); it != kv.end()) {
        return std::stoi(it->second);
    }
    if (!fallback.empty()) {
        if (auto it = kv.find(std::string(fallback)); it != kv.end()) {
            return std::stoi(it->second);
        }
    }
    return default_value;
}

std::string str_arg(const std::map<std::string, std::string>& kv, std::string_view key, std::string default_value = {}) {
    if (auto it = kv.find(std::string(key)); it != kv.end()) {
        return it->second;
    }
    return default_value;
}

Zone parse_zone(std::string_view value) {
    if (value == "melee" || value == "Melee") return Zone::Melee;
    if (value == "ranged" || value == "Ranged") return Zone::Ranged;
    if (value == "hand" || value == "Hand") return Zone::Hand;
    if (value == "deck" || value == "Deck") return Zone::Deck;
    if (value == "graveyard" || value == "cemetery" || value == "Cemetery") return Zone::Cemetery;
    if (value == "banished" || value == "Banished") return Zone::Banished;
    if (value == "stay" || value == "Stay") return Zone::Stay;
    throw std::runtime_error("unknown zone: " + std::string(value));
}

PlayerId parse_side(std::string_view value, PlayerId actor) {
    if (value == "self" || value == "me" || value == "actor") return actor;
    if (value == "enemy" || value == "opponent") return actor == kPlayerZero ? kPlayerOne : kPlayerZero;
    if (value == "0") return kPlayerZero;
    if (value == "1") return kPlayerOne;
    throw std::runtime_error("unknown side: " + std::string(value));
}

const std::vector<EntityId>& zone_vector(const GameState& state, PlayerId side, Zone zone) {
    const auto& player = state.player(side);
    switch (zone) {
        case Zone::Deck: return player.deck;
        case Zone::Hand: return player.hand;
        case Zone::Stay: return player.stay;
        case Zone::Melee: return player.rows[row_index(Zone::Melee)];
        case Zone::Ranged: return player.rows[row_index(Zone::Ranged)];
        case Zone::Cemetery: return player.cemetery;
        case Zone::Banished: return player.banished;
        case Zone::Leader: throw std::runtime_error("leader zone is not index-addressable; use leader action");
    }
    throw std::runtime_error("unsupported zone");
}

EntityId entity_at(const GameState& state, PlayerId side, Zone zone, int index) {
    const auto& zone_cards = zone_vector(state, side, zone);
    if (index < 0 || static_cast<std::size_t>(index) >= zone_cards.size()) {
        throw std::runtime_error("zone index out of range");
    }
    return zone_cards[static_cast<std::size_t>(index)];
}

std::vector<std::string> split_char(std::string_view text, char delim) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto pos = text.find(delim, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

EntityId parse_board_ref(const GameState& state, std::string_view spec, PlayerId actor) {
    // side:zone:index, where side may be self/enemy/0/1.
    const auto parts = split_char(spec, ':');
    if (parts.size() != 3) {
        throw std::runtime_error("expected board ref side:zone:index");
    }
    const PlayerId side = parse_side(parts[0], actor);
    const Zone zone = parse_zone(parts[1]);
    const int index = std::stoi(parts[2]);
    return entity_at(state, side, zone, index);
}


bool has_card_selector(const std::map<std::string, std::string>& kv) {
    return kv.count("entity") != 0
        || kv.count("entity_id") != 0
        || kv.count("eid") != 0
        || kv.count("card_id") != 0
        || kv.count("cid") != 0
        || kv.count("name") != 0
        || kv.count("card_name") != 0
        || kv.count("target_card_id") != 0
        || kv.count("target_cid") != 0
        || kv.count("target_name") != 0;
}

std::string selector_string(const std::map<std::string, std::string>& kv, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (auto it = kv.find(key); it != kv.end()) {
            return it->second;
        }
    }
    return {};
}

std::optional<EntityId> select_entity_from_candidates(
    const GameState& state,
    const std::vector<EntityId>& candidates,
    const std::map<std::string, std::string>& kv,
    std::string_view context
) {
    if (!has_card_selector(kv)) {
        return std::nullopt;
    }

    const std::string entity_text = selector_string(kv, {"entity", "entity_id", "eid"});
    if (!entity_text.empty()) {
        const EntityId entity = std::stoi(entity_text);
        if (std::find(candidates.begin(), candidates.end(), entity) == candidates.end()) {
            throw std::runtime_error(std::string(context) + " selector entity is not legal in this candidate set: " + entity_text);
        }
        return entity;
    }

    const std::string card_id = selector_string(kv, {"card_id", "cid", "target_card_id", "target_cid"});
    const std::string name = selector_string(kv, {"name", "card_name", "target_name"});
    const int copy = int_arg(kv, "copy", "", 0);
    int matched_copy = 0;
    for (const EntityId entity : candidates) {
        const RuntimeCard* card = state.find_card(entity);
        if (card == nullptr) {
            continue;
        }
        if (!card_id.empty() && card->definition->id != card_id) {
            continue;
        }
        if (!name.empty() && card->definition->name != name) {
            continue;
        }
        if (matched_copy == copy) {
            return entity;
        }
        ++matched_copy;
    }

    throw std::runtime_error(std::string(context) + " selector did not match any candidate; card_id=" + card_id + " name=" + name);
}

EntityId entity_from_zone_selector_or_index(
    const GameState& state,
    PlayerId player,
    Zone zone,
    const std::map<std::string, std::string>& kv,
    std::string_view context,
    std::string_view primary_index_key,
    std::string_view fallback_index_key,
    int default_index
) {
    const auto& candidates = zone_vector(state, player, zone);
    if (auto selected = select_entity_from_candidates(state, candidates, kv, context)) {
        return *selected;
    }
    const int index = int_arg(kv, primary_index_key, fallback_index_key, default_index);
    return entity_at(state, player, zone, index);
}

DeckSpec make_trace_deck(PlayerId owner) {
    const auto prefix = std::string("trace_p") + std::to_string(owner) + ".";
    DeckSpec deck;
    deck.leader = make_leader_definition(prefix + "leader", "Trace Leader " + std::to_string(owner), Faction::Monsters);
    deck.leader.effect_ids.clear();
    deck.stratagem = make_stratagem_definition(prefix + "stratagem", "Trace Stratagem " + std::to_string(owner));
    deck.stratagem.effect_ids.clear();

    for (int i = 0; i < 25; ++i) {
        auto card = make_unit_definition(prefix + "unit_" + std::to_string(i), "Trace Unit " + std::to_string(owner) + "-" + std::to_string(i), 3 + (i % 5), Faction::Monsters);
        card.effect_ids.clear();
        card.provision = 4;
        deck.cards.push_back(DeckCardSpec{card, 1});
    }
    return deck;
}

DeckSpec make_deck_for_mode(std::string_view deck_mode, PlayerId owner) {
    if (deck_mode == "trace") {
        return make_trace_deck(owner);
    }
    if (deck_mode == "deck-a" || deck_mode == "deck_a") {
        (void)owner;
        return deck_a::make_deck_spec();
    }
    throw std::runtime_error("unknown deck mode: " + std::string(deck_mode));
}

void register_effects_for_mode(EffectRegistry& effects, std::string_view effects_mode) {
    if (effects_mode == "none") {
        return;
    }
    if (effects_mode == "deck-a" || effects_mode == "deck_a") {
        deck_a::register_deck_a_effects(effects);
        return;
    }
    throw std::runtime_error("unknown effects mode: " + std::string(effects_mode));
}


std::string json_escape_local(std::string_view value) {
    std::ostringstream out;
    for (char ch : value) {
        if (ch == '"') out << "\\\"";
        else if (ch == '\\') out << "\\\\";
        else if (ch == '\n') out << "\\n";
        else if (ch == '\r') out << "\\r";
        else if (ch == '\t') out << "\\t";
        else out << ch;
    }
    return out.str();
}

std::string quoted(std::string_view value) {
    return std::string("\"") + json_escape_local(value) + "\"";
}

std::string zone_lower(Zone zone) {
    switch (zone) {
        case Zone::Melee: return "melee";
        case Zone::Ranged: return "ranged";
        case Zone::Hand: return "hand";
        case Zone::Deck: return "deck";
        case Zone::Stay: return "stay";
        case Zone::Cemetery: return "cemetery";
        case Zone::Banished: return "banished";
        case Zone::Leader: return "leader";
    }
    return "unknown";
}

std::string card_signature(const GameState& state, EntityId entity_id) {
    const RuntimeCard* card = state.find_card(entity_id);
    std::ostringstream out;
    out << "eid=" << entity_id;
    if (card != nullptr) {
        out << ":cid=" << card->definition->id << ":name=" << card->definition->name;
    }
    return out.str();
}

std::string target_signature(const GameState& state, const ActionTarget& target) {
    std::ostringstream out;
    switch (target.kind) {
        case ActionTargetKind::None:
            out << "target=none";
            break;
        case ActionTargetKind::Row:
            out << "side=" << target.side << ":row=" << zone_lower(target.zone);
            break;
        case ActionTargetKind::Card:
            out << "target=" << card_signature(state, target.entity_id);
            break;
    }
    return out.str();
}

std::string action_surface_label(const GameState& state, const Action& action) {
    std::ostringstream out;
    switch (action.type) {
        case ActionType::Pass:
            return "pass";
        case ActionType::EndTurn:
            return "end_turn";
        case ActionType::PlayCard:
            out << "play_hand:" << card_signature(state, action.source_entity_id)
                << ':' << target_signature(state, action.target);
            return out.str();
        case ActionType::DiscardCard:
            out << "discard_hand:" << card_signature(state, action.source_entity_id);
            return out.str();
        case ActionType::Mulligan:
            out << "mulligan:" << card_signature(state, action.source_entity_id);
            return out.str();
        case ActionType::KeepHand:
            return "keep_hand";
        case ActionType::UseLeader:
            out << "use_leader:" << card_signature(state, action.source_entity_id)
                << ':' << target_signature(state, action.target);
            return out.str();
        case ActionType::UseOrder:
            out << "use_order:" << card_signature(state, action.source_entity_id)
                << ':' << target_signature(state, action.target);
            return out.str();
        case ActionType::ChooseCardTarget:
            out << "choose_target:" << card_signature(state, action.target.entity_id);
            return out.str();
        case ActionType::ChooseRowTarget:
            out << "choose_row:" << target_signature(state, action.target);
            return out.str();
    }
    return "unknown";
}

bool has_deferred_order_to_close_for_surface(const GameState& state, PlayerId player_id) {
    const auto& player = state.player(player_id);
    for (Zone row_zone : kBattleZones) {
        for (EntityId entity_id : player.row(row_zone)) {
            const RuntimeCard* card = state.find_card(entity_id);
            if (card == nullptr || card->controller_id != player_id) {
                continue;
            }
            const auto it = card->memory.find("order_pending_consume");
            if (it != card->memory.end() && it->second == "true") {
                return true;
            }
        }
    }
    return false;
}

bool has_post_play_pending_end_for_surface(const GameState& state, PlayerId player_id) {
    return is_valid_player_id(player_id)
        && state.turn_contexts[static_cast<std::size_t>(player_id)].consumed_hand_card;
}

bool has_no_pass_after_intra_turn_action_for_surface(const GameState& state, PlayerId player_id) {
    return is_valid_player_id(player_id)
        && state.turn_contexts[static_cast<std::size_t>(player_id)].used_intra_turn_action;
}

std::string legal_surface_to_json(const GameState& state) {
    std::string decision_type = "none";
    PlayerId player_id = state.current_player_id;
    std::vector<std::string> actions;

    if (state.pending_choice.has_value()) {
        const PendingChoice& choice = *state.pending_choice;
        player_id = choice.player_id;
        if (choice.kind == PendingChoiceKind::CardTarget) {
            decision_type = "effect_card";
            for (const Action& action : legal_pending_choice_actions(state)) {
                actions.push_back(action_surface_label(state, action));
            }
        } else if (choice.kind == PendingChoiceKind::RowTarget) {
            decision_type = "place_row";
            for (const Action& action : legal_pending_choice_actions(state)) {
                actions.push_back(action_surface_label(state, action));
            }
        }
    } else if (state.mulligan_player_id.has_value()) {
        decision_type = "mulligan";
        player_id = *state.mulligan_player_id;
        for (const Action& action : legal_mulligan_actions(state, player_id)) {
            actions.push_back(action_surface_label(state, action));
        }
    } else if (state.status == MatchStatus::Running && state.phase == MatchPhase::Playing && !state.player(state.current_player_id).passed) {
        const bool deferred_order = has_deferred_order_to_close_for_surface(state, state.current_player_id);
        const bool post_play_pending_end = deferred_order || has_post_play_pending_end_for_surface(state, state.current_player_id);
        decision_type = post_play_pending_end ? "post_play" : "turn";
        player_id = state.current_player_id;
        if (!post_play_pending_end) {
            if (!has_no_pass_after_intra_turn_action_for_surface(state, state.current_player_id)) {
                actions.push_back(action_surface_label(state, Action::pass(state.current_player_id)));
            }
            for (const Action& action : legal_play_card_actions(state, player_id)) {
                actions.push_back(action_surface_label(state, action));
            }
            for (const Action& action : legal_discard_card_actions(state, player_id)) {
                actions.push_back(action_surface_label(state, action));
            }
        } else {
            actions.push_back(action_surface_label(state, Action::end_turn(state.current_player_id)));
        }
        for (const Action& action : legal_order_actions(state, player_id)) {
            actions.push_back(action_surface_label(state, action));
        }
        for (const Action& action : legal_leader_actions(state, player_id)) {
            actions.push_back(action_surface_label(state, action));
        }
    }

    std::sort(actions.begin(), actions.end());
    std::ostringstream out;
    out << "{\n";
    out << "  \"decision_type\": " << quoted(decision_type) << ",\n";
    out << "  \"player_id\": " << player_id << ",\n";
    out << "  \"actions\": [";
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (i != 0) out << ", ";
        out << quoted(actions[i]);
    }
    out << "]\n";
    out << "}";
    return out.str();
}

std::string result_to_json(const KernelResult& result) {
    gwent::trace::SnapshotOptions options;
    std::ostringstream out;
    out << "{\n";
    out << "  \"applied\": " << (result.applied ? "true" : "false") << ",\n";
    out << "  \"status\": \"" << to_string(result.status) << "\",\n";
    out << "  \"message\": \"";
    for (char ch : result.message) {
        if (ch == '"') out << "\\\"";
        else if (ch == '\\') out << "\\\\";
        else if (ch == '\n') out << "\\n";
        else out << ch;
    }
    out << "\",\n";
    out << "  \"events\": " << gwent::trace::events_to_json(result.events, options) << "\n";
    out << '}';
    return out.str();
}

Action parse_action_line(const GameState& state, std::string_view raw_line) {
    auto line = trim(raw_line);
    const auto comment = line.find('#');
    if (comment != std::string::npos) {
        line = trim(std::string_view(line).substr(0, comment));
    }
    if (line.empty()) {
        return Action::pass(-1);
    }

    const auto tokens = split_ws(line);
    const auto kv = parse_kv(tokens);
    const auto op = tokens.at(0);
    const PlayerId player = int_arg(kv, "p", "player", state.current_player_id);

    if (op == "pass") {
        return Action::pass(player);
    }
    if (op == "end_turn") {
        return Action::end_turn(player);
    }
    if (op == "mulligan") {
        const EntityId hand_card = entity_from_zone_selector_or_index(state, player, Zone::Hand, kv, "mulligan hand", "h", "hand", 0);
        return Action::mulligan(player, hand_card);
    }
    if (op == "keep_hand") {
        return Action::keep_hand(player);
    }
    if (op == "discard_hand") {
        const EntityId hand_card = entity_from_zone_selector_or_index(state, player, Zone::Hand, kv, "discard_hand", "h", "hand", 0);
        return Action::discard_card(player, hand_card);
    }
    if (op == "play_hand") {
        const EntityId hand_card = entity_from_zone_selector_or_index(state, player, Zone::Hand, kv, "play_hand", "h", "hand", 0);
        const RuntimeCard* card = state.find_card(hand_card);
        if (card == nullptr) {
            throw std::runtime_error("hand card disappeared");
        }
        if (card->definition->card_type == CardType::Special) {
            return Action::play_card(player, hand_card);
        }
        const auto row_name = str_arg(kv, "row", "melee");
        const auto side_name = str_arg(kv, "side", "self");
        const PlayerId target_side = parse_side(side_name, player);
        const Zone target_row = parse_zone(row_name);
        // Current PlayCard requires a concrete dynamic insertion index.  Keep
        // old trace scripts valid by appending when neither index nor position
        // is specified, while allowing new traces to exercise exact placement.
        const int insert_position = int_arg(
            kv,
            "index",
            "position",
            static_cast<int>(state.player(target_side).row(target_row).size())
        );
        return Action::play_card(player, hand_card, target_side, target_row, insert_position);
    }
    if (op == "play_special") {
        const EntityId hand_card = entity_from_zone_selector_or_index(state, player, Zone::Hand, kv, "play_special", "h", "hand", 0);
        return Action::play_card(player, hand_card);
    }
    if (op == "use_leader") {
        const auto& leader = state.player(player).leader;
        if (!leader.has_value()) {
            throw std::runtime_error("player has no leader");
        }
        if (auto it = kv.find("target"); it != kv.end()) {
            auto action = Action::use_leader(player, *leader);
            action.target = ActionTarget::card(parse_board_ref(state, it->second, player));
            return action;
        }
        return Action::use_leader(player, *leader);
    }
    if (op == "use_order") {
        const EntityId source = kv.count("source") != 0
            ? parse_board_ref(state, kv.at("source"), player)
            : parse_board_ref(state, str_arg(kv, "order", "self:melee:0"), player);
        if (auto it = kv.find("target"); it != kv.end()) {
            return Action::use_order(player, source, ActionTarget::card(parse_board_ref(state, it->second, player)));
        }
        return Action::use_order(player, source);
    }
    if (op == "choose_target") {
        if (!state.pending_choice.has_value()) {
            throw std::runtime_error("choose_target requested but state has no pending choice");
        }
        EntityId target = state.pending_choice->legal_card_targets.at(0);
        if (kv.count("target") != 0) {
            target = parse_board_ref(state, kv.at("target"), player);
        } else if (auto selected = select_entity_from_candidates(state, state.pending_choice->legal_card_targets, kv, "choose_target")) {
            target = *selected;
        }
        return Action::choose_card_target(player, state.pending_choice->source_entity_id, target);
    }

    if (op == "choose_row") {
        if (!state.pending_choice.has_value()) {
            throw std::runtime_error("choose_row requested but state has no pending choice");
        }
        const auto row_name = str_arg(kv, "row", "melee");
        const auto side_name = str_arg(kv, "side", "enemy");
        return Action::choose_row_target(player, state.pending_choice->source_entity_id, parse_side(side_name, player), parse_zone(row_name));
    }

    if (op == "choose_insert") {
        if (!state.pending_choice.has_value()
            || state.pending_choice->kind != PendingChoiceKind::InsertPosition
            || state.pending_choice->legal_row_targets.empty()) {
            throw std::runtime_error("choose_insert requested but state has no insert-position choice");
        }
        const Location fallback = state.pending_choice->legal_row_targets.front();
        const auto row_name = str_arg(kv, "row", std::string(to_string(fallback.zone)));
        const auto side_name = str_arg(kv, "side", fallback.side == player ? "self" : "enemy");
        const int position = int_arg(kv, "index", "position", static_cast<int>(fallback.index));
        return Action::choose_insert_position(
            player,
            state.pending_choice->source_entity_id,
            parse_side(side_name, player),
            parse_zone(row_name),
            position
        );
    }

    throw std::runtime_error("unknown trace action op: " + op);
}

void append_step(gwent::trace::TraceDocument& doc, const GameState& state, std::string label, bool include_legal_surface, std::string action_json = {}, std::string result_json = {}) {
    gwent::trace::SnapshotOptions options;
    auto state_json = gwent::trace::state_to_json(state, options);
    doc.steps.push_back(gwent::trace::TraceStep{
        static_cast<int>(doc.steps.size()),
        std::move(label),
        std::move(action_json),
        std::move(result_json),
        include_legal_surface ? legal_surface_to_json(state) : std::string{},
        std::move(state_json),
        gwent::trace::state_checksum(state, options),
    });
}

std::vector<std::string> built_in_script(std::string_view scenario) {
    if (scenario == "setup" || scenario == "deck_a_setup" || scenario == "deck_a_option_replay") {
        return {};
    }
    if (scenario == "smoke") {
        return {
            "keep_hand p=0",
            "keep_hand p=1",
            "play_hand p=0 h=0 row=melee side=self",
            "end_turn p=0",
            "play_hand p=1 h=0 row=ranged side=self",
            "end_turn p=1",
            "pass p=0",
            "pass p=1",
        };
    }
    throw std::runtime_error("unknown built-in scenario: " + std::string(scenario));
}

std::vector<std::string> read_script_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("could not open script: " + path);
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::uint64_t seed = 0;
        PlayerId starting_player = 0;
        std::string scenario = "smoke";
        std::optional<std::string> script_path;
        bool shuffle = false;
        std::string deck_mode = "trace";
        std::string effects_mode = "none";
        bool include_legal_surface = false;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto require_value = [&](std::string_view name) -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error("missing value for " + std::string(name));
                }
                return argv[++i];
            };
            if (arg == "--seed") {
                seed = static_cast<std::uint64_t>(std::stoull(require_value(arg)));
            } else if (arg == "--starting-player") {
                starting_player = std::stoi(require_value(arg));
            } else if (arg == "--scenario") {
                scenario = require_value(arg);
            } else if (arg == "--script") {
                script_path = require_value(arg);
            } else if (arg == "--shuffle") {
                shuffle = true;
            } else if (arg == "--deck") {
                deck_mode = require_value(arg);
            } else if (arg == "--effects") {
                effects_mode = require_value(arg);
            } else if (arg == "--include-legal-surface") {
                include_legal_surface = true;
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "usage: gwent_trace_runner [--scenario name] [--script file] [--seed n] [--starting-player 0|1] [--shuffle] [--deck trace|deck-a] [--effects none|deck-a] [--include-legal-surface]\n";
                return EXIT_SUCCESS;
            } else {
                throw std::runtime_error("unknown argument: " + arg);
            }
        }

        GameState state;
        MatchSetupConfig config;
        config.seed = seed;
        config.starting_player_id = starting_player;
        config.shuffle_decks = shuffle;
        state = make_empty_standard_state(config);
        auto deck0 = make_deck_for_mode(deck_mode, 0);
        auto deck1 = make_deck_for_mode(deck_mode, 1);
        [[maybe_unused]] auto setup_result = setup_standard_match(state, deck0, deck1, config);

        EffectRegistry effects;
        register_effects_for_mode(effects, effects_mode);
        KernelConfig kernel_config;
        gwent::trace::TraceDocument doc;
        doc.scenario = scenario;
        doc.seed = seed;
        doc.starting_player_id = starting_player;
        append_step(doc, state, "initial", include_legal_surface);

        const auto lines = script_path.has_value() ? read_script_file(*script_path) : built_in_script(scenario);
        for (const auto& line : lines) {
            auto trimmed = trim(line);
            const auto comment = trimmed.find('#');
            if (comment != std::string::npos) {
                trimmed = trim(std::string_view(trimmed).substr(0, comment));
            }
            if (trimmed.empty()) {
                continue;
            }
            const Action action = parse_action_line(state, trimmed);
            if (action.player_id == -1) {
                continue;
            }
            const auto action_json = gwent::trace::action_to_json(action);
            const KernelResult result = apply_action_with_kernel(state, action, effects, kernel_config);
            append_step(doc, state, trimmed, include_legal_surface, action_json, result_to_json(result));
            if (!result.applied) {
                break;
            }
        }

        std::cout << gwent::trace::trace_to_json(doc) << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "trace_runner error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
