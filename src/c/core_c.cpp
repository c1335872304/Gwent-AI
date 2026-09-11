#include "gwent/c/core.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <new>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef GWENT_HAS_OPENMP
#include <omp.h>
#endif

#include "gwent/api/core.hpp"
#include "gwent/cards/supported_cards.hpp"
#include "gwent/engine/primitives.hpp"
#include "gwent/engine/row_effects.hpp"
#include "gwent/generated/supported_card_data.hpp"
#include "gwent/rl/strategic_reward.hpp"
#include "gwent/trace/state_snapshot.hpp"

struct gwent_deck_a_game {
    gwent::api::DeckAGame impl;
};

struct gwent_rl_round_reward_tracker {
    int round_no{0};
    std::array<int, 2> cards_spent{0, 0};
};

struct gwent_rl_env {
    gwent_rl_config config{};
    gwent::api::Game game{};
    std::vector<gwent::Action> actions{};
    gwent_rl_observation observation{};
    gwent_rl_step_result last_result{};
    std::uint64_t decision_serial{0};
    std::vector<int> object_index_scratch{};
    gwent_rl_round_reward_tracker round_reward_tracker{};
};

struct gwent_rl_collector {
    gwent_rl_collector_config config{};
    std::vector<gwent_rl_env*> envs{};
    std::vector<std::uint64_t> next_seeds{};
    std::vector<gwent_rl_matchup_config> matchups{};
    std::uint64_t matchup_weight_total{0};

    std::vector<gwent_rl_batch_item> items{};
    std::vector<gwent_rl_batch_step_result> apply_results{};

    std::vector<float> global_features{};
    std::vector<int> object_entity_ids{};
    std::vector<int> object_card_ids{};
    std::vector<int> object_owner_ids{};
    std::vector<int> object_controller_ids{};
    std::vector<int> object_zone_ids{};
    std::vector<int> object_row_ids{};
    std::vector<int> object_slot_indices{};
    std::vector<unsigned char> object_mask{};
    std::vector<float> object_features{};

    std::vector<int> option_kind_ids{};
    std::vector<int> option_card_ids{};
    std::vector<int> option_source_object_indices{};
    std::vector<int> option_target_object_indices{};
    std::vector<int> option_target_side_ids{};
    std::vector<int> option_target_zone_ids{};
    std::vector<int> option_target_row_ids{};
    std::vector<int> option_insert_positions{};
    std::vector<int> option_hand_slot_indices{};
    std::vector<std::uint64_t> option_stable_hashes{};
    std::vector<unsigned char> option_mask{};
    std::vector<float> option_features{};

    std::vector<int> prefix_kind_ids{};
    std::vector<int> prefix_source_object_indices{};
    std::vector<int> prefix_target_object_indices{};
    std::vector<int> prefix_card_ids{};
    std::vector<int> prefix_row_ids{};
    std::vector<int> prefix_insert_positions{};
    std::vector<unsigned char> prefix_mask{};

    std::size_t completed_episodes{0};
    std::size_t total_steps{0};
    std::size_t total_resets{0};
    std::size_t max_total_resets{std::numeric_limits<std::size_t>::max()};

    std::size_t collector_threads{1};
    std::vector<unsigned char> stepped_flags{};
};

