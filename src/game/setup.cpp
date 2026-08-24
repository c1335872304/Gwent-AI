#include "gwent/game/setup.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace gwent {
namespace {

class SetupRng {
public:
    explicit SetupRng(std::uint64_t seed) : state_(splitmix64(seed)) {
        if (state_ == 0) {
            state_ = 0xA5A5A5A5A5A5A5A5ull;
        }
    }

    [[nodiscard]] PlayerId choose_player() {
        return static_cast<PlayerId>(randbelow(2));
    }

    [[nodiscard]] std::uint64_t state() const noexcept { return state_; }

    template <typename T>
    void shuffle(std::vector<T>& values) {
        // Match the Python core's FastRng.shuffle exactly: Fisher-Yates from
        // the end, with randbelow(i + 1). This keeps setup golden traces stable
        // across Python and C++ for the same deck order and seed.
        for (std::size_t i = values.size(); i > 1; --i) {
            const std::size_t j = static_cast<std::size_t>(randbelow(static_cast<std::uint64_t>(i)));
            std::swap(values[i - 1], values[j]);
        }
    }

private:
    static constexpr std::uint64_t kMask = 0xffffffffffffffffull;

    static std::uint64_t splitmix64(std::uint64_t x) {
        x = (x + 0x9E3779B97F4A7C15ull) & kMask;
        std::uint64_t z = x;
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull) & kMask;
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EBull) & kMask;
        return (z ^ (z >> 31)) & kMask;
    }

    [[nodiscard]] std::uint64_t next_u64() {
        std::uint64_t x = state_;
        x ^= (x >> 12) & kMask;
        x ^= (x << 25) & kMask;
        x ^= (x >> 27) & kMask;
        state_ = x & kMask;
        return (x * 0x2545F4914F6CDD1Dull) & kMask;
    }

    [[nodiscard]] std::uint64_t randbelow(std::uint64_t n) {
        if (n == 0) {
            throw std::invalid_argument("randbelow requires n > 0");
        }
        const std::uint64_t remainder = ((kMask % n) + 1) % n;
        const std::uint64_t limit = std::uint64_t{0} - remainder;
        while (true) {
            const std::uint64_t x = next_u64();
            if (remainder == 0 || x < limit) {
                return x % n;
            }
        }
    }

    std::uint64_t state_;
};

void require_player_id(PlayerId player_id, const char* name) {
    if (!is_valid_player_id(player_id)) {
        throw std::out_of_range(std::string{name} + " must be 0 or 1");
    }
}

void require_non_negative(int value, const char* name) {
    if (value < 0) {
        throw std::invalid_argument(std::string{name} + " must be non-negative");
    }
}

void validate_config(const MatchSetupConfig& config) {
    require_non_negative(config.hand_limit, "hand_limit");
    require_non_negative(config.opening_hand_size, "opening_hand_size");
    require_non_negative(config.opening_mulligans_first, "opening_mulligans_first");
    require_non_negative(config.opening_mulligans_second, "opening_mulligans_second");
    require_non_negative(config.between_round_draw, "between_round_draw");
    require_non_negative(config.round_mulligan_base, "round_mulligan_base");
    if (config.row_capacity <= 0) {
        throw std::invalid_argument("row_capacity must be positive");
    }
    if (config.starting_player_id.has_value()) {
        require_player_id(config.starting_player_id.value(), "starting_player_id");
    }
}

void validate_deck_spec(const DeckSpec& deck, PlayerId owner) {
    if (deck.leader.card_type != CardType::Leader) {
        throw std::invalid_argument("deck leader for player " + std::to_string(owner) + " must have CardType::Leader");
    }
    if (deck.stratagem.card_type != CardType::Stratagem) {
        throw std::invalid_argument("deck stratagem for player " + std::to_string(owner) + " must have CardType::Stratagem");
    }
    for (const auto& entry : deck.cards) {
        if (entry.count <= 0) {
            throw std::invalid_argument("deck card counts must be positive");
        }
    }
}

