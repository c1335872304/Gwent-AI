#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gwent/core/invariants.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/action.hpp"
#include "gwent/engine/effect_registry.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/game/setup.hpp"
#include "gwent/trace/state_snapshot.hpp"

namespace gwent::api {

struct MatchConfig {
    std::uint64_t seed = 0;
    std::optional<PlayerId> starting_player_id;
    bool shuffle_decks = true;
    KernelConfig kernel_config{};
};

struct MatchSpec {
    DeckSpec deck0;
    DeckSpec deck1;
};

class Game {
public:
    Game() = default;

    [[nodiscard]] static Game create(
        const MatchSpec& spec,
        EffectRegistry effects,
        const MatchConfig& config = {}
    );

    [[nodiscard]] const GameState& state() const noexcept;
    [[nodiscard]] GameState& mutable_state() noexcept;

    [[nodiscard]] std::vector<Action> legal_actions() const;
    [[nodiscard]] Decision current_decision() const;
    [[nodiscard]] KernelResult apply(const Action& action);

    [[nodiscard]] InvariantReport validate(const InvariantOptions& options = {}) const;
    [[nodiscard]] bool is_finished() const noexcept;
    [[nodiscard]] std::optional<PlayerId> winner() const noexcept;

    [[nodiscard]] std::string snapshot_json(const trace::SnapshotOptions& options = {}) const;
    [[nodiscard]] std::string checksum(const trace::SnapshotOptions& options = {}) const;

private:
    GameState state_{};
    EffectRegistry effects_{};
    KernelConfig kernel_config_{};
};

using DeckAMatchConfig = MatchConfig;

// A small public facade around the rule core. It intentionally exposes stable
// player-facing operations and keeps setup/effect-registry/kernel wiring out of
// caller code. Lower-level engine types remain available for tools and tests.
class DeckAGame {
public:
    DeckAGame() = default;

    [[nodiscard]] static DeckAGame create(const DeckAMatchConfig& config = {});

    [[nodiscard]] const GameState& state() const noexcept;
    [[nodiscard]] GameState& mutable_state() noexcept;

    [[nodiscard]] std::vector<Action> legal_actions() const;
    [[nodiscard]] Decision current_decision() const;
    [[nodiscard]] KernelResult apply(const Action& action);

    [[nodiscard]] InvariantReport validate(const InvariantOptions& options = {}) const;
    [[nodiscard]] bool is_finished() const noexcept;
    [[nodiscard]] std::optional<PlayerId> winner() const noexcept;

    [[nodiscard]] std::string snapshot_json(const trace::SnapshotOptions& options = {}) const;
    [[nodiscard]] std::string checksum(const trace::SnapshotOptions& options = {}) const;

private:
    Game game_{};
};

}  // namespace gwent::api