namespace {

void detach_pending_resolution_frame(gwent::GameState& state) {
    if (!state.pending_choice.has_value() || !state.pending_choice->resolution_frame) {
        return;
    }

    // A PendingChoice owns its in-progress resolution through a shared_ptr so
    // staged choices within one game can resume the same task tail. A product
    // preview, however, is a separate game branch: sharing this frame would
    // let a choice made by the preview append to the live game's prefix and
    // mutate its budgets/trigger stack. Copy all mutable frame state here.
    const std::shared_ptr<gwent::ResolutionFrame>& source = state.pending_choice->resolution_frame;
    auto detached = std::make_shared<gwent::ResolutionFrame>(*source);
    if (source->root_state_before) {
        detached->root_state_before = std::make_shared<gwent::GameState>(*source->root_state_before);
    }
    state.pending_choice->resolution_frame = std::move(detached);
}

class RlSurfaceOverflow : public std::runtime_error {
public:
    RlSurfaceOverflow(gwent_c_result_code code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] gwent_c_result_code code() const noexcept { return code_; }
private:
    gwent_c_result_code code_;
};

class RlOptionOverflow final : public RlSurfaceOverflow {
public:
    explicit RlOptionOverflow(std::size_t count)
        : RlSurfaceOverflow(
            GWENT_C_OPTION_OVERFLOW,
            "RL legal action count " + std::to_string(count)
                + " exceeds GWENT_RL_MAX_OPTIONS=" + std::to_string(GWENT_RL_MAX_OPTIONS)) {}
};

class RlObjectOverflow final : public RlSurfaceOverflow {
public:
    explicit RlObjectOverflow(std::size_t count)
        : RlSurfaceOverflow(
            GWENT_C_OBJECT_OVERFLOW,
            "RL object count " + std::to_string(count)
                + " exceeds GWENT_RL_MAX_OBJECTS=" + std::to_string(GWENT_RL_MAX_OBJECTS)) {}
};

class RlPrefixOverflow final : public RlSurfaceOverflow {
public:
    explicit RlPrefixOverflow(std::size_t count)
        : RlSurfaceOverflow(
            GWENT_C_PREFIX_OVERFLOW,
            "RL prefix count " + std::to_string(count)
                + " exceeds GWENT_RL_MAX_PREFIX=" + std::to_string(GWENT_RL_MAX_PREFIX)) {}
};

gwent::InvariantPolicy to_cpp_policy(gwent_c_invariant_policy policy) noexcept {
    switch (policy) {
        case GWENT_C_INVARIANT_BEFORE_APPLY:
            return gwent::InvariantPolicy::BeforeApply;
        case GWENT_C_INVARIANT_AFTER_APPLY:
            return gwent::InvariantPolicy::AfterApply;
        case GWENT_C_INVARIANT_BEFORE_AND_AFTER_APPLY:
            return gwent::InvariantPolicy::BeforeAndAfterApply;
        case GWENT_C_INVARIANT_DISABLED:
        default:
            return gwent::InvariantPolicy::Disabled;
    }
}

gwent_c_action_status to_c_status(gwent::ActionStatus status) noexcept {
    switch (status) {
        case gwent::ActionStatus::Applied:
            return GWENT_C_ACTION_APPLIED;
        case gwent::ActionStatus::IllegalAction:
            return GWENT_C_ACTION_ILLEGAL_ACTION;
        case gwent::ActionStatus::InvalidPlayer:
            return GWENT_C_ACTION_INVALID_PLAYER;
        case gwent::ActionStatus::NotCurrentPlayer:
            return GWENT_C_ACTION_NOT_CURRENT_PLAYER;
        case gwent::ActionStatus::InvalidPhase:
            return GWENT_C_ACTION_INVALID_PHASE;
        case gwent::ActionStatus::InvalidSource:
            return GWENT_C_ACTION_INVALID_SOURCE;
        case gwent::ActionStatus::InvalidTarget:
            return GWENT_C_ACTION_INVALID_TARGET;
        case gwent::ActionStatus::EmptyDeck:
            return GWENT_C_ACTION_EMPTY_DECK;
        case gwent::ActionStatus::UnsupportedAction:
            return GWENT_C_ACTION_UNSUPPORTED_ACTION;
        case gwent::ActionStatus::TaskLimitExceeded:
            return GWENT_C_ACTION_TASK_LIMIT_EXCEEDED;
        case gwent::ActionStatus::InvariantViolation:
            return GWENT_C_ACTION_INVARIANT_VIOLATION;
    }
    return GWENT_C_ACTION_UNKNOWN;
}

gwent_c_result_code set_string(std::string value, gwent_c_string* out) noexcept {
    if (out == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    out->data = nullptr;
    out->size = 0;

    char* buffer = static_cast<char*>(std::malloc(value.size() + 1));
    if (buffer == nullptr) {
        return GWENT_C_ALLOCATION_FAILURE;
    }
    if (!value.empty()) {
        std::memcpy(buffer, value.data(), value.size());
    }
    buffer[value.size()] = '\0';
    out->data = buffer;
    out->size = value.size();
    return GWENT_C_OK;
}


constexpr const char* kGlobalFeatureNames[GWENT_RL_GLOBAL_FEATURE_COUNT] = {
    "bias",
    "match_status",
    "match_phase",
    "round_no",
    "turn_no",
    "actor_id",
    "opponent_id",
    "actor_score",
    "opponent_score",
    "score_diff_actor_minus_opponent",
    "actor_melee_score",
    "actor_ranged_score",
    "opponent_melee_score",
    "opponent_ranged_score",
    "actor_hand_count",
    "opponent_hand_count",
    "actor_cemetery_count",
    "opponent_cemetery_count",
    "actor_passed",
    "opponent_passed",
    "actor_round_wins",
    "opponent_round_wins",
    "actor_leader_used",
    "opponent_leader_used",
    "has_pending_choice",
    "pending_choice_kind",
    "actor_mulligans_available",
    "opponent_mulligans_available",
    "actor_is_current_player",
    "done",
    "actor_melee_frost_duration",
    "actor_ranged_frost_duration",
    "opponent_melee_frost_duration",
    "opponent_ranged_frost_duration",
};

constexpr const char* kObjectFeatureNames[GWENT_RL_OBJECT_FEATURE_COUNT] = {
    "bias",
    "card_type",
    "owner_is_actor",
    "controller_is_actor",
    "zone_id",
    "row_id",
    "slot_index",
    "base_power",
    "power",
    "power_minus_base",
    "armor",
    "bleeding",
    "vitality",
    "poison",
    "shield",
    "locked",
    "veil",
    "defender",
    "doomed",
    "order_charges",
    "cooldown",
    "countdown",
    "is_unit",
    "has_power",
    "infusion_count",
    "saved_frost_melee",
    "saved_frost_ranged",
};

constexpr const char* kOptionFeatureNames[GWENT_RL_OPTION_FEATURE_COUNT] = {
    "bias",
    "option_kind",
    "player_is_actor",
    "has_source_object",
    "has_target_object",
    "has_row_target",
    "source_object_index",
    "target_object_index",
    "target_side_is_actor",
    "target_row_id",
    "target_power",
    "target_armor",
    "target_bleeding",
    "target_vitality",
    "target_locked",
    "target_is_unit",
};

const char* indexed_name(const char* const* names, std::size_t count, std::size_t index) noexcept {
    return index < count ? names[index] : "";
}

std::string legal_actions_to_json(const gwent::api::DeckAGame& game) {
    const auto actions = game.legal_actions();
    std::string json;
    json += "[";
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (i != 0) {
            json += ",";
        }
        json += gwent::trace::action_to_json(actions[i]);
    }
    json += "]";
    return json;
}



gwent_rl_reward_config default_reward_config() noexcept {
    gwent_rl_reward_config config{};
    config.mode = GWENT_RL_REWARD_TERMINAL_ONLY;
    config.final_win = 1.0F;
    config.final_loss = -1.0F;
    config.final_draw = 0.0F;
    config.round_win = 0.22F;
    config.round_loss = -0.12F;
    config.round_draw = 0.03F;
    config.score_delta_weight = 0.002F;
    config.damage_weight = 0.002F;
    config.boost_weight = 0.001F;
    config.armor_weight = 0.001F;
    config.status_weight = 0.004F;
    config.removal_weight = 0.02F;
    config.card_play_cost = 0.03F;
    config.leader_use_cost = 0.01F;
    config.order_use_cost = 0.005F;
    config.discard_card_cost = 0.01F;
    config.secured_pass_reward = 0.08F;
    config.secured_overplay_cost = 0.15F;
    config.hand_delta_round_weight = 0.03F;
    config.score_delta_contested_only = 1;

    // Strategic/resource shaping. These defaults intentionally keep the final
    // match result dominant while teaching Gwent-specific round efficiency.
    // R1: earn one card=0.15, even=0.10, lose one=0.05, lose two=-0.03.
    config.round1_win_base = 0.10F;
    config.round1_card_cost_weight = 0.05F;
    config.round1_excess_card_penalty = 0.03F;
    // R2: earn one card=0.15, even=0.07, lose one=-0.04, lose two=-0.15.
    config.round2_win_base = 0.07F;
    config.round2_card_cost_weight = 0.08F;
    config.round2_excess_card_penalty = 0.03F;
    config.close_round_win_bonus = 0.02F;
    config.close_round_win_margin_cap = 15;

    config.max_step_reward_abs = 1.25F;
    return config;
}


std::size_t read_collector_threads() noexcept {
    const char* raw = std::getenv("GWENT_COLLECTOR_THREADS");
    if (raw == nullptr || raw[0] == '\0') {
        return 1;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(raw, &end, 10);
    if (end == raw || value == 0UL) {
        return 1;
    }

    return static_cast<std::size_t>(std::min<unsigned long>(value, 256UL));
}

gwent_rl_config default_rl_config() noexcept {
    gwent_rl_config config{};
    config.seed = 0;
    config.starting_player_id = -1;
    config.shuffle_decks = 1;
    config.enable_invariants = 0;
    config.current_player_perspective = 1;
    config.include_private_info = 0;
    config.reward_config = default_reward_config();
    config.player0_deck_id = GWENT_RL_DECK_A;
    config.player1_deck_id = GWENT_RL_DECK_A;
    return config;
}

gwent_rl_collector_config default_collector_config() noexcept {
    gwent_rl_collector_config config{};
    config.num_envs = 128;
    config.max_batch_size = 128;
    config.base_seed = 0;
    config.starting_player_id = -1;
    config.shuffle_decks = 1;
    config.auto_reset_done_envs = 1;
    config.enable_invariants = 0;
    config.current_player_perspective = 1;
    config.include_private_info = 0;
    config.reward_config = default_reward_config();
    config.player0_deck_id = GWENT_RL_DECK_A;
    config.player1_deck_id = GWENT_RL_DECK_A;
    return config;
}

gwent_rl_config env_config_from_collector(const gwent_rl_collector_config& config, std::uint64_t seed) noexcept {
    gwent_rl_config env_config{};
    env_config.seed = seed;
    env_config.starting_player_id = config.starting_player_id;
    env_config.shuffle_decks = config.shuffle_decks;
    env_config.enable_invariants = config.enable_invariants;
    env_config.current_player_perspective = config.current_player_perspective;
    env_config.include_private_info = config.include_private_info;
    env_config.reward_config = config.reward_config;
    env_config.player0_deck_id = config.player0_deck_id;
    env_config.player1_deck_id = config.player1_deck_id;
    return env_config;
}

gwent::api::MatchConfig to_cpp_rl_config(const gwent_rl_config& config) {
    gwent::api::MatchConfig cpp_config;
    cpp_config.seed = config.seed;
    if (config.starting_player_id >= 0) {
        cpp_config.starting_player_id = static_cast<gwent::PlayerId>(config.starting_player_id);
    }
    cpp_config.shuffle_decks = config.shuffle_decks != 0;
    cpp_config.kernel_config.invariant_policy =
        config.enable_invariants != 0 ? gwent::InvariantPolicy::AfterApply : gwent::InvariantPolicy::Disabled;
    cpp_config.kernel_config.leader_action_consumes_turn = false;
    cpp_config.kernel_config.order_action_consumes_turn = false;
    return cpp_config;
}

gwent::DeckSpec deck_spec_for_rl_id(int deck_id) {
    const auto ids = gwent::generated::supported_deck_ids();
    if (deck_id < 0 || static_cast<std::size_t>(deck_id) >= ids.size()) {
        throw std::invalid_argument("unknown RL deck id");
    }
    return gwent::generated::make_supported_deck_spec(ids[static_cast<std::size_t>(deck_id)]);
}

gwent_rl_matchup_config select_matchup(const gwent_rl_collector& collector, std::uint64_t game_ordinal) {
    if (collector.matchups.empty()) {
        return {collector.config.player0_deck_id, collector.config.player1_deck_id, 1};
    }
    std::uint64_t slot = game_ordinal % collector.matchup_weight_total;
    for (const auto& matchup : collector.matchups) {
        if (slot < matchup.weight) {
            return matchup;
        }
        slot -= matchup.weight;
    }
    return collector.matchups.back();
}

void apply_matchup(gwent_rl_config& config, const gwent_rl_matchup_config& matchup) noexcept {
    config.player0_deck_id = matchup.player0_deck_id;
    config.player1_deck_id = matchup.player1_deck_id;
}

gwent::api::Game make_rl_game(const gwent_rl_config& config) {
    gwent::api::MatchSpec match_spec{
        deck_spec_for_rl_id(config.player0_deck_id),
        deck_spec_for_rl_id(config.player1_deck_id),
    };
    gwent::EffectRegistry effects;
    gwent::supported_cards::register_supported_card_effects(effects);
    return gwent::api::Game::create(std::move(match_spec), std::move(effects), to_cpp_rl_config(config));
}

int stable_card_id(const gwent::RuntimeCard& card) noexcept {
    try {
        return std::stoi(card.definition->id);
    } catch (...) {
        return -1;
    }
}

int zone_id(gwent::Zone zone) noexcept {
    return static_cast<int>(zone);
}

int row_id(gwent::Zone zone) noexcept {
    if (zone == gwent::Zone::Melee) {
        return 0;
    }
    if (zone == gwent::Zone::Ranged) {
        return 1;
    }
    return -1;
}

gwent_rl_option_kind to_rl_option_kind(gwent::ActionType type) noexcept {
    switch (type) {
        case gwent::ActionType::Pass:
            return GWENT_RL_OPTION_PASS;
        case gwent::ActionType::EndTurn:
            return GWENT_RL_OPTION_END_TURN;
        case gwent::ActionType::PlayCard:
            return GWENT_RL_OPTION_PLAY_CARD;
        case gwent::ActionType::DiscardCard:
            return GWENT_RL_OPTION_DISCARD_CARD;
        case gwent::ActionType::Mulligan:
            return GWENT_RL_OPTION_MULLIGAN;
        case gwent::ActionType::KeepHand:
            return GWENT_RL_OPTION_KEEP_HAND;
        case gwent::ActionType::UseLeader:
            return GWENT_RL_OPTION_USE_LEADER;
        case gwent::ActionType::UseOrder:
            return GWENT_RL_OPTION_USE_ORDER;
        case gwent::ActionType::ChooseCardTarget:
            return GWENT_RL_OPTION_CHOOSE_CARD;
        case gwent::ActionType::ChooseRowTarget:
            return GWENT_RL_OPTION_CHOOSE_ROW;
        case gwent::ActionType::ChooseInsertPosition:
            return GWENT_RL_OPTION_CHOOSE_INSERT_POSITION;
    }
    return GWENT_RL_OPTION_UNKNOWN;
}

gwent_rl_decision_kind to_rl_decision_kind(gwent::DecisionType type, const gwent::GameState& state) noexcept {
    if (state.status == gwent::MatchStatus::Finished) {
        return GWENT_RL_DECISION_FINISHED;
    }
    switch (type) {
        case gwent::DecisionType::TurnAction:
            return GWENT_RL_DECISION_TURN;
        case gwent::DecisionType::Mulligan:
            return GWENT_RL_DECISION_MULLIGAN;
        case gwent::DecisionType::ChooseTarget:
            if (state.pending_choice.has_value() && state.pending_choice->kind == gwent::PendingChoiceKind::InsertPosition) {
                return GWENT_RL_DECISION_INSERT_POSITION;
            }
            if (state.pending_choice.has_value() && state.pending_choice->kind == gwent::PendingChoiceKind::RowTarget) {
                return GWENT_RL_DECISION_ROW_TARGET;
            }
            return GWENT_RL_DECISION_CARD_TARGET;
        case gwent::DecisionType::None:
        default:
            return GWENT_RL_DECISION_NONE;
    }
}

float clamp_reward_value(float value, float limit) noexcept {
    const float lim = std::abs(limit);
    if (lim <= 0.0F) {
        return value;
    }
    return std::max(-lim, std::min(lim, value));
}

void add_zero_sum_reward(std::array<float, 2>& rewards, int player_id, float amount) noexcept {
    if (amount == 0.0F || player_id < 0 || player_id > 1) {
        return;
    }
    rewards[static_cast<std::size_t>(player_id)] += amount;
    rewards[static_cast<std::size_t>(1 - player_id)] -= amount;
}

int controller_for_entity(const gwent::GameState& state, gwent::EntityId entity_id) noexcept {
    const auto* card = state.find_card(entity_id);
    return card == nullptr ? -1 : static_cast<int>(card->controller_id);
}

int round_winner_from_event(const gwent::EventRecord& event) noexcept {
    if (event.kind != gwent::EventKind::RoundFinished) {
        return -2;
    }
    if (event.message == "winner=0") {
        return 0;
    }
    if (event.message == "winner=1") {
        return 1;
    }
    if (event.message == "winner=tie") {
        return -1;
    }
    return -2;
}

bool is_dense_mode(const gwent_rl_reward_config& config) noexcept {
    return config.mode == GWENT_RL_REWARD_DENSE_SHAPING;
}

bool is_round_mode(const gwent_rl_reward_config& config) noexcept {
    return config.mode == GWENT_RL_REWARD_ROUND_SHAPING || config.mode == GWENT_RL_REWARD_DENSE_SHAPING;
}

bool is_strategic_mode(const gwent_rl_reward_config& config) noexcept {
    return config.mode == GWENT_RL_REWARD_STRATEGIC_SHAPING;
}

void reset_round_reward_tracker(gwent_rl_env& env) noexcept {
    env.round_reward_tracker.round_no = env.game.state().round_no;
    env.round_reward_tracker.cards_spent = {0, 0};
}

bool entity_was_in_hand(const gwent::GameState& state, gwent::EntityId entity_id, int player_id) noexcept {
    if (player_id < 0 || player_id > 1 || entity_id == gwent::kInvalidEntityId) {
        return false;
    }
    const auto location = state.location_of(entity_id);
    return location.has_value()
        && static_cast<int>(location->side) == player_id
        && location->zone == gwent::Zone::Hand;
}

std::array<int, 2> hand_cards_spent_this_step(
    const gwent::GameState& pre_state,
    const gwent::Action& action,
    const gwent::KernelResult* result) noexcept {
    std::array<int, 2> spent{0, 0};
    if (result == nullptr || !result->applied) {
        return spent;
    }

    // Event lists are tiny. Scan earlier events for duplicate source ids instead
    // of allocating a temporary set/vector on every RL step.
    for (std::size_t i = 0; i < result->events.size(); ++i) {
        const auto& event = result->events[i];
        const int player_id = static_cast<int>(event.actor_id);
        if (event.kind != gwent::EventKind::CardPlayed
            || player_id < 0 || player_id > 1
            || !entity_was_in_hand(pre_state, event.source_entity_id, player_id)) {
            continue;
        }
        bool seen = false;
        for (std::size_t j = 0; j < i; ++j) {
            if (result->events[j].kind == gwent::EventKind::CardPlayed
                && result->events[j].source_entity_id == event.source_entity_id) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            ++spent[static_cast<std::size_t>(player_id)];
        }
    }

    const int action_player = static_cast<int>(action.player_id);
    if (action.type == gwent::ActionType::DiscardCard
        && action_player >= 0 && action_player <= 1
        && entity_was_in_hand(pre_state, action.source_entity_id, action_player)) {
        ++spent[static_cast<std::size_t>(action_player)];
    }
    return spent;
}

gwent::rl::StrategicRoundRewardParams strategic_reward_params(const gwent_rl_reward_config& config) noexcept {
    gwent::rl::StrategicRoundRewardParams params;
    params.round1_win_base = config.round1_win_base;
    params.round1_card_cost_weight = config.round1_card_cost_weight;
    params.round1_excess_card_penalty = config.round1_excess_card_penalty;
    params.round2_win_base = config.round2_win_base;
    params.round2_card_cost_weight = config.round2_card_cost_weight;
    params.round2_excess_card_penalty = config.round2_excess_card_penalty;
    params.close_round_win_bonus = config.close_round_win_bonus;
    params.close_round_win_margin_cap = config.close_round_win_margin_cap;
    return params;
}

std::array<float, 2> compute_rl_rewards(
    const gwent_rl_reward_config& config,
    gwent_rl_env& env,
    const gwent::api::Game& game_before,
    const gwent::api::Game& game,
    const gwent::Action& action,
    const gwent::KernelResult* result,
    const std::array<int, 2>& pre_scores) noexcept {
    std::array<float, 2> rewards{0.0F, 0.0F};
    const auto& pre_state = game_before.state();
    const auto& state = game.state();
    bool round_finished = false;
    int round_winner = -2;

    if (is_strategic_mode(config)) {
        if (env.round_reward_tracker.round_no != pre_state.round_no) {
            env.round_reward_tracker.round_no = pre_state.round_no;
            env.round_reward_tracker.cards_spent = {0, 0};
        }
        const auto spent_this_step = hand_cards_spent_this_step(pre_state, action, result);
        env.round_reward_tracker.cards_spent[0] += spent_this_step[0];
        env.round_reward_tracker.cards_spent[1] += spent_this_step[1];
    }

    if (result != nullptr && result->applied) {
        for (const auto& event : result->events) {
            const int event_round_winner = round_winner_from_event(event);
            if (event_round_winner != -2) {
                round_winner = event_round_winner;
                round_finished = true;
                if (is_round_mode(config)) {
                    if (event_round_winner == -1) {
                        rewards[0] += config.round_draw;
                        rewards[1] += config.round_draw;
                    } else {
                        rewards[static_cast<std::size_t>(event_round_winner)] += config.round_win;
                        rewards[static_cast<std::size_t>(1 - event_round_winner)] += config.round_loss;
                    }
                }
                continue;
            }

            if (!is_dense_mode(config)) {
                continue;
            }

            const int actor = static_cast<int>(event.actor_id);
            const int target_controller = controller_for_entity(state, event.target_entity_id);
            const bool actor_vs_enemy = actor >= 0 && actor <= 1 && target_controller >= 0 && target_controller <= 1 && actor != target_controller;
            const float amount = static_cast<float>(std::max(1, std::abs(event.amount)));

            if (event.kind == gwent::EventKind::CardDamaged || event.kind == gwent::EventKind::CardDrained) {
                if (actor_vs_enemy) {
                    add_zero_sum_reward(rewards, actor, config.damage_weight * amount);
                }
            } else if (event.kind == gwent::EventKind::StatusAdded) {
                if (actor_vs_enemy) {
                    add_zero_sum_reward(rewards, actor, config.status_weight * amount);
                }
            } else if (event.kind == gwent::EventKind::CardDestroyed || event.kind == gwent::EventKind::CardBanished) {
                if (actor_vs_enemy) {
                    float threat = 1.0F;
                    if (const auto* target = state.find_card(event.target_entity_id); target != nullptr) {
                        threat += 0.05F * static_cast<float>(std::max(0, target->definition->provision));
                        threat += 0.05F * static_cast<float>(std::max(0, target->state.base_power));
                    }
                    add_zero_sum_reward(rewards, actor, config.removal_weight * threat);
                }
            } else if (event.kind == gwent::EventKind::CardBoosted) {
                if (actor >= 0 && actor <= 1) {
                    add_zero_sum_reward(rewards, actor, config.boost_weight * amount);
                }
            } else if (event.kind == gwent::EventKind::ArmorAdded) {
                if (actor >= 0 && actor <= 1) {
                    add_zero_sum_reward(rewards, actor, config.armor_weight * amount);
                }
            }
        }
    }

    const int actor_for_action = static_cast<int>(action.player_id);
    const int opponent_for_action = actor_for_action >= 0 && actor_for_action <= 1 ? 1 - actor_for_action : -1;
    const bool secured_round_before = actor_for_action >= 0 && actor_for_action <= 1
        && opponent_for_action >= 0
        && pre_state.player(static_cast<gwent::PlayerId>(opponent_for_action)).passed
        && pre_scores[static_cast<std::size_t>(actor_for_action)] > pre_scores[static_cast<std::size_t>(opponent_for_action)];

    if (is_strategic_mode(config) && round_finished && round_winner >= 0 && round_winner <= 1) {
        const int loser = 1 - round_winner;
        // On an explicit PASS, pre_scores are already the final board scores for
        // the round. If the round ends automatically after a last-card play,
        // the post-action board may already have been cleaned up by the kernel,
        // so the final margin is intentionally treated as unknown and receives
        // no close-margin bonus rather than using a stale pre-action score.
        int margin = std::max(1, config.close_round_win_margin_cap);
        if (action.type == gwent::ActionType::Pass) {
            const int final_score_winner = pre_scores[static_cast<std::size_t>(round_winner)];
            const int final_score_loser = pre_scores[static_cast<std::size_t>(loser)];
            margin = std::max(1, final_score_winner - final_score_loser);
        }
        const float round_reward = gwent::rl::round_efficiency_reward(
            strategic_reward_params(config),
            pre_state.round_no,
            env.round_reward_tracker.cards_spent[static_cast<std::size_t>(round_winner)],
            env.round_reward_tracker.cards_spent[static_cast<std::size_t>(loser)],
            margin);
        add_zero_sum_reward(rewards, round_winner, round_reward);
    }

    if (is_dense_mode(config) && !round_finished) {
        const bool allow_score_delta = config.score_delta_contested_only == 0 || !secured_round_before;
        if (allow_score_delta) {
            const int post0 = state.board_score(gwent::kPlayerZero);
            const int post1 = state.board_score(gwent::kPlayerOne);
            const float delta0 = static_cast<float>((post0 - pre_scores[0]) - (post1 - pre_scores[1]));
            add_zero_sum_reward(rewards, 0, config.score_delta_weight * delta0);
        }
    }

    if (is_dense_mode(config) && result != nullptr && result->applied) {
        if (action.type == gwent::ActionType::Pass && secured_round_before && config.secured_pass_reward != 0.0F) {
            add_zero_sum_reward(rewards, actor_for_action, std::abs(config.secured_pass_reward));
        }
        if ((action.type == gwent::ActionType::PlayCard || action.type == gwent::ActionType::UseLeader || action.type == gwent::ActionType::UseOrder)
            && secured_round_before && config.secured_overplay_cost != 0.0F) {
            add_zero_sum_reward(rewards, actor_for_action, -std::abs(config.secured_overplay_cost));
        }
        if (action.type == gwent::ActionType::PlayCard && config.card_play_cost != 0.0F) {
            add_zero_sum_reward(rewards, actor_for_action, -std::abs(config.card_play_cost));
        }
        if (action.type == gwent::ActionType::DiscardCard && config.discard_card_cost != 0.0F) {
            add_zero_sum_reward(rewards, actor_for_action, -std::abs(config.discard_card_cost));
        }
        if (action.type == gwent::ActionType::UseLeader && config.leader_use_cost != 0.0F) {
            add_zero_sum_reward(rewards, actor_for_action, -std::abs(config.leader_use_cost));
        }
        if (action.type == gwent::ActionType::UseOrder && config.order_use_cost != 0.0F) {
            add_zero_sum_reward(rewards, actor_for_action, -std::abs(config.order_use_cost));
        }
    }

    if (game.is_finished()) {
        const auto winner = game.winner();
        if (!winner.has_value()) {
            rewards[0] += config.final_draw;
            rewards[1] += config.final_draw;
        } else if (*winner == 0) {
            rewards[0] += config.final_win;
            rewards[1] += config.final_loss;
        } else if (*winner == 1) {
            rewards[0] += config.final_loss;
            rewards[1] += config.final_win;
        }
    }

    if (is_strategic_mode(config) && round_finished) {
        env.round_reward_tracker.round_no = state.round_no;
        env.round_reward_tracker.cards_spent = {0, 0};
    }

    rewards[0] = clamp_reward_value(rewards[0], config.max_step_reward_abs);
    rewards[1] = clamp_reward_value(rewards[1], config.max_step_reward_abs);
    return rewards;
}

void set_object_feature(gwent_rl_observation& obs, std::size_t object_index, std::size_t feature_index, float value) noexcept {
    if (object_index >= GWENT_RL_MAX_OBJECTS || feature_index >= GWENT_RL_OBJECT_FEATURE_COUNT) {
        return;
    }
    obs.object_features[object_index * GWENT_RL_OBJECT_FEATURE_COUNT + feature_index] = value;
}

void set_option_feature(gwent_rl_observation& obs, std::size_t option_index, std::size_t feature_index, float value) noexcept {
    if (option_index >= GWENT_RL_MAX_OPTIONS || feature_index >= GWENT_RL_OPTION_FEATURE_COUNT) {
        return;
    }
    obs.option_features[option_index * GWENT_RL_OPTION_FEATURE_COUNT + feature_index] = value;
}

std::uint64_t stable_action_hash(const gwent::Action& action) noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](std::uint64_t value) noexcept {
        h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
    };
    mix(static_cast<std::uint64_t>(action.type));
    mix(static_cast<std::uint64_t>(action.player_id));
    mix(static_cast<std::uint64_t>(action.source_entity_id));
    mix(static_cast<std::uint64_t>(action.target.kind));
    mix(static_cast<std::uint64_t>(action.target.side));
    mix(static_cast<std::uint64_t>(action.target.zone));
    mix(static_cast<std::uint64_t>(action.target.entity_id));
    mix(static_cast<std::uint64_t>(action.target.definition_id));
    mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(action.insert_position)));
    return h;
}

