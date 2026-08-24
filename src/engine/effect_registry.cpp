#include "gwent/engine/effect_registry.hpp"
#include "gwent/engine/board_rules.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace gwent {


namespace {

constexpr std::uint64_t kMask64 = 0xffffffffffffffffull;

std::uint64_t splitmix64(std::uint64_t x) {
    x = (x + 0x9E3779B97F4A7C15ull) & kMask64;
    std::uint64_t z = x;
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull) & kMask64;
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EBull) & kMask64;
    return (z ^ (z >> 31)) & kMask64;
}

std::uint64_t next_u64(GameState& state) {
    if (state.rng_state == 0) {
        state.rng_state = splitmix64(0);
        if (state.rng_state == 0) {
            state.rng_state = 0xA5A5A5A5A5A5A5A5ull;
        }
    }
    std::uint64_t x = state.rng_state;
    x ^= (x >> 12) & kMask64;
    x ^= (x << 25) & kMask64;
    x ^= (x >> 27) & kMask64;
    state.rng_state = x & kMask64;
    return (x * 0x2545F4914F6CDD1Dull) & kMask64;
}

std::uint64_t randbelow(GameState& state, std::uint64_t n) {
    if (n == 0) {
        throw std::invalid_argument("randbelow requires n > 0");
    }
    const std::uint64_t remainder = ((kMask64 % n) + 1) % n;
    const std::uint64_t limit = std::uint64_t{0} - remainder;
    while (true) {
        const std::uint64_t x = next_u64(state);
        if (remainder == 0 || x < limit) {
            return x % n;
        }
    }
}

}  // namespace

void KernelContext::emit(EventRecord event) {
    events.push_back(std::move(event));
}

void KernelContext::enqueue(Task task) {
    queue.push_back(std::move(task));
}

void KernelContext::enqueue_spawn_card(
    PlayerId actor_id,
    CardDefinition definition,
    Location destination,
    EntityId source_entity_id
) {
    if (!state.card_catalog) {
        state.card_catalog = std::make_shared<CardCatalog>();
    }
    const CardDefId definition_id = state.card_catalog->intern(std::move(definition));
    queue.push_back(Task::spawn_card(actor_id, definition_id, destination, source_entity_id));
}

void KernelContext::set_continuation(std::vector<ContinuationStep> steps) {
    continuation = std::move(steps);
}

void KernelContext::clear_continuation() noexcept {
    continuation.clear();
}

void KernelContext::append_continuation(ContinuationStep step) {
    continuation.push_back(step);
}

ListenerId KernelContext::add_listener(RuntimeListener listener) {
    if (listener.listener_id <= 0) {
        listener.listener_id = ++state.next_listener_id;
    } else if (listener.listener_id > state.next_listener_id) {
        state.next_listener_id = listener.listener_id;
    }
    const ListenerId id = listener.listener_id;
    state.listeners[id] = std::move(listener);
    emit(EventRecord{TaskType::Pass, "listener_registered", kPlayerZero, state.listeners[id].source_id.value_or(kInvalidEntityId), kInvalidEntityId, id, state.listeners[id].event});
    return id;
}

ListenerId KernelContext::add_listener(
    std::string event,
    std::string handler,
    std::optional<EntityId> source_id,
    bool once,
    std::unordered_map<std::string, std::string> filters,
    std::unordered_map<std::string, std::string> data,
    bool require_source,
    bool source_must_be_on_board,
    bool disabled_by_lock
) {
    RuntimeListener listener;
    listener.event = std::move(event);
    listener.handler = std::move(handler);
    listener.source_id = source_id;
    listener.once = once;
    listener.filters = std::move(filters);
    listener.data = std::move(data);
    listener.require_source = require_source;
    listener.source_must_be_on_board = source_must_be_on_board;
    listener.disabled_by_lock = disabled_by_lock;
    return add_listener(std::move(listener));
}

bool KernelContext::remove_listener(ListenerId listener_id) {
    const auto erased = state.listeners.erase(listener_id);
    if (erased > 0) {
        emit(EventRecord{TaskType::Pass, "listener_removed", kPlayerZero, kInvalidEntityId, kInvalidEntityId, listener_id, {}});
    }
    return erased > 0;
}