void write_config(GameState& state, const MatchSetupConfig& config) {
    state.match_config["standard_deck_game"] = "true";
    state.match_config["seed"] = std::to_string(config.seed);
    state.match_config["hand_limit"] = std::to_string(config.hand_limit);
    state.match_config["opening_hand_size"] = std::to_string(config.opening_hand_size);
    state.match_config["opening_mulligans_first"] = std::to_string(config.opening_mulligans_first);
    state.match_config["opening_mulligans_second"] = std::to_string(config.opening_mulligans_second);
    state.match_config["between_round_draw"] = std::to_string(config.between_round_draw);
    state.match_config["round_mulligan_base"] = std::to_string(config.round_mulligan_base);
    state.match_config["row_capacity"] = std::to_string(config.row_capacity);
}

void add_deck_to_state(GameState& state, const DeckSpec& deck, PlayerId owner) {
    auto& player = state.player(owner);
    for (const auto& entry : deck.cards) {
        for (int copy = 0; copy < entry.count; ++copy) {
            const auto id = state.add_card(entry.definition, owner, Location{owner, Zone::Deck, player.deck.size()});
            player.starting_deck.push_back(state.cards.at(id).definition->id);
        }
    }
    state.add_card(deck.leader, owner, Location{owner, Zone::Leader, 0});
}

const DeckSpec& deck_for_player(const DeckSpec& deck0, const DeckSpec& deck1, PlayerId player_id) {
    return player_id == kPlayerZero ? deck0 : deck1;
}

PlayerPreparationLog finalize_draw_log(GameState& state, PlayerId player_id, PlayerPreparationLog log) {
    const auto& player = state.player(player_id);
    log.actual_draw_count = static_cast<int>(log.drawn.size());
    log.missing_draw_count = std::max(0, log.requested_draw_count - log.actual_draw_count);
    log.hand_size = static_cast<int>(player.hand.size());
    log.deck_size = static_cast<int>(player.deck.size());
    for (const EntityId entity_id : log.drawn) {
        if (const auto* card = state.find_card(entity_id)) {
            log.drawn_card_ids.push_back(card->definition->id);
        }
    }
    return log;
}

}  // namespace

GameState make_empty_standard_state(const MatchSetupConfig& config) {
    validate_config(config);

    GameState state;
    state.players = {PlayerState{kPlayerZero}, PlayerState{kPlayerOne}};
    state.status = MatchStatus::NotStarted;
    state.phase = MatchPhase::NotStarted;
    state.round_no = 0;
    state.turn_no = 0;
    state.current_player_id = kPlayerZero;
    state.starting_player_id = kPlayerZero;
    state.round_starting_player_id = kPlayerZero;
    state.rng_state = config.seed;
    write_config(state, config);
    return state;
}

PlayerPreparationLog draw_direct(GameState& state, PlayerId player_id, int count, int hand_limit) {
    require_player_id(player_id, "player_id");
    require_non_negative(count, "count");
    require_non_negative(hand_limit, "hand_limit");

    PlayerPreparationLog log;
    log.requested_draw_count = count;

    auto& player = state.player(player_id);
    for (int i = 0; i < count; ++i) {
        if (player.deck.empty() || static_cast<int>(player.hand.size()) >= hand_limit) {
            break;
        }
        const EntityId entity_id = player.deck.front();
        state.move_card(entity_id, Location{player_id, Zone::Hand, player.hand.size()});
        log.drawn.push_back(entity_id);
    }

    return finalize_draw_log(state, player_id, std::move(log));
}