int card_id_for_entity(const gwent::GameState& state, gwent::EntityId entity_id) noexcept {
    const auto* card = state.find_card(entity_id);
    return card == nullptr ? -1 : stable_card_id(*card);
}

int object_index_for_entity(const std::vector<int>& object_indices, gwent::EntityId entity_id) noexcept {
    if (entity_id < 0) {
        return -1;
    }
    const auto index = static_cast<std::size_t>(entity_id);
    return index < object_indices.size() ? object_indices[index] : -1;
}

int hand_slot_for_action(const gwent::GameState& state, const gwent::Action& action) noexcept {
    if (!(action.type == gwent::ActionType::PlayCard || action.type == gwent::ActionType::Mulligan)) {
        return -1;
    }
    if (action.player_id < 0 || static_cast<std::size_t>(action.player_id) >= gwent::kPlayerCount) {
        return -1;
    }
    const auto& hand = state.players[static_cast<std::size_t>(action.player_id)].hand;
    for (std::size_t i = 0; i < hand.size(); ++i) {
        if (hand[i] == action.source_entity_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void append_object(
    gwent_rl_observation& obs,
    std::vector<int>& object_indices,
    const gwent::GameState& state,
    gwent::EntityId entity_id,
    gwent::Location location,
    gwent::PlayerId actor_id) {
    if (obs.object_count >= GWENT_RL_MAX_OBJECTS) {
        throw RlObjectOverflow(obs.object_count + 1);
    }
    const auto* card = state.find_card(entity_id);
    if (card == nullptr) {
        return;
    }
    const std::size_t i = obs.object_count++;
    if (entity_id < 0) {
        throw std::runtime_error("runtime entity id must be non-negative");
    }
    const auto entity_index = static_cast<std::size_t>(entity_id);
    if (entity_index >= object_indices.size()) {
        object_indices.resize(entity_index + 1, -1);
    }
    object_indices[entity_index] = static_cast<int>(i);
    obs.object_mask[i] = 1;
    obs.object_entity_ids[i] = static_cast<int>(entity_id);
    obs.object_card_ids[i] = stable_card_id(*card);
    obs.object_owner_ids[i] = static_cast<int>(card->owner_id);
    obs.object_controller_ids[i] = static_cast<int>(card->controller_id);
    obs.object_zone_ids[i] = zone_id(location.zone);
    obs.object_row_ids[i] = row_id(location.zone);
    obs.object_slot_indices[i] = static_cast<int>(location.index);

    set_object_feature(obs, i, 0, 1.0F);
    set_object_feature(obs, i, 1, static_cast<float>(card->definition->card_type));
    set_object_feature(obs, i, 2, card->owner_id == actor_id ? 1.0F : 0.0F);
    set_object_feature(obs, i, 3, card->controller_id == actor_id ? 1.0F : 0.0F);
    set_object_feature(obs, i, 4, static_cast<float>(zone_id(location.zone)));
    set_object_feature(obs, i, 5, static_cast<float>(row_id(location.zone)));
    set_object_feature(obs, i, 6, static_cast<float>(location.index));
    set_object_feature(obs, i, 7, static_cast<float>(card->state.base_power));
    set_object_feature(obs, i, 8, static_cast<float>(card->state.power));
    set_object_feature(obs, i, 9, static_cast<float>(card->state.power - card->state.base_power));
    set_object_feature(obs, i, 10, static_cast<float>(card->state.armor));
    set_object_feature(obs, i, 11, static_cast<float>(card->state.bleeding));
    set_object_feature(obs, i, 12, static_cast<float>(card->state.vitality));
    set_object_feature(obs, i, 13, static_cast<float>(card->state.poison));
    set_object_feature(obs, i, 14, card->state.shield ? 1.0F : 0.0F);
    set_object_feature(obs, i, 15, card->state.locked ? 1.0F : 0.0F);
    set_object_feature(obs, i, 16, card->state.veil ? 1.0F : 0.0F);
    set_object_feature(obs, i, 17, card->state.defender ? 1.0F : 0.0F);
    set_object_feature(obs, i, 18, card->state.doomed ? 1.0F : 0.0F);
    set_object_feature(obs, i, 19, static_cast<float>(card->state.order_charges));
    set_object_feature(obs, i, 20, static_cast<float>(card->state.cooldown));
    set_object_feature(obs, i, 21, static_cast<float>(card->state.countdown));
    set_object_feature(obs, i, 22, card->is_unit_card() ? 1.0F : 0.0F);
    set_object_feature(obs, i, 23, card->has_power() ? 1.0F : 0.0F);
    set_object_feature(obs, i, 24, static_cast<float>(gwent::infusion_count(state, entity_id)));
    const auto memory_value = [&](std::string_view key) {
        const auto it = card->memory.find(std::string(key));
        if (it == card->memory.end()) return 0.0F;
        int value = 0;
        const auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
        return ec == std::errc{} && ptr == it->second.data() + it->second.size() ? static_cast<float>(value) : 0.0F;
    };
    set_object_feature(obs, i, 25, memory_value("saved_frost_melee"));
    set_object_feature(obs, i, 26, memory_value("saved_frost_ranged"));
}

void append_zone_objects(
    gwent_rl_observation& obs,
    std::vector<int>& object_indices,
    const gwent::GameState& state,
    gwent::PlayerId side,
    gwent::Zone zone,
    const std::vector<gwent::EntityId>& ids,
    gwent::PlayerId actor_id) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        append_object(obs, object_indices, state, ids[i], gwent::Location{side, zone, i}, actor_id);
    }
}

constexpr int kRlKnowledgeZonePublicDecklist = 8;

int stable_card_id_from_definition(const gwent::CardDefinition& definition) noexcept {
    try {
        return std::stoi(definition.id);
    } catch (...) {
        return -1;
    }
}

void append_public_decklist_definition(
    gwent_rl_observation& obs,
    const gwent::GameState& state,
    gwent::PlayerId side,
    std::string_view card_definition_id,
    gwent::PlayerId actor_id) {
    if (obs.object_count >= GWENT_RL_MAX_OBJECTS) {
        throw RlObjectOverflow(obs.object_count + 1);
    }
    if (state.card_catalog == nullptr) {
        return;
    }
    const auto definition_id = state.card_catalog->id_of(card_definition_id);
    if (!definition_id.has_value()) {
        return;
    }
    const auto& definition = state.card_catalog->at(*definition_id);
    const std::size_t i = obs.object_count++;
    obs.object_mask[i] = 1;
    obs.object_entity_ids[i] = -1;  // definition knowledge, not a runtime entity
    obs.object_card_ids[i] = stable_card_id_from_definition(definition);
    obs.object_owner_ids[i] = static_cast<int>(side);
    obs.object_controller_ids[i] = static_cast<int>(side);
    obs.object_zone_ids[i] = kRlKnowledgeZonePublicDecklist;
    obs.object_row_ids[i] = -1;
    obs.object_slot_indices[i] = -1;

    set_object_feature(obs, i, 0, 1.0F);
    set_object_feature(obs, i, 1, static_cast<float>(definition.card_type));
    set_object_feature(obs, i, 2, side == actor_id ? 1.0F : 0.0F);
    set_object_feature(obs, i, 3, side == actor_id ? 1.0F : 0.0F);
    set_object_feature(obs, i, 4, static_cast<float>(kRlKnowledgeZonePublicDecklist));
    set_object_feature(obs, i, 5, -1.0F);
    set_object_feature(obs, i, 6, -1.0F);
    set_object_feature(obs, i, 7, static_cast<float>(definition.base_power));
    set_object_feature(obs, i, 8, static_cast<float>(definition.base_power));
    set_object_feature(obs, i, 22, definition.card_type == gwent::CardType::Unit ? 1.0F : 0.0F);
    set_object_feature(obs, i, 23, definition.card_type == gwent::CardType::Unit ? 1.0F : 0.0F);
}

void append_public_decklists(
    gwent_rl_observation& obs,
    const gwent::GameState& state,
    gwent::PlayerId actor_id) {
    for (gwent::PlayerId side = 0; side < gwent::kPlayerCount; ++side) {
        const auto& starting_deck = state.player(side).starting_deck;
        for (const std::string& card_id : starting_deck) {
            append_public_decklist_definition(obs, state, side, card_id, actor_id);
        }
    }
}

void fill_globals(gwent_rl_observation& obs, const gwent::GameState& state, gwent::PlayerId actor_id) noexcept {
    const gwent::PlayerId actor = (actor_id >= 0 && static_cast<std::size_t>(actor_id) < gwent::kPlayerCount)
        ? actor_id
        : state.current_player_id;
    const gwent::PlayerId opponent = actor == 0 ? 1 : 0;
    const auto& actor_player = state.players[static_cast<std::size_t>(actor)];
    const auto& opponent_player = state.players[static_cast<std::size_t>(opponent)];

    obs.global_features[0] = 1.0F;
    obs.global_features[1] = static_cast<float>(state.status);
    obs.global_features[2] = static_cast<float>(state.phase);
    obs.global_features[3] = static_cast<float>(state.round_no);
    obs.global_features[4] = static_cast<float>(state.turn_no);
    obs.global_features[5] = static_cast<float>(actor);
    obs.global_features[6] = static_cast<float>(opponent);
    obs.global_features[7] = static_cast<float>(state.board_score(actor));
    obs.global_features[8] = static_cast<float>(state.board_score(opponent));
    obs.global_features[9] = static_cast<float>(state.board_score(actor) - state.board_score(opponent));
    obs.global_features[10] = static_cast<float>(state.row_score(actor, gwent::Zone::Melee));
    obs.global_features[11] = static_cast<float>(state.row_score(actor, gwent::Zone::Ranged));
    obs.global_features[12] = static_cast<float>(state.row_score(opponent, gwent::Zone::Melee));
    obs.global_features[13] = static_cast<float>(state.row_score(opponent, gwent::Zone::Ranged));
    obs.global_features[14] = static_cast<float>(actor_player.hand.size());
    obs.global_features[15] = static_cast<float>(opponent_player.hand.size());
    obs.global_features[16] = static_cast<float>(actor_player.cemetery.size());
    obs.global_features[17] = static_cast<float>(opponent_player.cemetery.size());
    obs.global_features[18] = actor_player.passed ? 1.0F : 0.0F;
    obs.global_features[19] = opponent_player.passed ? 1.0F : 0.0F;
    obs.global_features[20] = static_cast<float>(actor_player.round_wins);
    obs.global_features[21] = static_cast<float>(opponent_player.round_wins);
    obs.global_features[22] = actor_player.leader_used ? 1.0F : 0.0F;
    obs.global_features[23] = opponent_player.leader_used ? 1.0F : 0.0F;
    obs.global_features[24] = state.pending_choice.has_value() ? 1.0F : 0.0F;
    obs.global_features[25] = state.pending_choice.has_value() ? static_cast<float>(state.pending_choice->kind) : 0.0F;
    obs.global_features[26] = static_cast<float>(actor_player.mulligans_available);
    obs.global_features[27] = static_cast<float>(opponent_player.mulligans_available);
    obs.global_features[28] = state.current_player_id == actor ? 1.0F : 0.0F;
    obs.global_features[29] = obs.done != 0 ? 1.0F : 0.0F;
    obs.global_features[30] = static_cast<float>(gwent::row_effect_duration(state, actor, gwent::Zone::Melee, "frost"));
    obs.global_features[31] = static_cast<float>(gwent::row_effect_duration(state, actor, gwent::Zone::Ranged, "frost"));
    obs.global_features[32] = static_cast<float>(gwent::row_effect_duration(state, opponent, gwent::Zone::Melee, "frost"));
    obs.global_features[33] = static_cast<float>(gwent::row_effect_duration(state, opponent, gwent::Zone::Ranged, "frost"));
}

void fill_observation_options(
    gwent_rl_observation& obs,
    const gwent::GameState& state,
    const std::vector<gwent::Action>& actions,
    const std::vector<int>& object_indices) {
    const gwent::PlayerId actor = obs.actor_id >= 0 ? static_cast<gwent::PlayerId>(obs.actor_id) : state.current_player_id;
    const std::size_t option_count = actions.size();
    if (option_count > static_cast<std::size_t>(GWENT_RL_MAX_OPTIONS)) {
        throw RlOptionOverflow(option_count);
    }
    obs.option_count = option_count;

    // Keep the compile-time array bound visible in the loop condition.  This
    // both documents the ABI contract and avoids optimizer false positives
    // around the fixed [GWENT_RL_MAX_OPTIONS] C arrays.
    for (std::size_t i = 0; i < static_cast<std::size_t>(GWENT_RL_MAX_OPTIONS); ++i) {
        if (i >= option_count) {
            break;
        }
        const auto& action = actions[i];
        const auto kind = to_rl_option_kind(action.type);
        const int source_object_index = object_index_for_entity(object_indices, action.source_entity_id);
        int target_object_index = -1;
        int target_side_id = -1;
        int target_zone_id = -1;
        int target_row_id = -1;

        obs.option_mask[i] = 1;
        obs.option_kind_ids[i] = static_cast<int>(kind);
        // 对普通动作，card_id 表示动作来源卡牌。
        // 对 CHOOSE_CARD，真正需要区分的是“正在选择哪张候选牌”。
        if (
            action.type == gwent::ActionType::ChooseCardTarget &&
            action.target.kind == gwent::ActionTargetKind::CardDefinition
        ) {
            try {
                obs.option_card_ids[i] = std::stoi(state.card_catalog->at(action.target.definition_id).id);
            } catch (...) {
                obs.option_card_ids[i] = -1;
            }
        } else if (
            action.type == gwent::ActionType::ChooseCardTarget &&
            action.target.kind == gwent::ActionTargetKind::Card
        ) {
            obs.option_card_ids[i] =
                card_id_for_entity(state, action.target.entity_id);
        } else {
            obs.option_card_ids[i] =
                card_id_for_entity(state, action.source_entity_id);
        }

        obs.option_source_object_indices[i] = source_object_index;
        obs.option_target_object_indices[i] = -1;
        obs.option_target_side_ids[i] = -1;
        obs.option_target_zone_ids[i] = -1;
        obs.option_target_row_ids[i] = -1;
        obs.option_insert_positions[i] = action.insert_position;
        obs.option_hand_slot_indices[i] = hand_slot_for_action(state, action);
        obs.option_stable_hashes[i] = stable_action_hash(action);

        const gwent::RuntimeCard* target_card = nullptr;
        if (action.target.kind == gwent::ActionTargetKind::Card) {
            target_object_index = object_index_for_entity(object_indices, action.target.entity_id);
            target_card = state.find_card(action.target.entity_id);
            const auto target_location = state.location_of(action.target.entity_id);
            if (target_location.has_value()) {
                target_side_id = static_cast<int>(target_location->side);
                target_zone_id = zone_id(target_location->zone);
                target_row_id = row_id(target_location->zone);
            }
        } else if (action.target.kind == gwent::ActionTargetKind::Row) {
            target_side_id = static_cast<int>(action.target.side);
            target_zone_id = zone_id(action.target.zone);
            target_row_id = row_id(action.target.zone);
        }
        obs.option_target_object_indices[i] = target_object_index;
        obs.option_target_side_ids[i] = target_side_id;
        obs.option_target_zone_ids[i] = target_zone_id;
        obs.option_target_row_ids[i] = target_row_id;

        set_option_feature(obs, i, 0, 1.0F);
        set_option_feature(obs, i, 1, static_cast<float>(kind));
        set_option_feature(obs, i, 2, action.player_id == actor ? 1.0F : 0.0F);
        set_option_feature(obs, i, 3, source_object_index >= 0 ? 1.0F : 0.0F);
        set_option_feature(obs, i, 4, target_object_index >= 0 ? 1.0F : 0.0F);
        set_option_feature(obs, i, 5, action.target.kind == gwent::ActionTargetKind::Row ? 1.0F : 0.0F);
        set_option_feature(obs, i, 6, static_cast<float>(source_object_index));
        set_option_feature(obs, i, 7, static_cast<float>(target_object_index));
        set_option_feature(obs, i, 8, target_side_id == static_cast<int>(actor) ? 1.0F : 0.0F);
        set_option_feature(obs, i, 9, static_cast<float>(target_row_id));
        if (target_card != nullptr) {
            set_option_feature(obs, i, 10, static_cast<float>(target_card->state.power));
            set_option_feature(obs, i, 11, static_cast<float>(target_card->state.armor));
            set_option_feature(obs, i, 12, static_cast<float>(target_card->state.bleeding));
            set_option_feature(obs, i, 13, static_cast<float>(target_card->state.vitality));
            set_option_feature(obs, i, 14, target_card->state.locked ? 1.0F : 0.0F);
            set_option_feature(obs, i, 15, target_card->is_unit_card() ? 1.0F : 0.0F);
        }
    }
}

void fill_source_and_prefix(gwent_rl_observation& obs, const gwent::GameState& state, const std::vector<int>& object_indices) {
    obs.source_object_index = -1;
    obs.source_entity_id = gwent::kInvalidEntityId;
    obs.source_card_id = -1;
    obs.prefix_count = 0;

    if (!state.pending_choice.has_value()) {
        return;
    }
    const auto& pending = *state.pending_choice;
    obs.source_entity_id = static_cast<int>(pending.source_entity_id);
    obs.source_card_id = card_id_for_entity(state, pending.source_entity_id);
    obs.source_object_index = object_index_for_entity(object_indices, pending.source_entity_id);

    if (pending.resolution_frame) {
        const auto& prefix = pending.resolution_frame->decision_prefix;
        if (prefix.size() > static_cast<std::size_t>(GWENT_RL_MAX_PREFIX)) {
            throw RlPrefixOverflow(prefix.size());
        }
        obs.prefix_count = prefix.size();
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            const auto& token = prefix[i];
            obs.prefix_mask[i] = 1;
            obs.prefix_kind_ids[i] = static_cast<int>(to_rl_option_kind(token.kind));
            obs.prefix_source_object_indices[i] = object_index_for_entity(object_indices, token.source_entity_id);
            obs.prefix_target_object_indices[i] = object_index_for_entity(object_indices, token.target_entity_id);
            obs.prefix_card_ids[i] = -1;
            if (token.target_definition_id != gwent::kInvalidCardDefId) {
                try {
                    obs.prefix_card_ids[i] = std::stoi(state.card_catalog->at(token.target_definition_id).id);
                } catch (...) {
                    obs.prefix_card_ids[i] = -1;
                }
            } else if (token.target_entity_id != gwent::kInvalidEntityId) {
                obs.prefix_card_ids[i] = card_id_for_entity(state, token.target_entity_id);
            }
            obs.prefix_row_ids[i] = token.row.has_value() ? row_id(token.row->zone) : -1;
            obs.prefix_insert_positions[i] = (token.kind == gwent::ActionType::ChooseInsertPosition
                    || token.kind == gwent::ActionType::PlayCard) && token.row.has_value()
                ? static_cast<int>(token.row->index)
                : -1;
        }
        return;
    }

    // Legacy handcrafted PendingChoice fallback. New kernel-generated choices
    // always carry ResolutionFrame::decision_prefix.
    obs.prefix_count = 1;
    obs.prefix_mask[0] = 1;
    if (pending.origin_action_type.has_value()) {
        obs.prefix_kind_ids[0] = static_cast<int>(to_rl_option_kind(*pending.origin_action_type));
    } else {
        obs.prefix_kind_ids[0] = pending.kind == gwent::PendingChoiceKind::RowTarget
            ? static_cast<int>(GWENT_RL_OPTION_CHOOSE_ROW)
            : static_cast<int>(GWENT_RL_OPTION_CHOOSE_CARD);
    }
    obs.prefix_source_object_indices[0] = obs.source_object_index;
    obs.prefix_target_object_indices[0] = -1;
    obs.prefix_row_ids[0] = -1;
    obs.prefix_insert_positions[0] = -1;
}

