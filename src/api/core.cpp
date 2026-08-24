#include "gwent/api/core.hpp"

#include "gwent/cards/deck_a.hpp"
#include "gwent/cards/supported_cards.hpp"

#include <utility>

namespace gwent::api {

Game Game::create(const MatchSpec& spec, EffectRegistry effects, const MatchConfig& config) {
    Game game;
    game.kernel_config_ = config.kernel_config;
    game.effects_ = std::move(effects);

    MatchSetupConfig setup_config;
    setup_config.seed = config.seed;
    setup_config.starting_player_id = config.starting_player_id;
    setup_config.shuffle_decks = config.shuffle_decks;
    setup_config.hand_limit = game.kernel_config_.hand_limit;

    game.state_ = make_empty_standard_state(setup_config);
    const auto setup_result = setup_standard_match(game.state_, spec.deck0, spec.deck1, setup_config);
    (void)setup_result;

    // A standard match initially interns only definitions that physically occur
    // in the two deck specs. Supported effects can also create cards at runtime
    // or request card-definition choices whose helper definitions are not part
    // of either starting deck (for example Red Riders' three mode choices).
    // Keep the shared catalog complete before those effects execute. CardCatalog
    // uses stable storage, so interning the remaining immutable definitions does
    // not invalidate RuntimeCard::definition pointers created during setup.
    for (CardDefinition definition : supported_cards::make_all_definitions()) {
        (void)game.state_.card_catalog->intern(std::move(definition));
    }

    return game;
}

const GameState& Game::state() const noexcept {
    return state_;
}

GameState& Game::mutable_state() noexcept {
    return state_;
}

std::vector<Action> Game::legal_actions() const {
    LegalActionConfig legal_config;
    legal_config.performance_counters = kernel_config_.performance_counters;
    return make_current_turn_decision(state_, legal_config).legal_actions;
}

Decision Game::current_decision() const {
    LegalActionConfig legal_config;
    legal_config.performance_counters = kernel_config_.performance_counters;
    return make_current_turn_decision(state_, legal_config);
}

KernelResult Game::apply(const Action& action) {
    return apply_action_with_kernel(state_, action, effects_, kernel_config_);
}

InvariantReport Game::validate(const InvariantOptions& options) const {
    InvariantOptions effective_options = options;
    if (effective_options.performance_counters == nullptr) {
        effective_options.performance_counters = kernel_config_.performance_counters;
    }
    return validate_state_invariants(state_, effective_options);
}

bool Game::is_finished() const noexcept {
    return state_.status == MatchStatus::Finished;
}

std::optional<PlayerId> Game::winner() const noexcept {
    return state_.winner_id;
}

std::string Game::snapshot_json(const trace::SnapshotOptions& options) const {
    PerformanceScope scope(kernel_config_.performance_counters, PerformanceMetric::SnapshotJson);
    return trace::state_to_json(state_, options);
}

std::string Game::checksum(const trace::SnapshotOptions& options) const {
    PerformanceScope scope(kernel_config_.performance_counters, PerformanceMetric::SnapshotJson);
    return trace::state_checksum(state_, options);
}

DeckAGame DeckAGame::create(const DeckAMatchConfig& config) {
    EffectRegistry effects;
    supported_cards::register_supported_card_effects(effects);
    const DeckSpec deck = deck_a::make_deck_spec();

    DeckAGame game;
    game.game_ = Game::create(MatchSpec{deck, deck}, std::move(effects), config);
    return game;
}

const GameState& DeckAGame::state() const noexcept { return game_.state(); }
GameState& DeckAGame::mutable_state() noexcept { return game_.mutable_state(); }
std::vector<Action> DeckAGame::legal_actions() const { return game_.legal_actions(); }
Decision DeckAGame::current_decision() const { return game_.current_decision(); }
KernelResult DeckAGame::apply(const Action& action) { return game_.apply(action); }
InvariantReport DeckAGame::validate(const InvariantOptions& options) const { return game_.validate(options); }
bool DeckAGame::is_finished() const noexcept { return game_.is_finished(); }
std::optional<PlayerId> DeckAGame::winner() const noexcept { return game_.winner(); }
std::string DeckAGame::snapshot_json(const trace::SnapshotOptions& options) const {
    return game_.snapshot_json(options);
}
std::string DeckAGame::checksum(const trace::SnapshotOptions& options) const {
    return game_.checksum(options);
}

}  // namespace gwent::api
