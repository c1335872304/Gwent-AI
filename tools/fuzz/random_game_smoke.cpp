#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "gwent/api/core.hpp"
#include "gwent/c/core.h"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/core/enums.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/decision.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/trace/state_snapshot.hpp"

namespace {

using namespace gwent;

struct Options {
    int games = 1000;
    std::uint64_t seed = 0;
    int max_actions_per_game = 512;
    bool require_terminal = true;
    bool verbose = false;
};

[[nodiscard]] std::string zone_name(Zone zone) {
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

[[nodiscard]] std::string target_label(const ActionTarget& target) {
    std::ostringstream out;
    switch (target.kind) {
        case ActionTargetKind::None:
            out << "none";
            break;
        case ActionTargetKind::Row:
            out << "row(side=" << target.side << ",zone=" << zone_name(target.zone) << ")";
            break;
        case ActionTargetKind::Card:
            out << "card(eid=" << target.entity_id << ")";
            break;
    }
    return out.str();
}

[[nodiscard]] std::string action_label(const Action& action) {
    std::ostringstream out;
    out << to_string(action.type) << "(p=" << action.player_id;
    if (action.source_entity_id != kInvalidEntityId) {
        out << ",src=" << action.source_entity_id;
    }
    out << ",target=" << target_label(action.target) << ")";
    return out.str();
}

[[nodiscard]] std::string decision_label(const Decision& decision) {
    std::ostringstream out;
    out << to_string(decision.type) << "(p=" << decision.player_id
        << ",actions=" << decision.legal_actions.size() << ")";
    return out.str();
}

[[nodiscard]] std::string status_label(const GameState& state) {
    std::ostringstream out;
    out << "status=" << to_string(state.status)
        << " phase=" << to_string(state.phase)
        << " engine_phase=" << to_string(state.engine_phase)
        << " round=" << state.round_no
        << " turn=" << state.turn_no
        << " current=" << state.current_player_id
        << " checksum=" << gwent::trace::state_checksum(state);
    for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
        const auto& player = state.player(player_id);
        out << " p" << player_id
            << "{passed=" << (player.passed ? "true" : "false")
            << ",hand=" << player.hand.size()
            << ",deck=" << player.deck.size()
            << ",melee=" << player.row(Zone::Melee).size()
            << ",ranged=" << player.row(Zone::Ranged).size()
            << ",leader_used=" << (player.leader_used ? "true" : "false")
            << "}";
    }
    for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
        const auto& turn = state.turn_contexts[static_cast<std::size_t>(player_id)];
        if (turn.used_intra_turn_action || turn.consumed_hand_card) {
            out << " turnctx[p" << player_id
                << ":intra=" << (turn.used_intra_turn_action ? "1" : "0")
                << ",consumed=" << (turn.consumed_hand_card ? "1" : "0")
                << ",played=" << (turn.played_card_this_turn ? "1" : "0")
                << ",discarded=" << (turn.discarded_card_this_turn ? "1" : "0")
                << ']';
        }
    }
    return out.str();
}

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](std::string_view name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + std::string(name));
            }
            return argv[++i];
        };

        if (arg == "--games") {
            options.games = std::stoi(require_value(arg));
        } else if (arg == "--seed") {
            options.seed = static_cast<std::uint64_t>(std::stoull(require_value(arg)));
        } else if (arg == "--max-actions") {
            options.max_actions_per_game = std::stoi(require_value(arg));
        } else if (arg == "--allow-non-terminal") {
            options.require_terminal = false;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "usage: gwent_random_game_smoke [--games n] [--seed n] "
                << "[--max-actions n] [--allow-non-terminal] [--verbose]\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.games <= 0) {
        throw std::runtime_error("--games must be positive");
    }
    if (options.max_actions_per_game <= 0) {
        throw std::runtime_error("--max-actions must be positive");
    }
    return options;
}

void require_invariants_ok(const api::DeckAGame& game, int game_index, int action_index, std::string_view stage) {
    const auto report = game.validate();
    if (!report.ok()) {
        std::ostringstream out;
        out << "invariant violation " << stage
            << " game=" << game_index
            << " action=" << action_index
            << ": " << report.summary() << '\n'
            << invariant_report_to_string(report) << '\n'
            << status_label(game.state());
        throw std::runtime_error(out.str());
    }
}