void rebuild_rl_observation(gwent_rl_env& env) {
    if (env.decision_serial != std::numeric_limits<std::uint64_t>::max()) {
        ++env.decision_serial;
    } else {
        env.decision_serial = 1;
    }
    std::memset(&env.observation, 0, sizeof(env.observation));
    const auto& state = env.game.state();
    const auto decision = env.game.current_decision();
    env.actions = decision.legal_actions;

    env.observation.schema_version = GWENT_RL_SCHEMA_VERSION;
    env.observation.done = env.game.is_finished() ? 1 : 0;
    env.observation.winner_id = -1;
    if (const auto winner = env.game.winner(); winner.has_value()) {
        env.observation.winner_id = static_cast<int>(*winner);
    }
    env.observation.actor_id = env.game.is_finished() ? -1 : static_cast<int>(decision.player_id);
    if (env.observation.actor_id < 0) {
        env.observation.actor_id = static_cast<int>(state.current_player_id);
    }
    env.observation.opponent_id = env.observation.actor_id == 0 ? 1 : 0;
    env.observation.perspective_player_id = env.config.current_player_perspective != 0
        ? env.observation.actor_id
        : static_cast<int>(state.current_player_id);
    env.observation.decision_kind = static_cast<int>(to_rl_decision_kind(decision.type, state));

    auto& object_indices = env.object_index_scratch;
    const std::size_t scratch_size = state.next_entity_id > 0
        ? static_cast<std::size_t>(state.next_entity_id)
        : 0;
    object_indices.assign(scratch_size, -1);
    const gwent::PlayerId actor_id = static_cast<gwent::PlayerId>(env.observation.perspective_player_id);

    // Schema v13 fair-information contract:
    // - public submitted decklists are visible as definition-only knowledge objects;
    // - the actor's own hand is visible;
    // - the opponent's exact hand identities are hidden unless oracle/debug mode is requested;
    // - opponent hand count remains a public global feature.
    append_public_decklists(env.observation, state, actor_id);

    for (gwent::PlayerId side = 0; side < gwent::kPlayerCount; ++side) {
        const auto& player = state.players[static_cast<std::size_t>(side)];
        if (side == actor_id || env.config.include_private_info != 0) {
            append_zone_objects(env.observation, object_indices, state, side, gwent::Zone::Hand, player.hand, actor_id);
        }
        append_zone_objects(env.observation, object_indices, state, side, gwent::Zone::Melee, player.rows[0], actor_id);
        append_zone_objects(env.observation, object_indices, state, side, gwent::Zone::Ranged, player.rows[1], actor_id);
        append_zone_objects(env.observation, object_indices, state, side, gwent::Zone::Stay, player.stay, actor_id);
        if (player.leader.has_value()) {
            append_object(env.observation, object_indices, state, *player.leader, gwent::Location{side, gwent::Zone::Leader, 0}, actor_id);
        }
        append_zone_objects(env.observation, object_indices, state, side, gwent::Zone::Cemetery, player.cemetery, actor_id);
        append_zone_objects(env.observation, object_indices, state, side, gwent::Zone::Banished, player.banished, actor_id);
    }

    fill_source_and_prefix(env.observation, state, object_indices);
    fill_globals(env.observation, state, actor_id);
    fill_observation_options(env.observation, state, env.actions, object_indices);
}