std::size_t KernelContext::random_index(std::size_t size) {
    if (size == 0) {
        throw std::invalid_argument("random_index requires size > 0");
    }
    return static_cast<std::size_t>(randbelow(state, static_cast<std::uint64_t>(size)));
}

std::optional<Location> KernelContext::random_open_battle_row(PlayerId player_id) {
    std::vector<Zone> rows;
    rows.reserve(kBattleZones.size());
    for (Zone zone : kBattleZones) {
        if (row_has_space(state, player_id, zone)) {
            rows.push_back(zone);
        }
    }
    if (rows.empty()) {
        return std::nullopt;
    }
    const Zone zone = rows[random_index(rows.size())];
    return Location{player_id, zone, state.player(player_id).row(zone).size()};
}

void KernelContext::request_card_choice(
    PlayerId player_id,
    EntityId source_entity_id,
    std::string_view effect_id,
    std::string prompt,
    std::vector<EntityId> legal_card_targets
) {
    // A Deploy resolves only after the unit has been inserted onto the board.
    // The newly deployed source must not appear among its own player-facing
    // card targets.  Keep this rule in the choice boundary so every current
    // and future deploy effect gets the same behavior instead of relying on
    // individual card handlers to remember `exclude_source`.
    if (choice_origin_action_type == ActionType::PlayCard) {
        const auto source_location = state.location_of(source_entity_id);
        if (source_location.has_value() && is_battle_zone(source_location->zone)) {
            legal_card_targets.erase(
                std::remove(legal_card_targets.begin(), legal_card_targets.end(), source_entity_id),
                legal_card_targets.end()
            );
        }
    }

    // Do not create an empty pending choice.  An effect with no remaining
    // legal target simply continues/finishes normally.
    if (legal_card_targets.empty()) {
        emit(EventRecord{
            TaskType::ResolvePendingCardChoice,
            "pending_card_choice_skipped_no_legal_targets",
            is_valid_player_id(player_id) ? player_id : kPlayerZero,
            source_entity_id,
            kInvalidEntityId,
            0,
            std::string(effect_id)
        });
        return;
    }

    PendingChoice choice;
    choice.kind = PendingChoiceKind::CardTarget;
    choice.choice_id = state.next_pending_choice_id++;
    choice.player_id = is_valid_player_id(player_id) ? player_id : kPlayerZero;
    choice.source_entity_id = source_entity_id;
    choice.effect_id = std::string(effect_id);
    choice.prompt = std::move(prompt);
    choice.origin_action_type = choice_origin_action_type;
    choice.legal_card_targets = std::move(legal_card_targets);
    choice.continuation = continuation;
    choice.suspended_tasks = queue.take_all();
    choice.resolution_frame = resolution_frame;
    if (resolution_frame) {
        consume_choice_budget(*resolution_frame);
    }
    state.pending_choice = std::move(choice);
    state.engine_phase = EnginePhase::AwaitingChoice;

    const PendingChoice& stored = state.pending_choice.value();
    emit(EventRecord{
        TaskType::ResolvePendingCardChoice,
        "pending_card_choice_requested",
        stored.player_id,
        stored.source_entity_id,
        kInvalidEntityId,
        static_cast<int>(stored.legal_card_targets.size()),
        stored.effect_id
    });
}

void KernelContext::request_row_choice(
    PlayerId player_id,
    EntityId source_entity_id,
    std::string_view effect_id,
    std::string prompt,
    std::vector<Location> legal_row_targets
) {
    PendingChoice choice;
    choice.kind = PendingChoiceKind::RowTarget;
    choice.choice_id = state.next_pending_choice_id++;
    choice.player_id = is_valid_player_id(player_id) ? player_id : kPlayerZero;
    choice.source_entity_id = source_entity_id;
    choice.effect_id = std::string(effect_id);
    choice.prompt = std::move(prompt);
    choice.origin_action_type = choice_origin_action_type;
    choice.legal_row_targets = std::move(legal_row_targets);
    choice.continuation = continuation;
    choice.suspended_tasks = queue.take_all();
    choice.resolution_frame = resolution_frame;
    if (resolution_frame) {
        consume_choice_budget(*resolution_frame);
    }
    state.pending_choice = std::move(choice);
    state.engine_phase = EnginePhase::AwaitingChoice;

    const PendingChoice& stored = state.pending_choice.value();
    emit(EventRecord{
        TaskType::ResolvePendingCardChoice,
        "pending_row_choice_requested",
        stored.player_id,
        stored.source_entity_id,
        kInvalidEntityId,
        static_cast<int>(stored.legal_row_targets.size()),
        stored.effect_id
    });
}

