#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "gwent/api/core.hpp"
#include "gwent/core/performance.hpp"
#include "gwent/engine/decision.hpp"
#include "gwent/trace/state_snapshot.hpp"

namespace {

using namespace gwent;

struct Options {
    int games = 128;
    std::uint64_t seed = 0;
    int max_actions_per_game = 512;
    bool enable_invariants = false;
    bool json = false;
};

[[nodiscard]] std::string require_value(int& i, int argc, char** argv, std::string_view name) {
    if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string(name));
    }
    return argv[++i];
}

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--games") {
            options.games = std::stoi(require_value(i, argc, argv, arg));
        } else if (arg == "--seed") {
            options.seed = static_cast<std::uint64_t>(std::stoull(require_value(i, argc, argv, arg)));
        } else if (arg == "--max-actions") {
            options.max_actions_per_game = std::stoi(require_value(i, argc, argv, arg));
        } else if (arg == "--with-invariants") {
            options.enable_invariants = true;
        } else if (arg == "--json") {
            options.json = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "usage: gwent_profile_smoke [--games n] [--seed n] "
                << "[--max-actions n] [--with-invariants] [--json]\n";
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

void run_profile(const Options& options, PerformanceCounters& counters) {
    for (int game_index = 0; game_index < options.games; ++game_index) {
        api::DeckAMatchConfig config;
        config.seed = options.seed + static_cast<std::uint64_t>(game_index * 7919);
        config.shuffle_decks = true;
        config.kernel_config.performance_counters = &counters;
        config.kernel_config.invariant_policy = options.enable_invariants
            ? InvariantPolicy::AfterApply
            : InvariantPolicy::Disabled;

        auto game = api::DeckAGame::create(config);
        std::mt19937_64 rng(config.seed ^ 0x6a09e667f3bcc909ULL);

        int action_count = 0;
        for (; action_count < options.max_actions_per_game && !game.is_finished(); ++action_count) {
            const Decision decision = game.current_decision();
            if (decision.legal_actions.empty()) {
                std::ostringstream out;
                out << "non-terminal game produced no legal actions"
                    << " game=" << game_index
                    << " action=" << action_count;
                throw std::runtime_error(out.str());
            }

            const auto choice = static_cast<std::size_t>(rng() % decision.legal_actions.size());
            const KernelResult result = game.apply(decision.legal_actions[choice]);
            if (!result.applied) {
                std::ostringstream out;
                out << "profiled legal action was rejected"
                    << " game=" << game_index
                    << " action=" << action_count
                    << " status=" << to_string(result.status)
                    << " message=" << result.message;
                throw std::runtime_error(out.str());
            }
        }

        if (!game.is_finished()) {
            std::ostringstream out;
            out << "profile game did not terminate within action budget"
                << " game=" << game_index
                << " max_actions=" << options.max_actions_per_game;
            throw std::runtime_error(out.str());
        }

        gwent::trace::SnapshotOptions snapshot_options;
        snapshot_options.pretty = false;
        (void)game.snapshot_json(snapshot_options);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        PerformanceCounters counters;
        run_profile(options, counters);

        if (options.json) {
            std::cout << performance_counters_to_json(counters) << '\n';
        } else {
            std::cout << "profile_smoke ok: games=" << options.games
                      << " seed=" << options.seed
                      << " apply_calls=" << counters.apply_calls
                      << " legal_actions_calls=" << counters.legal_actions_calls
                      << " target_selector_calls=" << counters.target_selector_calls
                      << " entity_index_builds=" << counters.entity_index_builds
                      << " kernel_tasks=" << counters.kernel_tasks_processed
                      << '\n';
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "profile_smoke error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