void fill_step_result(
    gwent_rl_env& env,
    gwent_c_result_code result_code,
    gwent_c_action_status status,
    const std::array<float, 2>& rewards,
    gwent_rl_step_result* out) noexcept {
    env.last_result = gwent_rl_step_result{};
    env.last_result.result_code = result_code;
    env.last_result.action_status = status;
    env.last_result.actor_id = env.observation.done ? -1 : env.observation.actor_id;
    env.last_result.option_count = env.observation.option_count;
    env.last_result.done = env.game.is_finished() ? 1 : 0;
    env.last_result.winner_id = -1;
    if (const auto winner = env.game.winner(); winner.has_value()) {
        env.last_result.winner_id = static_cast<int>(*winner);
    }
    env.last_result.reward[0] = rewards[0];
    env.last_result.reward[1] = rewards[1];
    if (out != nullptr) {
        *out = env.last_result;
    }
}

void fill_zero_step_result(gwent_rl_env& env, gwent_c_result_code result_code, gwent_c_action_status status, gwent_rl_step_result* out) noexcept {
    fill_step_result(env, result_code, status, std::array<float, 2>{0.0F, 0.0F}, out);
}

void fill_batch_step_result(
    gwent_rl_batch_step_result& out,
    std::size_t env_index,
    std::uint64_t decision_serial,
    const gwent_rl_step_result& step) noexcept {
    out = gwent_rl_batch_step_result{};
    out.env_index = env_index;
    out.decision_serial = decision_serial;
    out.result_code = step.result_code;
    out.action_status = step.action_status;
    out.done = step.done;
    out.actor_id = step.actor_id;
    out.winner_id = step.winner_id;
    out.reward[0] = step.reward[0];
    out.reward[1] = step.reward[1];
    out.option_count = step.option_count;
}

void resize_collector_buffers(gwent_rl_collector& collector, std::size_t max_batch_size) {
    collector.items.resize(max_batch_size);
    collector.apply_results.resize(max_batch_size);
    collector.stepped_flags.resize(max_batch_size);

    collector.global_features.resize(max_batch_size * GWENT_RL_GLOBAL_FEATURE_COUNT);
    collector.object_entity_ids.resize(max_batch_size * GWENT_RL_MAX_OBJECTS);
    collector.object_card_ids.resize(max_batch_size * GWENT_RL_MAX_OBJECTS);
    collector.object_owner_ids.resize(max_batch_size * GWENT_RL_MAX_OBJECTS);
    collector.object_controller_ids.resize(max_batch_size * GWENT_RL_MAX_OBJECTS);
    collector.object_zone_ids.resize(max_batch_size * GWENT_RL_MAX_OBJECTS);
    collector.object_row_ids.resize(max_batch_size * GWENT_RL_MAX_OBJECTS);
    collector.object_slot_indices.resize(max_batch_size * GWENT_RL_MAX_OBJECTS);
    collector.object_mask.resize(max_batch_size * GWENT_RL_MAX_OBJECTS);
    collector.object_features.resize(max_batch_size * GWENT_RL_MAX_OBJECTS * GWENT_RL_OBJECT_FEATURE_COUNT);

    collector.option_kind_ids.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_card_ids.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_source_object_indices.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_target_object_indices.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_target_side_ids.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_target_zone_ids.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_target_row_ids.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_insert_positions.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_hand_slot_indices.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_stable_hashes.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_mask.resize(max_batch_size * GWENT_RL_MAX_OPTIONS);
    collector.option_features.resize(max_batch_size * GWENT_RL_MAX_OPTIONS * GWENT_RL_OPTION_FEATURE_COUNT);

    collector.prefix_kind_ids.resize(max_batch_size * GWENT_RL_MAX_PREFIX);
    collector.prefix_card_ids.resize(max_batch_size * GWENT_RL_MAX_PREFIX);
    collector.prefix_source_object_indices.resize(max_batch_size * GWENT_RL_MAX_PREFIX);
    collector.prefix_target_object_indices.resize(max_batch_size * GWENT_RL_MAX_PREFIX);
    collector.prefix_row_ids.resize(max_batch_size * GWENT_RL_MAX_PREFIX);
    collector.prefix_insert_positions.resize(max_batch_size * GWENT_RL_MAX_PREFIX);
    collector.prefix_mask.resize(max_batch_size * GWENT_RL_MAX_PREFIX);
}

void clear_collector_row(gwent_rl_collector& collector, std::size_t row) noexcept {
    const std::size_t object_base = row * GWENT_RL_MAX_OBJECTS;
    const std::size_t object_feature_base = object_base * GWENT_RL_OBJECT_FEATURE_COUNT;
    const std::size_t option_base = row * GWENT_RL_MAX_OPTIONS;
    const std::size_t option_feature_base = option_base * GWENT_RL_OPTION_FEATURE_COUNT;
    const std::size_t global_base = row * GWENT_RL_GLOBAL_FEATURE_COUNT;
    const std::size_t prefix_base = row * GWENT_RL_MAX_PREFIX;

    std::fill_n(collector.global_features.data() + global_base, GWENT_RL_GLOBAL_FEATURE_COUNT, 0.0F);
    std::fill_n(collector.object_entity_ids.data() + object_base, GWENT_RL_MAX_OBJECTS, 0);
    std::fill_n(collector.object_card_ids.data() + object_base, GWENT_RL_MAX_OBJECTS, 0);
    std::fill_n(collector.object_owner_ids.data() + object_base, GWENT_RL_MAX_OBJECTS, 0);
    std::fill_n(collector.object_controller_ids.data() + object_base, GWENT_RL_MAX_OBJECTS, 0);
    std::fill_n(collector.object_zone_ids.data() + object_base, GWENT_RL_MAX_OBJECTS, 0);
    std::fill_n(collector.object_row_ids.data() + object_base, GWENT_RL_MAX_OBJECTS, 0);
    std::fill_n(collector.object_slot_indices.data() + object_base, GWENT_RL_MAX_OBJECTS, 0);
    std::fill_n(collector.object_mask.data() + object_base, GWENT_RL_MAX_OBJECTS, static_cast<unsigned char>(0));
    std::fill_n(collector.object_features.data() + object_feature_base, GWENT_RL_MAX_OBJECTS * GWENT_RL_OBJECT_FEATURE_COUNT, 0.0F);

    std::fill_n(collector.option_kind_ids.data() + option_base, GWENT_RL_MAX_OPTIONS, 0);
    std::fill_n(collector.option_card_ids.data() + option_base, GWENT_RL_MAX_OPTIONS, 0);
    std::fill_n(collector.option_source_object_indices.data() + option_base, GWENT_RL_MAX_OPTIONS, 0);
    std::fill_n(collector.option_target_object_indices.data() + option_base, GWENT_RL_MAX_OPTIONS, 0);
    std::fill_n(collector.option_target_side_ids.data() + option_base, GWENT_RL_MAX_OPTIONS, 0);
    std::fill_n(collector.option_target_zone_ids.data() + option_base, GWENT_RL_MAX_OPTIONS, 0);
    std::fill_n(collector.option_target_row_ids.data() + option_base, GWENT_RL_MAX_OPTIONS, 0);
    std::fill_n(collector.option_insert_positions.data() + option_base, GWENT_RL_MAX_OPTIONS, -1);
    std::fill_n(collector.option_hand_slot_indices.data() + option_base, GWENT_RL_MAX_OPTIONS, 0);
    std::fill_n(collector.option_stable_hashes.data() + option_base, GWENT_RL_MAX_OPTIONS, static_cast<std::uint64_t>(0));
    std::fill_n(collector.option_mask.data() + option_base, GWENT_RL_MAX_OPTIONS, static_cast<unsigned char>(0));
    std::fill_n(collector.option_features.data() + option_feature_base, GWENT_RL_MAX_OPTIONS * GWENT_RL_OPTION_FEATURE_COUNT, 0.0F);

    std::fill_n(collector.prefix_kind_ids.data() + prefix_base, GWENT_RL_MAX_PREFIX, 0);
    std::fill_n(collector.prefix_card_ids.data() + prefix_base, GWENT_RL_MAX_PREFIX, -1);
    std::fill_n(collector.prefix_source_object_indices.data() + prefix_base, GWENT_RL_MAX_PREFIX, 0);
    std::fill_n(collector.prefix_target_object_indices.data() + prefix_base, GWENT_RL_MAX_PREFIX, 0);
    std::fill_n(collector.prefix_row_ids.data() + prefix_base, GWENT_RL_MAX_PREFIX, 0);
    std::fill_n(collector.prefix_insert_positions.data() + prefix_base, GWENT_RL_MAX_PREFIX, -1);
    std::fill_n(collector.prefix_mask.data() + prefix_base, GWENT_RL_MAX_PREFIX, static_cast<unsigned char>(0));
}