void KernelContext::request_card_definition_choice(
    PlayerId player_id,
    EntityId source_entity_id,
    std::string_view effect_id,
    std::string prompt,
    std::vector<CardDefId> legal_definition_ids
) {
    PendingChoice choice;
    choice.kind = PendingChoiceKind::CardDefinitionChoice;
    choice.choice_id = state.next_pending_choice_id++;
    choice.player_id = is_valid_player_id(player_id) ? player_id : kPlayerZero;
    choice.source_entity_id = source_entity_id;
    choice.effect_id = std::string(effect_id);
    choice.prompt = std::move(prompt);
    choice.origin_action_type = choice_origin_action_type;
    choice.legal_card_definition_ids = std::move(legal_definition_ids);
    choice.continuation = continuation;
    choice.suspended_tasks = queue.take_all();
    choice.resolution_frame = resolution_frame;
    if (resolution_frame) consume_choice_budget(*resolution_frame);
    state.pending_choice = std::move(choice);
    state.engine_phase = EnginePhase::AwaitingChoice;
    emit(EventRecord{TaskType::ResolvePendingCardChoice, "pending_card_definition_choice_requested", player_id, source_entity_id, kInvalidEntityId, static_cast<int>(state.pending_choice->legal_card_definition_ids.size()), std::string(effect_id)});
}


void KernelContext::request_play_row_choice(
    PlayerId player_id,
    EntityId source_entity_id,
    std::string prompt,
    std::vector<Location> legal_row_targets
) {
    PendingChoice choice;
    choice.kind = PendingChoiceKind::RowTarget;
    choice.resume_kind = PendingChoiceResumeKind::PlayCard;
    choice.choice_id = state.next_pending_choice_id++;
    choice.player_id = is_valid_player_id(player_id) ? player_id : kPlayerZero;
    choice.source_entity_id = source_entity_id;
    choice.prompt = std::move(prompt);
    choice.origin_action_type = ActionType::PlayCard;
    choice.legal_row_targets = std::move(legal_row_targets);
    choice.continuation = continuation;
    choice.suspended_tasks = queue.take_all();
    choice.resolution_frame = resolution_frame;
    if (resolution_frame) {
        consume_choice_budget(*resolution_frame);
    }
    state.pending_choice = std::move(choice);
    state.engine_phase = EnginePhase::AwaitingChoice;

    const PendingChoice& stored = state.pending_choice.value();
    emit(EventRecord{
        TaskType::ResolvePendingCardChoice,
        "play_row_choice_requested",
        stored.player_id,
        stored.source_entity_id,
        kInvalidEntityId,
        static_cast<int>(stored.legal_row_targets.size()),
        "PlayCard"
    });
}

void EffectRegistry::register_handler(std::string effect_id, EffectHandler handler) {
    handlers_[std::move(effect_id)] = std::move(handler);
}

bool EffectRegistry::contains(std::string_view effect_id) const {
    return handlers_.find(effect_id) != handlers_.end();
}

bool EffectRegistry::invoke(KernelContext& context, const EffectCall& call) const {
    if (context.resolution_frame) {
        consume_effect_budget(*context.resolution_frame);
    }
    const auto it = handlers_.find(call.effect_id);
    if (it == handlers_.end()) {
        return false;
    }
    it->second(context, call);
    return true;
}

std::size_t EffectRegistry::size() const noexcept {
    return handlers_.size();
}

}  // namespace gwent