void require_decision_is_well_formed(
    const api::DeckAGame& game,
    const Decision& decision,
    int game_index,
    int action_index
) {
    if (game.is_finished()) {
        return;
    }
    if (decision.legal_actions.empty()) {
        std::ostringstream out;
        out << "non-terminal state has no legal actions"
            << " game=" << game_index
            << " action=" << action_index
            << ' ' << decision_label(decision) << ' '
            << status_label(game.state());
        throw std::runtime_error(out.str());
    }

    if (decision.legal_actions.size() > static_cast<std::size_t>(GWENT_RL_MAX_OPTIONS)) {
        throw std::runtime_error("legal action surface exceeds GWENT_RL_MAX_OPTIONS; RL adapter would overflow");
    }

    for (const Action& action : decision.legal_actions) {
        if (!is_legal_action(game.state(), action)) {
            throw std::runtime_error("decision contains an action rejected by authoritative legality query");
        }
    }

    const EnginePhase expected_phase = game.state().pending_choice.has_value()
        ? EnginePhase::AwaitingChoice
        : EnginePhase::ActionWindow;
    if (game.state().engine_phase != expected_phase) {
        throw std::runtime_error("stable decision exposed with inconsistent EnginePhase");
    }

    if (game.state().pending_choice.has_value()) {
        const PendingChoice& choice = *game.state().pending_choice;
        if (decision.type != DecisionType::ChooseTarget) {
            std::ostringstream out;
            out << "pending choice did not produce a ChooseTarget decision"
                << " game=" << game_index
                << " action=" << action_index
                << ' ' << decision_label(decision) << ' '
                << status_label(game.state());
            throw std::runtime_error(out.str());
        }
        if (choice.kind == PendingChoiceKind::CardTarget && choice.legal_card_targets.empty()) {
            throw std::runtime_error("pending card choice has no legal targets");
        }
        if (choice.kind == PendingChoiceKind::RowTarget && choice.legal_row_targets.empty()) {
            throw std::runtime_error("pending row choice has no legal rows");
        }
    }
}

void run_one_game(const Options& options, int game_index) {
    api::DeckAMatchConfig config;
    config.seed = options.seed + static_cast<std::uint64_t>(game_index * 7919);
    config.shuffle_decks = true;
    config.kernel_config.invariant_policy = InvariantPolicy::AfterApply;

    auto game = api::DeckAGame::create(config);
    std::mt19937_64 rng(config.seed ^ 0x9e3779b97f4a7c15ULL);
    require_invariants_ok(game, game_index, 0, "after setup");

    int applied_actions = 0;
    for (; applied_actions < options.max_actions_per_game && !game.is_finished(); ++applied_actions) {
        Decision decision = game.current_decision();
        require_decision_is_well_formed(game, decision, game_index, applied_actions);

        const std::size_t choice_index = static_cast<std::size_t>(rng() % decision.legal_actions.size());
        const Action action = decision.legal_actions[choice_index];
        const KernelResult result = game.apply(action);
        if (!result.applied) {
            std::ostringstream out;
            out << "legal action was rejected"
                << " game=" << game_index
                << " action=" << applied_actions
                << " chosen=" << action_label(action)
                << " status=" << to_string(result.status)
                << " message=" << result.message
                << ' ' << status_label(game.state());
            throw std::runtime_error(out.str());
        }
        require_invariants_ok(game, game_index, applied_actions + 1, "after apply");
    }

    if (options.require_terminal && !game.is_finished()) {
        std::ostringstream out;
        out << "game did not terminate within max action budget"
            << " game=" << game_index
            << " max_actions=" << options.max_actions_per_game
            << ' ' << status_label(game.state());
        throw std::runtime_error(out.str());
    }

    if (options.verbose) {
        std::cout << "game " << game_index
                  << " seed=" << config.seed
                  << " actions=" << applied_actions
                  << " finished=" << (game.is_finished() ? "true" : "false")
                  << " winner=";
        if (game.winner().has_value()) {
            std::cout << *game.winner();
        } else {
            std::cout << "none";
        }
        std::cout << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        for (int game = 0; game < options.games; ++game) {
            run_one_game(options, game);
        }
        std::cout << "random_game_smoke ok: games=" << options.games
                  << " seed=" << options.seed
                  << " max_actions=" << options.max_actions_per_game
                  << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "random_game_smoke error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
