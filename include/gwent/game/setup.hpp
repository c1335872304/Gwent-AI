#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/core/ids.hpp"
#include "gwent/core/state.hpp"

namespace gwent {

struct DeckCardSpec {
    CardDefinition definition;
    int count = 1;
};

struct DeckSpec {
    std::vector<DeckCardSpec> cards;
    CardDefinition leader;
    CardDefinition stratagem;
};

struct MatchSetupConfig {
    std::uint64_t seed = 0;
    std::optional<PlayerId> starting_player_id;
    int hand_limit = 10;
    int opening_hand_size = 10;
    int opening_mulligans_first = 3;
    int opening_mulligans_second = 2;
    int between_round_draw = 3;
    int round_mulligan_base = 2;
    int row_capacity = 9;
    bool shuffle_decks = true;
};

struct PlayerPreparationLog {
    std::vector<EntityId> drawn;
    std::vector<std::string> drawn_card_ids;
    int requested_draw_count = 0;
    int actual_draw_count = 0;
    int missing_draw_count = 0;
    int mulligan_limit = 0;
    int hand_size = 0;
    int deck_size = 0;
};

struct OpeningSetupResult {
    PlayerId starting_player_id = kPlayerZero;
    PlayerId stratagem_owner = kPlayerZero;
    EntityId stratagem_entity_id = kInvalidEntityId;
    Location stratagem_location{kPlayerZero, Zone::Melee, 0};
    std::array<PlayerPreparationLog, kPlayerCount> players{};
};

struct RoundPreparationResult {
    int round_no = 0;
    PlayerId round_starting_player_id = kPlayerZero;
    std::array<PlayerPreparationLog, kPlayerCount> players{};
};

[[nodiscard]] GameState make_empty_standard_state(const MatchSetupConfig& config = {});

// Moves up to count cards from the top of a player's deck into hand.
// The top of deck is index 0, matching the Python core.
[[nodiscard]] PlayerPreparationLog draw_direct(
    GameState& state,
    PlayerId player_id,
    int count,
    int hand_limit
);

// Creates a new standard match state: deck entities, leaders, opening draw,
// first-player stratagem in own melee row, and opening mulligan allowances.
[[nodiscard]] OpeningSetupResult setup_standard_match(
    GameState& state,
    const DeckSpec& deck0,
    const DeckSpec& deck1,
    const MatchSetupConfig& config = {}
);

// Applies the round 2/3 hand rule: each player tries to draw 3, receives 2
// base mulligans, and gains one extra mulligan for each missing draw.
[[nodiscard]] RoundPreparationResult prepare_standard_round_hand(
    GameState& state,
    int round_no,
    PlayerId round_starting_player_id,
    const MatchSetupConfig& config = {}
);

}  // namespace gwent