void copy_observation_to_collector_row(
    gwent_rl_collector& collector,
    std::size_t row,
    std::size_t env_index,
    const gwent_rl_env& env) noexcept {
    clear_collector_row(collector, row);
    const auto& obs = env.observation;

    collector.items[row] = gwent_rl_batch_item{};
    collector.items[row].env_index = env_index;
    collector.items[row].decision_serial = env.decision_serial;
    collector.items[row].actor_id = obs.actor_id;
    collector.items[row].decision_kind = obs.decision_kind;
    collector.items[row].done = obs.done;
    collector.items[row].winner_id = obs.winner_id;
    collector.items[row].source_object_index = obs.source_object_index;
    collector.items[row].source_entity_id = obs.source_entity_id;
    collector.items[row].source_card_id = obs.source_card_id;
    collector.items[row].object_count = obs.object_count;
    collector.items[row].option_count = obs.option_count;
    collector.items[row].prefix_count = obs.prefix_count;

    const std::size_t global_base = row * GWENT_RL_GLOBAL_FEATURE_COUNT;
    const std::size_t prefix_base = row * GWENT_RL_MAX_PREFIX;
    std::memcpy(collector.global_features.data() + global_base,
                obs.global_features,
                sizeof(float) * GWENT_RL_GLOBAL_FEATURE_COUNT);

    const std::size_t object_base = row * GWENT_RL_MAX_OBJECTS;
    const std::size_t object_feature_base = object_base * GWENT_RL_OBJECT_FEATURE_COUNT;
    std::memcpy(collector.object_entity_ids.data() + object_base,
                obs.object_entity_ids,
                sizeof(int) * GWENT_RL_MAX_OBJECTS);
    std::memcpy(collector.object_card_ids.data() + object_base,
                obs.object_card_ids,
                sizeof(int) * GWENT_RL_MAX_OBJECTS);
    std::memcpy(collector.object_owner_ids.data() + object_base,
                obs.object_owner_ids,
                sizeof(int) * GWENT_RL_MAX_OBJECTS);
    std::memcpy(collector.object_controller_ids.data() + object_base,
                obs.object_controller_ids,
                sizeof(int) * GWENT_RL_MAX_OBJECTS);
    std::memcpy(collector.object_zone_ids.data() + object_base,
                obs.object_zone_ids,
                sizeof(int) * GWENT_RL_MAX_OBJECTS);
    std::memcpy(collector.object_row_ids.data() + object_base,
                obs.object_row_ids,
                sizeof(int) * GWENT_RL_MAX_OBJECTS);
    std::memcpy(collector.object_slot_indices.data() + object_base,
                obs.object_slot_indices,
                sizeof(int) * GWENT_RL_MAX_OBJECTS);
    std::memcpy(collector.object_mask.data() + object_base,
                obs.object_mask,
                sizeof(unsigned char) * GWENT_RL_MAX_OBJECTS);
    std::memcpy(collector.object_features.data() + object_feature_base,
                obs.object_features,
                sizeof(float) * GWENT_RL_MAX_OBJECTS * GWENT_RL_OBJECT_FEATURE_COUNT);

    const std::size_t option_base = row * GWENT_RL_MAX_OPTIONS;
    const std::size_t option_feature_base = option_base * GWENT_RL_OPTION_FEATURE_COUNT;
    std::memcpy(collector.option_kind_ids.data() + option_base,
                obs.option_kind_ids,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_card_ids.data() + option_base,
                obs.option_card_ids,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_source_object_indices.data() + option_base,
                obs.option_source_object_indices,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_target_object_indices.data() + option_base,
                obs.option_target_object_indices,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_target_side_ids.data() + option_base,
                obs.option_target_side_ids,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_target_zone_ids.data() + option_base,
                obs.option_target_zone_ids,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_target_row_ids.data() + option_base,
                obs.option_target_row_ids,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_insert_positions.data() + option_base,
                obs.option_insert_positions,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_hand_slot_indices.data() + option_base,
                obs.option_hand_slot_indices,
                sizeof(int) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_stable_hashes.data() + option_base,
                obs.option_stable_hashes,
                sizeof(std::uint64_t) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_mask.data() + option_base,
                obs.option_mask,
                sizeof(unsigned char) * GWENT_RL_MAX_OPTIONS);
    std::memcpy(collector.option_features.data() + option_feature_base,
                obs.option_features,
                sizeof(float) * GWENT_RL_MAX_OPTIONS * GWENT_RL_OPTION_FEATURE_COUNT);

    std::memcpy(collector.prefix_kind_ids.data() + prefix_base,
                obs.prefix_kind_ids,
                sizeof(int) * GWENT_RL_MAX_PREFIX);
    std::memcpy(collector.prefix_card_ids.data() + prefix_base,
                obs.prefix_card_ids,
                sizeof(int) * GWENT_RL_MAX_PREFIX);
    std::memcpy(collector.prefix_source_object_indices.data() + prefix_base,
                obs.prefix_source_object_indices,
                sizeof(int) * GWENT_RL_MAX_PREFIX);
    std::memcpy(collector.prefix_target_object_indices.data() + prefix_base,
                obs.prefix_target_object_indices,
                sizeof(int) * GWENT_RL_MAX_PREFIX);
    std::memcpy(collector.prefix_row_ids.data() + prefix_base,
                obs.prefix_row_ids,
                sizeof(int) * GWENT_RL_MAX_PREFIX);
    std::memcpy(collector.prefix_insert_positions.data() + prefix_base,
                obs.prefix_insert_positions,
                sizeof(int) * GWENT_RL_MAX_PREFIX);
    std::memcpy(collector.prefix_mask.data() + prefix_base,
                obs.prefix_mask,
                sizeof(unsigned char) * GWENT_RL_MAX_PREFIX);
}

void fill_inference_batch_view(gwent_rl_collector& collector, std::size_t count, gwent_rl_inference_batch* out_batch) noexcept {
    if (out_batch == nullptr) {
        return;
    }
    *out_batch = gwent_rl_inference_batch{};
    out_batch->count = count;
    out_batch->max_count = collector.config.max_batch_size;
    out_batch->items = collector.items.data();
    out_batch->global_features = collector.global_features.data();
    out_batch->object_entity_ids = collector.object_entity_ids.data();
    out_batch->object_card_ids = collector.object_card_ids.data();
    out_batch->object_owner_ids = collector.object_owner_ids.data();
    out_batch->object_controller_ids = collector.object_controller_ids.data();
    out_batch->object_zone_ids = collector.object_zone_ids.data();
    out_batch->object_row_ids = collector.object_row_ids.data();
    out_batch->object_slot_indices = collector.object_slot_indices.data();
    out_batch->object_mask = collector.object_mask.data();
    out_batch->object_features = collector.object_features.data();
    out_batch->option_kind_ids = collector.option_kind_ids.data();
    out_batch->option_card_ids = collector.option_card_ids.data();
    out_batch->option_source_object_indices = collector.option_source_object_indices.data();
    out_batch->option_target_object_indices = collector.option_target_object_indices.data();
    out_batch->option_target_side_ids = collector.option_target_side_ids.data();
    out_batch->option_target_zone_ids = collector.option_target_zone_ids.data();
    out_batch->option_target_row_ids = collector.option_target_row_ids.data();
    out_batch->option_insert_positions = collector.option_insert_positions.data();
    out_batch->option_hand_slot_indices = collector.option_hand_slot_indices.data();
    out_batch->option_stable_hashes = collector.option_stable_hashes.data();
    out_batch->option_mask = collector.option_mask.data();
    out_batch->option_features = collector.option_features.data();
    out_batch->prefix_kind_ids = collector.prefix_kind_ids.data();
    out_batch->prefix_card_ids = collector.prefix_card_ids.data();
    out_batch->prefix_source_object_indices = collector.prefix_source_object_indices.data();
    out_batch->prefix_target_object_indices = collector.prefix_target_object_indices.data();
    out_batch->prefix_row_ids = collector.prefix_row_ids.data();
    out_batch->prefix_insert_positions = collector.prefix_insert_positions.data();
    out_batch->prefix_mask = collector.prefix_mask.data();
}

void fill_apply_result_batch_view(gwent_rl_collector& collector, std::size_t count, gwent_rl_apply_result_batch* out_results) noexcept {
    if (out_results == nullptr) {
        return;
    }
    *out_results = gwent_rl_apply_result_batch{};
    out_results->count = count;
    out_results->results = collector.apply_results.data();
}

}  // namespace