OpeningSetupResult setup_standard_match(
    GameState& state,
    const DeckSpec& deck0,
    const DeckSpec& deck1,
    const MatchSetupConfig& config
) {
    validate_config(config);
    validate_deck_spec(deck0, kPlayerZero);
    validate_deck_spec(deck1, kPlayerOne);

    state = make_empty_standard_state(config);

    add_deck_to_state(state, deck0, kPlayerZero);
    add_deck_to_state(state, deck1, kPlayerOne);

    SetupRng rng(config.seed);
    if (config.shuffle_decks) {
        rng.shuffle(state.player(kPlayerZero).deck);
        rng.shuffle(state.player(kPlayerOne).deck);
        state.normalize_zone_indices(kPlayerZero, Zone::Deck);
        state.normalize_zone_indices(kPlayerOne, Zone::Deck);
    }

    const PlayerId starter = config.starting_player_id.has_value() ? config.starting_player_id.value() : rng.choose_player();
    require_player_id(starter, "starting_player_id");
    state.rng_state = rng.state();

    OpeningSetupResult result;
    result.starting_player_id = starter;
    result.stratagem_owner = starter;

    for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
        PlayerPreparationLog log = draw_direct(state, player_id, config.opening_hand_size, config.hand_limit);
        log.mulligan_limit = (player_id == starter) ? config.opening_mulligans_first : config.opening_mulligans_second;
        state.player(player_id).mulligans_available = log.mulligan_limit;
        state.player(player_id).cards_drawn_this_round = log.actual_draw_count;
        result.players[static_cast<std::size_t>(player_id)] = std::move(log);
    }

    const DeckSpec& starter_deck = deck_for_player(deck0, deck1, starter);
    const auto& melee = state.player(starter).row(Zone::Melee);
    const EntityId stratagem_id = state.add_card(
        starter_deck.stratagem,
        starter,
        Location{starter, Zone::Melee, melee.size()}
    );
    result.stratagem_entity_id = stratagem_id;
    result.stratagem_location = state.location_of(stratagem_id).value();

    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.engine_phase = EnginePhase::ActionWindow;
    state.round_no = 1;
    state.turn_no = 1;
    state.current_player_id = starter;
    state.starting_player_id = starter;
    state.round_starting_player_id = starter;
    // 换牌按本轮先手 -> 后手顺序进行；current_player_id 仍保留真正的回合先手。
    state.mulligan_player_id = starter;
    state.match_config["starting_player_id"] = std::to_string(starter);
    state.match_config["stratagem_owner"] = std::to_string(starter);
    state.match_config["stratagem_entity_id"] = std::to_string(stratagem_id);
    state.match_config["stratagem_zone"] = std::string{to_string(Zone::Melee)};

    return result;
}

RoundPreparationResult prepare_standard_round_hand(
    GameState& state,
    int round_no,
    PlayerId round_starting_player_id,
    const MatchSetupConfig& config
) {
    validate_config(config);
    if (round_no < 2) {
        throw std::invalid_argument("prepare_standard_round_hand is for round 2 or later");
    }
    require_player_id(round_starting_player_id, "round_starting_player_id");

    RoundPreparationResult result;
    result.round_no = round_no;
    result.round_starting_player_id = round_starting_player_id;

    for (PlayerId player_id : {kPlayerZero, kPlayerOne}) {
        PlayerPreparationLog log = draw_direct(state, player_id, config.between_round_draw, config.hand_limit);
        const int extra_mulligans = std::max(0, config.between_round_draw - log.actual_draw_count);
        log.missing_draw_count = extra_mulligans;
        log.mulligan_limit = config.round_mulligan_base + extra_mulligans;
        state.player(player_id).mulligans_available = log.mulligan_limit;
        state.player(player_id).cards_drawn_this_round = log.actual_draw_count;
        state.player(player_id).passed = false;
        state.player(player_id).pass_reason = PassReason::None;
        result.players[static_cast<std::size_t>(player_id)] = std::move(log);
    }

    state.round_no = round_no;
    state.turn_no = 1;
    state.current_player_id = round_starting_player_id;
    state.round_starting_player_id = round_starting_player_id;
    state.mulligan_player_id = round_starting_player_id;
    state.phase = MatchPhase::Playing;
    state.engine_phase = EnginePhase::ActionWindow;
    if (state.status != MatchStatus::Finished) {
        state.status = MatchStatus::Running;
    }

    return result;
}

}  // namespace gwent