extern "C" {

gwent_deck_a_match_config gwent_deck_a_default_config(void) {
    gwent_deck_a_match_config config{};
    config.seed = 0;
    config.starting_player_id = -1;
    config.shuffle_decks = 1;
    config.invariant_policy = GWENT_C_INVARIANT_DISABLED;
    return config;
}

gwent_deck_a_game* gwent_deck_a_game_create(const gwent_deck_a_match_config* config) {
    try {
        gwent::api::DeckAMatchConfig cpp_config;
        if (config != nullptr) {
            cpp_config.seed = config->seed;
            if (config->starting_player_id >= 0) {
                cpp_config.starting_player_id = static_cast<gwent::PlayerId>(config->starting_player_id);
            }
            cpp_config.shuffle_decks = config->shuffle_decks != 0;
            cpp_config.kernel_config.invariant_policy = to_cpp_policy(config->invariant_policy);
        }

        auto* game = new gwent_deck_a_game{};
        game->impl = gwent::api::DeckAGame::create(cpp_config);
        return game;
    } catch (...) {
        return nullptr;
    }
}

void gwent_deck_a_game_destroy(gwent_deck_a_game* game) {
    delete game;
}

int gwent_deck_a_game_is_finished(const gwent_deck_a_game* game) {
    if (game == nullptr) {
        return 0;
    }
    return game->impl.is_finished() ? 1 : 0;
}

int gwent_deck_a_game_winner(const gwent_deck_a_game* game) {
    if (game == nullptr) {
        return -1;
    }
    const auto winner = game->impl.winner();
    if (!winner.has_value()) {
        return -1;
    }
    return static_cast<int>(*winner);
}

size_t gwent_deck_a_game_legal_action_count(const gwent_deck_a_game* game) {
    if (game == nullptr) {
        return 0;
    }
    try {
        return game->impl.legal_actions().size();
    } catch (...) {
        return 0;
    }
}

gwent_c_result_code gwent_deck_a_game_legal_actions_json(const gwent_deck_a_game* game, gwent_c_string* out_json) {
    if (game == nullptr || out_json == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        return set_string(legal_actions_to_json(game->impl), out_json);
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}

gwent_c_result_code gwent_deck_a_game_snapshot_json(const gwent_deck_a_game* game, gwent_c_string* out_json) {
    if (game == nullptr || out_json == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        return set_string(game->impl.snapshot_json(), out_json);
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}

gwent_c_result_code gwent_deck_a_game_checksum(const gwent_deck_a_game* game, gwent_c_string* out_checksum) {
    if (game == nullptr || out_checksum == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        return set_string(game->impl.checksum(), out_checksum);
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}

gwent_c_result_code gwent_deck_a_game_apply_legal_action_at(
    gwent_deck_a_game* game,
    size_t legal_action_index,
    gwent_c_action_status* out_status) {
    if (game == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        const auto actions = game->impl.legal_actions();
        if (legal_action_index >= actions.size()) {
            return GWENT_C_OUT_OF_RANGE;
        }
        const auto result = game->impl.apply(actions[legal_action_index]);
        if (out_status != nullptr) {
            *out_status = to_c_status(result.status);
        }
        return GWENT_C_OK;
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}



gwent_rl_reward_config gwent_rl_default_reward_config(void) {
    return default_reward_config();
}

gwent_rl_config gwent_rl_default_config(void) {
    return default_rl_config();
}

gwent_rl_collector_config gwent_rl_default_collector_config(void) {
    return default_collector_config();
}

gwent_rl_env* gwent_rl_env_create(const gwent_rl_config* config) {
    try {
        auto* env = new gwent_rl_env{};
        env->config = config != nullptr ? *config : default_rl_config();
        env->game = make_rl_game(env->config);
        reset_round_reward_tracker(*env);
        rebuild_rl_observation(*env);
        fill_zero_step_result(*env, GWENT_C_OK, GWENT_C_ACTION_APPLIED, nullptr);
        return env;
    } catch (...) {
        return nullptr;
    }
}

gwent_rl_env* gwent_rl_env_clone(const gwent_rl_env* source) {
    if (source == nullptr) {
        return nullptr;
    }
    try {
        auto* clone = new gwent_rl_env(*source);
        // Most environment fields are value-owned. PendingChoice is the one
        // exception: its ResolutionFrame deliberately uses shared ownership
        // inside a live staged resolution, so detach it before a preview can
        // step the cloned game.
        detach_pending_resolution_frame(clone->game.mutable_state());
        return clone;
    } catch (...) {
        return nullptr;
    }
}

void gwent_rl_env_destroy(gwent_rl_env* env) {
    delete env;
}

gwent_c_result_code gwent_rl_env_reset(
    gwent_rl_env* env,
    uint64_t seed,
    int starting_player_id,
    gwent_rl_step_result* out_result) {
    if (env == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        env->config.seed = seed;
        env->config.starting_player_id = starting_player_id;
        env->game = make_rl_game(env->config);
        reset_round_reward_tracker(*env);
        rebuild_rl_observation(*env);
        fill_zero_step_result(*env, GWENT_C_OK, GWENT_C_ACTION_APPLIED, out_result);
        return GWENT_C_OK;
    } catch (const RlSurfaceOverflow& ex) {
        return ex.code();
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}

gwent_c_result_code gwent_rl_env_step_option(
    gwent_rl_env* env,
    size_t option_index,
    gwent_rl_step_result* out_result) {
    if (env == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        if (env->game.is_finished()) {
            rebuild_rl_observation(*env);
            fill_zero_step_result(*env, GWENT_C_OUT_OF_RANGE, GWENT_C_ACTION_UNKNOWN, out_result);
            return GWENT_C_OUT_OF_RANGE;
        }
        if (option_index >= env->observation.option_count) {
            rebuild_rl_observation(*env);
            fill_zero_step_result(*env, GWENT_C_OUT_OF_RANGE, GWENT_C_ACTION_UNKNOWN, out_result);
            return GWENT_C_OUT_OF_RANGE;
        }
        const auto action = env->actions[option_index];
        const auto game_before = env->game;
        const auto actions_before = env->actions;
        const auto observation_before = env->observation;
        const auto decision_serial_before = env->decision_serial;
        const auto round_reward_tracker_before = env->round_reward_tracker;
        const std::array<int, 2> pre_scores{
            env->game.state().board_score(gwent::kPlayerZero),
            env->game.state().board_score(gwent::kPlayerOne),
        };
        const auto result = env->game.apply(action);
        const auto rewards = compute_rl_rewards(env->config.reward_config, *env, game_before, env->game, action, &result, pre_scores);
        try {
            rebuild_rl_observation(*env);
        } catch (const RlSurfaceOverflow& ex) {
            // RL surface overflow is an adapter failure, not a legal game
            // transition. Restore the exact pre-step environment so callers
            // can fail fast without silently truncating objects/options/prefix
            // or advancing the episode behind their back.
            env->game = game_before;
            env->actions = actions_before;
            env->observation = observation_before;
            env->decision_serial = decision_serial_before;
            env->round_reward_tracker = round_reward_tracker_before;
            fill_zero_step_result(*env, ex.code(), GWENT_C_ACTION_UNKNOWN, out_result);
            return ex.code();
        }
        fill_step_result(*env, GWENT_C_OK, to_c_status(result.status), rewards, out_result);
        return GWENT_C_OK;
    } catch (const RlSurfaceOverflow& ex) {
        fill_zero_step_result(*env, ex.code(), GWENT_C_ACTION_UNKNOWN, out_result);
        return ex.code();
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}

const gwent_rl_observation* gwent_rl_env_observation(const gwent_rl_env* env) {
    if (env == nullptr) {
        return nullptr;
    }
    return &env->observation;
}

size_t gwent_rl_env_option_count(const gwent_rl_env* env) {
    if (env == nullptr) {
        return 0;
    }
    return env->observation.option_count;
}

int gwent_rl_env_actor_id(const gwent_rl_env* env) {
    if (env == nullptr) {
        return -1;
    }
    return env->observation.actor_id;
}

int gwent_rl_env_done(const gwent_rl_env* env) {
    if (env == nullptr) {
        return 0;
    }
    return env->observation.done;
}

int gwent_rl_env_winner_id(const gwent_rl_env* env) {
    if (env == nullptr) {
        return -1;
    }
    return env->observation.winner_id;
}

int gwent_rl_env_row_effect_duration(
    const gwent_rl_env* env,
    int player_id,
    int row_id,
    const char* effect_id) {
    if (env == nullptr || effect_id == nullptr || effect_id[0] == '\0') {
        return -1;
    }
    if (player_id < 0 || player_id > 1 || row_id < 0 || row_id > 1) {
        return -1;
    }

    const gwent::Zone zone = row_id == 0 ? gwent::Zone::Melee : gwent::Zone::Ranged;
    try {
        return gwent::row_effect_duration(
            env->game.state(),
            static_cast<gwent::PlayerId>(player_id),
            zone,
            effect_id);
    } catch (...) {
        return -1;
    }
}

gwent_c_result_code gwent_rl_env_checksum(const gwent_rl_env* env, gwent_c_string* out_checksum) {
    if (env == nullptr || out_checksum == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        return set_string(env->game.checksum(), out_checksum);
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}


gwent_rl_collector* gwent_rl_collector_create(const gwent_rl_collector_config* config) {
    return gwent_rl_collector_create_with_matchups(config, nullptr, 0);
}

gwent_rl_collector* gwent_rl_collector_create_with_matchups(
    const gwent_rl_collector_config* config,
    const gwent_rl_matchup_config* matchups,
    size_t matchup_count) {
    try {
        if (matchup_count > 0 && matchups == nullptr) {
            return nullptr;
        }
        auto* collector = new gwent_rl_collector{};
        collector->config = config != nullptr ? *config : default_collector_config();
        const auto deck_count = gwent::generated::supported_deck_ids().size();
        for (std::size_t i = 0; i < matchup_count; ++i) {
            const auto& matchup = matchups[i];
            if (matchup.weight == 0
                || matchup.player0_deck_id < 0 || static_cast<std::size_t>(matchup.player0_deck_id) >= deck_count
                || matchup.player1_deck_id < 0 || static_cast<std::size_t>(matchup.player1_deck_id) >= deck_count
                || collector->matchup_weight_total > std::numeric_limits<std::uint64_t>::max() - matchup.weight) {
                delete collector;
                return nullptr;
            }
            collector->matchups.push_back(matchup);
            collector->matchup_weight_total += matchup.weight;
        }
        collector->collector_threads = read_collector_threads();
        if (collector->config.num_envs == 0) {
            collector->config.num_envs = 1;
        }
        if (collector->config.max_batch_size == 0) {
            collector->config.max_batch_size = collector->config.num_envs;
        }
        if (collector->config.max_batch_size > collector->config.num_envs) {
            collector->config.max_batch_size = collector->config.num_envs;
        }

        resize_collector_buffers(*collector, collector->config.max_batch_size);
        collector->envs.reserve(collector->config.num_envs);
        collector->next_seeds.resize(collector->config.num_envs);

        for (std::size_t i = 0; i < collector->config.num_envs; ++i) {
            const std::uint64_t seed = collector->config.base_seed + static_cast<std::uint64_t>(i);
            collector->next_seeds[i] = collector->config.base_seed + collector->config.num_envs + static_cast<std::uint64_t>(i);
            gwent_rl_config env_config = env_config_from_collector(collector->config, seed);
            apply_matchup(env_config, select_matchup(*collector, i));
            auto* env = new gwent_rl_env{};
            env->config = env_config;
            env->game = make_rl_game(env->config);
            reset_round_reward_tracker(*env);
            rebuild_rl_observation(*env);
            fill_zero_step_result(*env, GWENT_C_OK, GWENT_C_ACTION_APPLIED, nullptr);
            collector->envs.push_back(env);
            ++collector->total_resets;
        }
        return collector;
    } catch (...) {
        return nullptr;
    }
}

void gwent_rl_collector_destroy(gwent_rl_collector* collector) {
    if (collector == nullptr) {
        return;
    }
    for (auto* env : collector->envs) {
        delete env;
    }
    delete collector;
}

gwent_c_result_code gwent_rl_collector_set_game_limit(
    gwent_rl_collector* collector,
    size_t max_games) {
    if (collector == nullptr || max_games == 0) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    // total_resets includes initial games plus all later resets.
    if (max_games < collector->total_resets) {
        return GWENT_C_OUT_OF_RANGE;
    }
    collector->max_total_resets = max_games;
    return GWENT_C_OK;
}

gwent_c_result_code gwent_rl_collector_collect(
    gwent_rl_collector* collector,
    gwent_rl_inference_batch* out_batch) {
    if (collector == nullptr || out_batch == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        std::vector<std::size_t> selected_envs;
        selected_envs.reserve(collector->config.max_batch_size);

        for (std::size_t env_index = 0;
             env_index < collector->envs.size() && selected_envs.size() < collector->config.max_batch_size;
             ++env_index) {
            auto* env = collector->envs[env_index];
            if (env == nullptr) {
                continue;
            }
            if (env->observation.done != 0) {
                if (collector->config.auto_reset_done_envs == 0
                    || collector->total_resets >= collector->max_total_resets) {
                    continue;
                }
                gwent_rl_step_result ignored{};
                const std::uint64_t seed = collector->next_seeds[env_index];
                collector->next_seeds[env_index] += collector->config.num_envs;
                apply_matchup(env->config, select_matchup(*collector, collector->total_resets));
                const auto reset_code = gwent_rl_env_reset(env, seed, collector->config.starting_player_id, &ignored);
                if (reset_code != GWENT_C_OK) {
                    return reset_code;
                }
                ++collector->total_resets;
            }
            if (env->observation.option_count == 0 || env->observation.done != 0) {
                continue;
            }
            selected_envs.push_back(env_index);
        }

        const std::size_t count = selected_envs.size();
#ifdef GWENT_HAS_OPENMP
#pragma omp parallel for schedule(static) num_threads(collector->collector_threads) if(collector->collector_threads > 1 && count > 1)
#endif
        for (std::int64_t row = 0; row < static_cast<std::int64_t>(count); ++row) {
            const auto env_index = selected_envs[static_cast<std::size_t>(row)];
            auto* env = collector->envs[env_index];
            if (env != nullptr) {
                copy_observation_to_collector_row(*collector, static_cast<std::size_t>(row), env_index, *env);
            }
        }

        fill_inference_batch_view(*collector, count, out_batch);
        return GWENT_C_OK;
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}

gwent_c_result_code gwent_rl_collector_apply_actions(
    gwent_rl_collector* collector,
    const gwent_rl_batch_action* actions,
    size_t action_count,
    gwent_rl_apply_result_batch* out_results) {
    if (collector == nullptr || (action_count > 0 && actions == nullptr)) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    try {
        const std::size_t result_count = action_count < collector->apply_results.size() ? action_count : collector->apply_results.size();
        std::fill_n(collector->stepped_flags.data(), result_count, static_cast<unsigned char>(0));

        // The fast path assumes each row targets a different environment. This is true
        // for batches produced by gwent_rl_collector_collect(). If a caller supplies
        // duplicate env indices manually, fall back to serial execution to preserve
        // the old ordered semantics and avoid racing on the same env.
        bool unique_env_targets = true;
        std::vector<unsigned char> seen_envs(collector->envs.size(), static_cast<unsigned char>(0));
        for (std::size_t i = 0; i < result_count; ++i) {
            const auto env_index = actions[i].env_index;
            if (env_index >= collector->envs.size()) {
                continue;
            }
            if (seen_envs[env_index] != 0) {
                unique_env_targets = false;
                break;
            }
            seen_envs[env_index] = static_cast<unsigned char>(1);
        }

#ifdef GWENT_HAS_OPENMP
#pragma omp parallel for schedule(static) num_threads(collector->collector_threads) if(collector->collector_threads > 1 && unique_env_targets && result_count > 1)
#endif
        for (std::int64_t signed_i = 0; signed_i < static_cast<std::int64_t>(result_count); ++signed_i) {
            const std::size_t i = static_cast<std::size_t>(signed_i);
            auto& out = collector->apply_results[i];
            out = gwent_rl_batch_step_result{};
            out.env_index = actions[i].env_index;
            out.decision_serial = actions[i].decision_serial;
            out.result_code = GWENT_C_OK;
            out.action_status = GWENT_C_ACTION_UNKNOWN;
            out.actor_id = -1;
            out.winner_id = -1;

            try {
                if (actions[i].env_index >= collector->envs.size() || collector->envs[actions[i].env_index] == nullptr) {
                    out.result_code = GWENT_C_OUT_OF_RANGE;
                    continue;
                }
                auto* env = collector->envs[actions[i].env_index];
                if (actions[i].decision_serial != env->decision_serial) {
                    out.result_code = GWENT_C_STALE_DECISION;
                    continue;
                }
                if (actions[i].option_index >= env->observation.option_count) {
                    out.result_code = GWENT_C_OUT_OF_RANGE;
                    continue;
                }

                gwent_rl_step_result step{};
                const auto code = gwent_rl_env_step_option(env, actions[i].option_index, &step);
                fill_batch_step_result(out, actions[i].env_index, actions[i].decision_serial, step);
                out.result_code = code == GWENT_C_OK ? step.result_code : code;
                collector->stepped_flags[i] = static_cast<unsigned char>(1);
            } catch (const std::bad_alloc&) {
                out.result_code = GWENT_C_ALLOCATION_FAILURE;
            } catch (...) {
                out.result_code = GWENT_C_EXCEPTION;
            }
        }

        gwent_c_result_code aggregate = GWENT_C_OK;
        std::size_t completed_delta = 0;
        std::size_t stepped_delta = 0;
        for (std::size_t i = 0; i < result_count; ++i) {
            const auto& out = collector->apply_results[i];
            if (out.result_code != GWENT_C_OK && aggregate == GWENT_C_OK) {
                aggregate = out.result_code;
            }
            if (collector->stepped_flags[i] != 0) {
                ++stepped_delta;
                if (out.done != 0) {
                    ++completed_delta;
                }
            }
        }
        collector->completed_episodes += completed_delta;
        collector->total_steps += stepped_delta;

        fill_apply_result_batch_view(*collector, result_count, out_results);
        if (action_count > collector->apply_results.size() && aggregate == GWENT_C_OK) {
            return GWENT_C_OUT_OF_RANGE;
        }
        return aggregate;
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}

gwent_c_result_code gwent_rl_collector_probe_pass(
    const gwent_rl_collector* collector,
    size_t env_index,
    gwent_rl_pass_probe_result* out_probe) {
    if (collector == nullptr || out_probe == nullptr) {
        return GWENT_C_INVALID_ARGUMENT;
    }
    *out_probe = gwent_rl_pass_probe_result{};
    if (env_index >= collector->envs.size()) {
        return GWENT_C_OUT_OF_RANGE;
    }
    const auto* env = collector->envs[env_index];
    if (env == nullptr || env->observation.done != 0) {
        return GWENT_C_OK;
    }

    const int actor = env->observation.actor_id;
    if (actor < 0 || actor > 1) {
        return GWENT_C_OK;
    }
    const int opponent = 1 - actor;
    const auto& state = env->game.state();
    if (!state.player(static_cast<gwent::PlayerId>(opponent)).passed) {
        return GWENT_C_OK;
    }

    const gwent::Action* pass_action = nullptr;
    for (const auto& action : env->actions) {
        if (action.type == gwent::ActionType::Pass && static_cast<int>(action.player_id) == actor) {
            pass_action = &action;
            break;
        }
    }
    if (pass_action == nullptr) {
        return GWENT_C_OK;
    }

    out_probe->applicable = 1;
    out_probe->actor_id = actor;
    out_probe->actor_hand_count = static_cast<int>(
        state.player(static_cast<gwent::PlayerId>(actor)).hand.size());
    out_probe->opponent_hand_count = static_cast<int>(
        state.player(static_cast<gwent::PlayerId>(opponent)).hand.size());
    out_probe->hand_diff_actor_minus_opponent =
        out_probe->actor_hand_count - out_probe->opponent_hand_count;

    try {
        auto simulated = env->game;
        const auto result = simulated.apply(*pass_action);
        if (!result.applied) {
            return GWENT_C_EXCEPTION;
        }
        int round_winner = -2;
        for (const auto& event : result.events) {
            const int parsed = round_winner_from_event(event);
            if (parsed != -2) {
                round_winner = parsed;
                break;
            }
        }
        if (round_winner == -2) {
            return GWENT_C_EXCEPTION;
        }
        if (round_winner == -1) {
            out_probe->outcome = GWENT_RL_PASS_DRAW;
        } else if (round_winner == actor) {
            out_probe->outcome = GWENT_RL_PASS_WIN;
        } else {
            out_probe->outcome = GWENT_RL_PASS_LOSS;
        }
        return GWENT_C_OK;
    } catch (const std::bad_alloc&) {
        return GWENT_C_ALLOCATION_FAILURE;
    } catch (...) {
        return GWENT_C_EXCEPTION;
    }
}

size_t gwent_rl_collector_env_count(const gwent_rl_collector* collector) {
    return collector == nullptr ? 0U : collector->envs.size();
}

size_t gwent_rl_collector_completed_episodes(const gwent_rl_collector* collector) {
    return collector == nullptr ? 0U : collector->completed_episodes;
}

size_t gwent_rl_collector_total_steps(const gwent_rl_collector* collector) {
    return collector == nullptr ? 0U : collector->total_steps;
}

size_t gwent_rl_collector_total_resets(const gwent_rl_collector* collector) {
    return collector == nullptr ? 0U : collector->total_resets;
}

const char* gwent_rl_decision_kind_name(gwent_rl_decision_kind kind) {
    switch (kind) {
        case GWENT_RL_DECISION_NONE:
            return "NONE";
        case GWENT_RL_DECISION_TURN:
            return "TURN";
        case GWENT_RL_DECISION_MULLIGAN:
            return "MULLIGAN";
        case GWENT_RL_DECISION_CARD_TARGET:
            return "CARD_TARGET";
        case GWENT_RL_DECISION_ROW_TARGET:
            return "ROW_TARGET";
        case GWENT_RL_DECISION_INSERT_POSITION:
            return "INSERT_POSITION";
        case GWENT_RL_DECISION_FINISHED:
            return "FINISHED";
    }
    return "UNKNOWN";
}

const char* gwent_rl_option_kind_name(gwent_rl_option_kind kind) {
    switch (kind) {
        case GWENT_RL_OPTION_UNKNOWN:
            return "UNKNOWN";
        case GWENT_RL_OPTION_PASS:
            return "PASS";
        case GWENT_RL_OPTION_END_TURN:
            return "END_TURN";
        case GWENT_RL_OPTION_PLAY_CARD:
            return "PLAY_CARD";
        case GWENT_RL_OPTION_DISCARD_CARD:
            return "DISCARD_CARD";
        case GWENT_RL_OPTION_MULLIGAN:
            return "MULLIGAN";
        case GWENT_RL_OPTION_KEEP_HAND:
            return "KEEP_HAND";
        case GWENT_RL_OPTION_USE_LEADER:
            return "USE_LEADER";
        case GWENT_RL_OPTION_USE_ORDER:
            return "USE_ORDER";
        case GWENT_RL_OPTION_CHOOSE_CARD:
            return "CHOOSE_CARD";
        case GWENT_RL_OPTION_CHOOSE_ROW:
            return "CHOOSE_ROW";
        case GWENT_RL_OPTION_CHOOSE_INSERT_POSITION:
            return "CHOOSE_INSERT_POSITION";
    }
    return "UNKNOWN";
}

const char* gwent_rl_reward_mode_name(gwent_rl_reward_mode mode) {
    switch (mode) {
        case GWENT_RL_REWARD_TERMINAL_ONLY:
            return "TERMINAL_ONLY";
        case GWENT_RL_REWARD_ROUND_SHAPING:
            return "ROUND_SHAPING";
        case GWENT_RL_REWARD_DENSE_SHAPING:
            return "DENSE_SHAPING";
        case GWENT_RL_REWARD_STRATEGIC_SHAPING:
            return "STRATEGIC_SHAPING";
    }
    return "UNKNOWN";
}


uint32_t gwent_rl_schema_version(void) {
    return GWENT_RL_SCHEMA_VERSION;
}

uint32_t gwent_rl_action_grammar_version(void) {
    return GWENT_RL_ACTION_GRAMMAR_VERSION;
}

uint32_t gwent_rl_reward_config_version(void) {
    return GWENT_RL_REWARD_CONFIG_VERSION;
}

size_t gwent_rl_global_feature_count(void) {
    return GWENT_RL_GLOBAL_FEATURE_COUNT;
}

size_t gwent_rl_max_objects(void) {
    return GWENT_RL_MAX_OBJECTS;
}

size_t gwent_rl_object_feature_count(void) {
    return GWENT_RL_OBJECT_FEATURE_COUNT;
}

size_t gwent_rl_max_options(void) {
    return GWENT_RL_MAX_OPTIONS;
}

size_t gwent_rl_option_feature_count(void) {
    return GWENT_RL_OPTION_FEATURE_COUNT;
}

size_t gwent_rl_max_prefix(void) {
    return GWENT_RL_MAX_PREFIX;
}

const char* gwent_rl_global_feature_name(size_t index) {
    return indexed_name(kGlobalFeatureNames, GWENT_RL_GLOBAL_FEATURE_COUNT, index);
}

const char* gwent_rl_object_feature_name(size_t index) {
    return indexed_name(kObjectFeatureNames, GWENT_RL_OBJECT_FEATURE_COUNT, index);
}

const char* gwent_rl_option_feature_name(size_t index) {
    return indexed_name(kOptionFeatureNames, GWENT_RL_OPTION_FEATURE_COUNT, index);
}

void gwent_c_string_free(gwent_c_string* value) {
    if (value == nullptr) {
        return;
    }
    std::free(value->data);
    value->data = nullptr;
    value->size = 0;
}

const char* gwent_c_result_code_name(gwent_c_result_code code) {
    switch (code) {
        case GWENT_C_OK:
            return "OK";
        case GWENT_C_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case GWENT_C_OUT_OF_RANGE:
            return "OUT_OF_RANGE";
        case GWENT_C_EXCEPTION:
            return "EXCEPTION";
        case GWENT_C_ALLOCATION_FAILURE:
            return "ALLOCATION_FAILURE";
        case GWENT_C_STALE_DECISION:
            return "STALE_DECISION";
        case GWENT_C_OPTION_OVERFLOW:
            return "OPTION_OVERFLOW";
        case GWENT_C_OBJECT_OVERFLOW:
            return "OBJECT_OVERFLOW";
        case GWENT_C_PREFIX_OVERFLOW:
            return "PREFIX_OVERFLOW";
    }
    return "UNKNOWN";
}

const char* gwent_c_action_status_name(gwent_c_action_status status) {
    switch (status) {
        case GWENT_C_ACTION_APPLIED:
            return "APPLIED";
        case GWENT_C_ACTION_ILLEGAL_ACTION:
            return "ILLEGAL_ACTION";
        case GWENT_C_ACTION_INVALID_PLAYER:
            return "INVALID_PLAYER";
        case GWENT_C_ACTION_NOT_CURRENT_PLAYER:
            return "NOT_CURRENT_PLAYER";
        case GWENT_C_ACTION_INVALID_PHASE:
            return "INVALID_PHASE";
        case GWENT_C_ACTION_INVALID_SOURCE:
            return "INVALID_SOURCE";
        case GWENT_C_ACTION_INVALID_TARGET:
            return "INVALID_TARGET";
        case GWENT_C_ACTION_EMPTY_DECK:
            return "EMPTY_DECK";
        case GWENT_C_ACTION_UNSUPPORTED_ACTION:
            return "UNSUPPORTED_ACTION";
        case GWENT_C_ACTION_TASK_LIMIT_EXCEEDED:
            return "TASK_LIMIT_EXCEEDED";
        case GWENT_C_ACTION_INVARIANT_VIOLATION:
            return "INVARIANT_VIOLATION";
        case GWENT_C_ACTION_UNKNOWN:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // extern "C"
