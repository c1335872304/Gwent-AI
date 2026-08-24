#include "gwent/cards/supported_cards.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gwent/core/location.hpp"
#include "gwent/generated/supported_card_data.hpp"
#include "gwent/engine/primitives.hpp"
#include "gwent/engine/board_rules.hpp"
#include "gwent/engine/row_effects.hpp"
#include "gwent/engine/targets.hpp"
#include "gwent/engine/task.hpp"

namespace gwent {
namespace supported_cards {
namespace {

inline constexpr std::string_view kVampire = "吸血鬼";
inline constexpr std::string_view kWildHunt = "狂猎";

bool require_or_request_card_target(
    KernelContext& context,
    const EffectCall& call,
    const TargetSelector& selector,
    const std::string& prompt
) {
    const TargetContext target_context{call.actor_id, call.source_entity_id, call.target};
    if (call.target.kind == ActionTargetKind::Card) {
        return action_card_target_is_valid(context.state, target_context, selector);
    }

    if (call.target.kind != ActionTargetKind::None && call.target.kind != ActionTargetKind::Row) {
        return false;
    }

    TargetSelector choice_selector = selector;
    choice_selector.max_targets = 0;
    std::vector<EntityId> legal_targets = select_card_targets(context.state, target_context, choice_selector);
    if (legal_targets.empty()) {
        return false;
    }

    context.request_card_choice(
        call.actor_id,
        call.source_entity_id,
        call.effect_id,
        prompt,
        std::move(legal_targets)
    );
    return false;
}

void emit_invalid_target(KernelContext& context, const EffectCall& call, std::string message) {
    context.emit(EventRecord{TaskType::ResolveOrder, "supported:invalid_target", call.actor_id, call.source_entity_id, kInvalidEntityId, 0, std::move(message)});
}

TargetSelector own_hand_unit_selector() {
    TargetSelector selector;
    selector.side = RelativeSide::Actor;
    selector.zone_scope = ZoneScope::Hand;
    selector.card_kind = TargetCardKind::Unit;
    selector.max_targets = 1;
    return selector;
}

TargetSelector enemy_unit_selector() {
    TargetSelector selector;
    selector.side = RelativeSide::Opponent;
    selector.zone_scope = ZoneScope::BattleRows;
    selector.card_kind = TargetCardKind::HasPower;
    selector.require_not_immune = true;
    selector.max_targets = 1;
    return selector;
}


TargetSelector battlefield_unit_selector() {
    TargetSelector selector;
    selector.side = RelativeSide::Any;
    selector.zone_scope = ZoneScope::BattleRows;
    selector.card_kind = TargetCardKind::HasPower;
    selector.require_not_immune = true;
    selector.max_targets = 1;
    return selector;
}

std::optional<Location> same_row_end_location(const GameState& state, EntityId anchor_id) {
    const auto source_location = state.location_of(anchor_id);
    if (!source_location.has_value() || !is_battle_zone(source_location->zone)) {
        return std::nullopt;
    }
    return Location{source_location->side, source_location->zone, state.player(source_location->side).row(source_location->zone).size()};
}

bool is_monsters_devotion(const GameState& state, PlayerId owner_id) {
    for (const RuntimeCard& card : state.cards) {
        if (card.owner_id != owner_id || card.is_token()) {
            continue;
        }
        if (card.definition->card_type == CardType::Leader || card.definition->card_type == CardType::Stratagem) {
            continue;
        }
        if (card.definition->faction != Faction::Monsters) {
            return false;
        }
    }
    return true;
}

bool has_bonded_same_name_on_side(const GameState& state, EntityId source_id) {
    const RuntimeCard* source = state.find_card(source_id);
    if (source == nullptr) {
        return false;
    }
    for (Zone zone : kBattleZones) {
        for (EntityId other_id : state.player(source->controller_id).row(zone)) {
            if (other_id == source_id) {
                continue;
            }
            const RuntimeCard* other = state.find_card(other_id);
            if (other != nullptr && other->definition->name == source->definition->name) {
                return true;
            }
        }
    }
    return false;
}

std::vector<EntityId> strongest_enemy_units(const GameState& state, PlayerId actor_id) {
    const PlayerId enemy = state.opponent_id(actor_id);
    int best = -1;
    std::vector<EntityId> ids;
    for (Zone zone : kBattleZones) {
        for (EntityId entity_id : state.player(enemy).row(zone)) {
            const RuntimeCard* card = state.find_card(entity_id);
            // Riptide does not ask the player to target a unit; it resolves
            // automatically against a highest-power enemy. Immunity only
            // blocks player-selected targeting, so immune units remain valid
            // candidates for this automatic effect.
            if (card == nullptr || !card->is_unit_card()) {
                continue;
            }
            if (card->state.power > best) {
                best = card->state.power;
                ids.clear();
                ids.push_back(entity_id);
            } else if (card->state.power == best) {
                ids.push_back(entity_id);
            }
        }
    }
    return ids;
}

int adjacent_friendly_vampires(const GameState& state, EntityId source_id) {
    const RuntimeCard* source = state.find_card(source_id);
    const auto location = state.location_of(source_id);
    if (source == nullptr || !location.has_value() || !is_battle_zone(location->zone)) {
        return 0;
    }
    const auto& row = state.player(location->side).row(location->zone);
    int count = 0;
    for (int delta : {-1, 1}) {
        const int index = static_cast<int>(location->index) + delta;
        if (index < 0 || index >= static_cast<int>(row.size())) {
            continue;
        }
        const RuntimeCard* neighbor = state.find_card(row[static_cast<std::size_t>(index)]);
        if (neighbor != nullptr && neighbor->controller_id == source->controller_id && neighbor->has_category(kVampire)) {
            count += 1;
        }
    }
    return count;
}

void notify_row_effect_applied(KernelContext& context, PlayerId source_player_id, int duration);

void add_blood_moon_row_effect(KernelContext& context, PlayerId source_player_id, PlayerId side, Zone zone, int duration) {
    add_or_extend_row_effect(context.state, side, zone, "blood_moon", duration, side, {
        {"bleeding_turns", "2"},
        {"damage", "2"},
    });
    notify_row_effect_applied(context, source_player_id, duration);
}

TargetSelector allied_unit_selector() {
    TargetSelector selector;
    selector.side = RelativeSide::Actor;
    selector.zone_scope = ZoneScope::BattleRows;
    selector.card_kind = TargetCardKind::HasPower;
    selector.max_targets = 1;
    return selector;
}

void notify_row_effect_applied(KernelContext& context, PlayerId source_player_id, int duration) {
    if (!is_valid_player_id(source_player_id) || duration <= 0) return;
    for (Zone zone : kBattleZones) {
        for (EntityId entity_id : context.state.player(source_player_id).row(zone)) {
            const RuntimeCard* card = context.state.find_card(entity_id);
            if (card == nullptr) continue;
            const auto marker = card->definition->metadata.find("boost_on_row_effect_applied");
            if (!card->state.locked && marker != card->definition->metadata.end() && marker->second == "true") {
                context.enqueue(Task::boost_card(source_player_id, entity_id, duration, entity_id));
            }
        }
    }
}

void add_frost_row_effect(KernelContext& context, PlayerId source_player_id, PlayerId side, Zone zone, int duration) {
    add_or_extend_row_effect(context.state, side, zone, "frost", duration, side, {{"damage", "2"}});
    notify_row_effect_applied(context, source_player_id, duration);
    if (is_valid_player_id(source_player_id) && side == context.state.opponent_id(source_player_id) && is_battle_zone(zone)) {
        context.state.turn_contexts[static_cast<std::size_t>(source_player_id)].opponent_rows_frosted_this_turn[row_index(zone)] = true;
    }
}

bool enemy_side_has_bleeding_unit(const GameState& state, PlayerId owner_id) {
    const PlayerId enemy = state.opponent_id(owner_id);
    for (Zone zone : kBattleZones) {
        for (EntityId entity_id : state.player(enemy).row(zone)) {
            const RuntimeCard* card = state.find_card(entity_id);
            if (card != nullptr && card->has_power() && card->state.bleeding > 0) {
                return true;
            }
        }
    }
    return false;
}

TargetSelector own_cemetery_unit_selector() {
    TargetSelector selector;
    selector.side = RelativeSide::Actor;
    selector.zone_scope = ZoneScope::Cemetery;
    selector.card_kind = TargetCardKind::Unit;
    selector.max_targets = 1;
    return selector;
}

TargetSelector enemy_cemetery_unit_selector() {
    TargetSelector selector;
    selector.side = RelativeSide::Opponent;
    selector.zone_scope = ZoneScope::Cemetery;
    selector.card_kind = TargetCardKind::Unit;
    selector.max_targets = 1;
    return selector;
}

void attach_verena_boost_block(GameState& state, EntityId source_entity_id, EntityId target_entity_id) {
    RuntimeCard* target = state.find_card(target_entity_id);
    const RuntimeCard* source = state.find_card(source_entity_id);
    const auto source_location = state.location_of(source_entity_id);
    if (target == nullptr || source == nullptr || !source_location.has_value() || !is_battle_zone(source_location->zone)) {
        return;
    }
    if (target->controller_id == source->controller_id || target->state.bleeding <= 0) {
        return;
    }
    target->memory["boost_blocked_by:" + std::to_string(source_entity_id)] = "true";
}

void refresh_verena_boost_blocks(GameState& state, EntityId source_entity_id) {
    const RuntimeCard* source = state.find_card(source_entity_id);
    if (source == nullptr) {
        return;
    }
    const PlayerId enemy = state.opponent_id(source->controller_id);
    for (Zone zone : kBattleZones) {
        for (EntityId target_id : state.player(enemy).row(zone)) {
            attach_verena_boost_block(state, source_entity_id, target_id);
        }
    }
}

bool draw_one_to_hand(GameState& state, PlayerId player_id, int hand_limit = 10) {
    PlayerState& player = state.player(player_id);
    if (player.deck.empty() || static_cast<int>(player.hand.size()) >= hand_limit) {
        return false;
    }
    const EntityId drawn = player.deck.front();
    primitive_move_card(state, drawn, Location{player_id, Zone::Hand, player.hand.size()});
    return true;
}

bool card_is_in_zone(const GameState& state, EntityId entity_id, PlayerId side, Zone zone) {
    const auto location = state.location_of(entity_id);
    return location.has_value() && location->side == side && location->zone == zone;
}

void change_base_power(RuntimeCard& card, int amount) {
    if (!card.has_power() || amount == 0) {
        return;
    }
    card.state.base_power += amount;
    card.state.power += amount;
}

void install_board_listener(KernelContext& context, const EffectCall& call, std::string event, std::string handler, std::unordered_map<std::string, std::string> filters = {}) {
    RuntimeListener listener;
    listener.event = std::move(event);
    listener.handler = std::move(handler);
    listener.source_id = call.source_entity_id;
    listener.once = false;
    listener.filters = std::move(filters);
    listener.require_source = true;
    listener.source_must_be_on_board = true;
    context.add_listener(std::move(listener));
}


bool is_gold_card(const RuntimeCard& card) {
    return card.definition->color == "Gold" || card.definition->color == "gold";
}

bool is_wild_hunt_card(const RuntimeCard& card) {
    return card.has_category(kWildHunt);
}

std::vector<EntityId> first_gold_cards_in_deck(const GameState& state, PlayerId player_id, std::size_t limit) {
    std::vector<EntityId> result;
    for (EntityId entity_id : state.player(player_id).deck) {
        const RuntimeCard* card = state.find_card(entity_id);
        if (card != nullptr && is_gold_card(*card)) {
            result.push_back(entity_id);
            if (result.size() >= limit) {
                break;
            }
        }
    }
    return result;
}

std::vector<EntityId> random_gold_cards_in_deck(KernelContext& context, PlayerId player_id, std::size_t limit) {
    std::vector<EntityId> pool = first_gold_cards_in_deck(context.state, player_id, context.state.player(player_id).deck.size());
    const std::size_t count = std::min(limit, pool.size());
    std::vector<EntityId> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t index = context.random_index(pool.size());
        result.push_back(pool[index]);
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(index));
    }
    return result;
}

std::vector<EntityId> wild_hunt_play_candidates(const GameState& state, PlayerId player_id, bool devotion) {
    std::vector<EntityId> result;
    for (EntityId entity_id : state.player(player_id).deck) {
        const RuntimeCard* card = state.find_card(entity_id);
        if (card == nullptr || !is_wild_hunt_card(*card)) {
            continue;
        }
        if (!devotion && card->definition->card_type != CardType::Special) {
            continue;
        }
        result.push_back(entity_id);
    }
    return result;
}

bool entity_in_list(EntityId id, const std::vector<EntityId>& ids) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string encode_entity_id_list(const std::vector<EntityId>& ids) {
    std::string encoded;
    for (EntityId id : ids) {
        if (!encoded.empty()) {
            encoded.push_back(',');
        }
        encoded += std::to_string(id);
    }
    return encoded;
}

std::vector<EntityId> decode_entity_id_list(std::string_view encoded) {
    std::vector<EntityId> ids;
    std::size_t start = 0;
    while (start <= encoded.size()) {
        const std::size_t end = encoded.find(',', start);
        const std::string_view token = end == std::string_view::npos
            ? encoded.substr(start)
            : encoded.substr(start, end - start);
        if (!token.empty()) {
            int value = 0;
            const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
            if (ec == std::errc() && ptr == token.data() + token.size()) {
                ids.push_back(value);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return ids;
}

void enqueue_play_from_staging(KernelContext& context, PlayerId actor_id, EntityId entity_id) {
    const RuntimeCard* card = context.state.find_card(entity_id);
    const auto location = context.state.location_of(entity_id);
    if (card == nullptr || !location.has_value() || location->side != actor_id
        || (location->zone != Zone::Deck && location->zone != Zone::Stay)) {
        context.emit(EventRecord{TaskType::PlayCard, "supported:play_from_staging_invalid", actor_id, kInvalidEntityId, entity_id, 0, {}});
        return;
    }

    // Every unit that is "played" by another card must use the exact same
    // structural PlayCard chain as a hand-played unit:
    //   source -> row -> insert position -> enter board -> Deploy.
    // The legacy helper asked for a row itself and then appended the unit at
    // the row end, which bypassed ChooseInsertPosition and became inconsistent
    // as soon as dynamic insertion was introduced.
    if (card->definition->card_type != CardType::Special) {
        if (!row_has_space(context.state, actor_id, Zone::Melee)
            && !row_has_space(context.state, actor_id, Zone::Ranged)) {
            context.emit(EventRecord{TaskType::PlayCard, "supported:play_from_deck_no_row_space", actor_id, entity_id, entity_id, 0, card->definition->name});
            return;
        }
        if (location->zone == Zone::Deck) {
            primitive_move_card(
                context.state,
                entity_id,
                Location{actor_id, Zone::Stay, context.state.player(actor_id).stay.size()}
            );
        }
    }

    // Do not clear the outer continuation here. A replay created by a Special
    // still needs its FinishSpecialCard continuation after the replayed card's
    // own row/insert/deploy chain has completed.
    Task play = Task::play_card(actor_id, entity_id, ActionTarget::none());
    play.reason = "from_deck";
    context.enqueue(std::move(play));
    context.emit(EventRecord{TaskType::PlayCard, "supported:play_from_deck_enqueued", actor_id, entity_id, entity_id, 0, card->definition->name});
}

void finish_source_special_now(KernelContext& context, PlayerId actor_id, EntityId source_id) {
    RuntimeCard* source = context.state.find_card(source_id);
    const auto location = context.state.location_of(source_id);
    if (source == nullptr || !location.has_value() || location->zone != Zone::Stay) {
        return;
    }
    const Zone destination = source->state.doomed ? Zone::Banished : Zone::Cemetery;
    primitive_move_card(context.state, source_id, Location{source->owner_id, destination, context.state.player(source->owner_id).zone_size(destination)});
    context.emit(EventRecord{TaskType::FinishSpecialCard, destination == Zone::Banished ? "special_banished" : "special_moved_to_cemetery", actor_id, source_id, source_id, 0, "pre-finished before deck play"});
}

}  // namespace


CardDefinition make_blood_scent_definition() {
    return generated::make_supported_card_definition(kBloodScentId);
}

int memory_int(const RuntimeCard& card, std::string_view key) {
    const auto it = card.memory.find(std::string(key));
    if (it == card.memory.end()) return 0;
    int value = 0;
    const auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
    return ec == std::errc{} && ptr == it->second.data() + it->second.size() ? value : 0;
}

int distinct_starting_bronze_wild_hunt(const GameState& state, PlayerId player) {
    if (state.card_catalog == nullptr) return 0;
    std::vector<std::string> ids;
    for (const std::string& id : state.player(player).starting_deck) {
        if (!state.card_catalog->contains(id)) continue;
        const CardDefinition& definition = state.card_catalog->at(id);
        if (definition.color == "Bronze" && definition.has_category(kWildHunt)) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return static_cast<int>(ids.size());
}

CardDefinition make_white_frost_definition() { return generated::make_supported_card_definition(kWhiteFrostId); }

CardDefinition make_ekimmara_definition() {
    return generated::make_supported_card_definition(kEkimmaraId);
}

CardDefinition make_armorer_workshop_definition() {
    return generated::make_supported_card_definition(kArmorerWorkshopId);
}

CardDefinition make_crystal_skull_definition() { return generated::make_supported_card_definition(kCrystalSkullId); }

CardDefinition make_regis_reborn_definition() {
    return generated::make_supported_card_definition(kRegisRebornId);
}

CardDefinition make_unseen_elder_definition() {
    return generated::make_supported_card_definition(kUnseenElderId);
}

CardDefinition make_giant_centipede_definition() {
    return generated::make_supported_card_definition(kGiantCentipedeId);
}

CardDefinition make_dettlaff_aep_definition() {
    return generated::make_supported_card_definition(kDettlaffAepId);
}

CardDefinition make_lord_riptide_definition() {
    return generated::make_supported_card_definition(kLordRiptideId);
}

CardDefinition make_katakan_definition() {
    return generated::make_supported_card_definition(kKatakanId);
}

CardDefinition make_verena_definition() {
    return generated::make_supported_card_definition(kVerenaId);
}

CardDefinition make_naglfar_definition() {
    return generated::make_supported_card_definition(kNaglfarId);
}

CardDefinition make_imlerith_definition() {
    return generated::make_supported_card_definition(kImlerithId);
}

CardDefinition make_geels_definition() {
    return generated::make_supported_card_definition(kGeelsId);
}

CardDefinition make_ozzrel_definition() {
    return generated::make_supported_card_definition(kOzzrelId);
}

CardDefinition make_queen_of_the_night_definition() {
    return generated::make_supported_card_definition(kQueenOfTheNightId);
}

CardDefinition make_fleder_definition() {
    return generated::make_supported_card_definition(kFlederId);
}

CardDefinition make_garkain_definition() {
    return generated::make_supported_card_definition(kGarkainId);
}

CardDefinition make_bruxa_definition() {
    return generated::make_supported_card_definition(kBruxaId);
}

CardDefinition make_feast_of_blood_definition() {
    return generated::make_supported_card_definition(kFeastOfBloodId);
}

CardDefinition make_aen_elle_conqueror_definition() {
    return generated::make_supported_card_definition(kAenElleConquerorId);
}

CardDefinition make_bloodscented_predator_definition() {
    return generated::make_supported_card_definition(kBloodscentedPredatorId);
}

std::vector<CardDefId> bronze_wild_hunt_pool(const GameState& state) {
    std::vector<CardDefId> pool;
    if (!state.card_catalog) return pool;
    for (const CardDefinition& definition : state.card_catalog->all()) {
        if (!definition.is_token && definition.card_type == CardType::Unit && definition.color == "Bronze" && definition.has_category(kWildHunt)) {
            pool.push_back(state.card_catalog->id_of(definition.id).value());
        }
    }
    std::sort(pool.begin(), pool.end(), [&](CardDefId a, CardDefId b) { return state.card_catalog->at(a).id < state.card_catalog->at(b).id; });
    return pool;
}

void spawn_and_play_created(KernelContext& context, PlayerId actor, CardDefId definition_id) {
    if (!row_has_space(context.state, actor, Zone::Melee) && !row_has_space(context.state, actor, Zone::Ranged)) {
        return;
    }

    const EntityId created = primitive_spawn_card(
        context.state,
        definition_id,
        actor,
        Location{actor, Zone::Stay, context.state.player(actor).stay.size()}
    );
    if (created == kInvalidEntityId) {
        return;
    }

    // A created unit must enter the ordinary play chain. This keeps the newly
    // created card staged while the player chooses row and dynamic insert
    // position, instead of hiding it behind the legacy custom row-choice path.
    Task play = Task::play_card(actor, created, ActionTarget::none());
    play.reason = "created";
    context.enqueue(std::move(play));
    context.emit(EventRecord{
        TaskType::PlayCard,
        "supported:created_card_staged_for_play",
        actor,
        created,
        created,
        0,
        context.state.find_card(created)->definition->name
    });
}

bool starting_deck_devotion(const GameState& state, PlayerId player) {
    if (!state.card_catalog) return false;
    for (const std::string& id : state.player(player).starting_deck) {
        if (!state.card_catalog->contains(id) || state.card_catalog->at(id).faction != Faction::Monsters) return false;
    }
    return true;
}

CardDefinition make_wild_hunt_navigator_definition() {
    return generated::make_supported_card_definition(kWildHuntNavigatorId);
}

CardDefinition make_wild_hunt_rider_definition() {
    return generated::make_supported_card_definition(kWildHuntRiderId);
}

CardDefinition make_wild_hunt_warrior_definition() {
    return generated::make_supported_card_definition(kWildHuntWarriorId);
}

CardDefinition make_wild_hunt_hound_definition() {
    return generated::make_supported_card_definition(kWildHuntHoundId);
}

CardDefinition make_wild_hunt_bruiser_definition() {
    return generated::make_supported_card_definition(kWildHuntBruiserId);
}

CardDefinition make_naglfar_crew_definition() {
    return generated::make_supported_card_definition(kNaglfarCrewId);
}

CardDefinition make_naglfar_taskmaster_definition() {
    return generated::make_supported_card_definition(kNaglfarTaskmasterId);
}

CardDefinition make_aen_elle_aristocrat_definition() {
    return generated::make_supported_card_definition(kAenElleAristocratId);
}

CardDefinition make_aen_elle_slave_trader_definition() {
    return generated::make_supported_card_definition(kAenElleSlaveTraderId);
}
CardDefinition make_oberon_king_definition() { return generated::make_supported_card_definition(kOberonKingId); }
CardDefinition make_oberon_invader_definition() { return generated::make_supported_card_definition(kOberonInvaderId); }
CardDefinition make_oberon_conqueror_definition() { return generated::make_supported_card_definition(kOberonConquerorId); }
CardDefinition make_imleriths_wrath_definition() { return generated::make_supported_card_definition(kImlerithsWrathId); }
CardDefinition make_apiarian_phantom_definition() { return generated::make_supported_card_definition(kApiarianPhantomId); }
CardDefinition make_eredin_breacc_glas_definition() { return generated::make_supported_card_definition(kEredinBreaccGlasId); }
CardDefinition make_winter_queen_definition() { return generated::make_supported_card_definition(kWinterQueenId); }
CardDefinition make_red_riders_definition() { return generated::make_supported_card_definition(kRedRidersId); }
CardDefinition make_ancient_foglet_definition() { return generated::make_supported_card_definition(kAncientFogletId); }
CardDefinition make_caranthir_golden_child_definition() { return generated::make_supported_card_definition(kCaranthirGoldenChildId); }
CardDefinition make_tir_na_lia_definition() { return generated::make_supported_card_definition(kTirNaLiaId); }
CardDefinition make_ard_gaeth_definition() { return generated::make_supported_card_definition(kArdGaethId); }

std::vector<CardDefinition> make_all_definitions() {
    return generated::make_supported_card_definitions();
}

CardCatalog make_catalog() {
    CardCatalog catalog;
    for (CardDefinition definition : make_all_definitions()) {
        catalog.add(std::move(definition));
    }
    return catalog;
}

void register_basic_supported_card_effects(EffectRegistry& registry) {

    registry.register_handler(std::string(kBloodScentLeaderEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "腥膻之味：选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "blood_scent requires one enemy unit target");
            }
            return;
        }
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 3, call.source_entity_id));

        const RuntimeCard* leader = context.state.find_card(call.source_entity_id);
        if (leader != nullptr && leader->state.order_charges == 0) {
            const auto destination = context.random_open_battle_row(call.actor_id);
            if (destination.has_value()) {
                context.enqueue_spawn_card(call.actor_id, make_ekimmara_definition(), destination.value(), call.source_entity_id);
            }
        }
    });

    registry.register_handler(std::string(kWhiteFrostLeaderEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "白霜降临：选择要移动的敌军单位")) {
            if (!context.state.pending_choice.has_value()) emit_invalid_target(context, call, "white frost requires one enemy unit target");
            return;
        }
        const auto origin = context.state.location_of(call.target.entity_id);
        if (!origin.has_value() || !is_battle_zone(origin->zone)) return;
        const Zone destination = origin->zone == Zone::Melee ? Zone::Ranged : Zone::Melee;
        if (!row_has_space(context.state, origin->side, destination)) {
            add_frost_row_effect(context, call.actor_id, origin->side, origin->zone, 2);
            return;
        }
        const bool moved = primitive_move_card(context.state, call.target.entity_id,
            Location{origin->side, destination, context.state.player(origin->side).row(destination).size()});
        if (!moved) {
            emit_invalid_target(context, call, "white frost could not move target");
            return;
        }
        context.emit(EventRecord{TaskType::MoveCard, "card_moved", call.actor_id, call.source_entity_id, call.target.entity_id, 0, "white_frost"});
        add_frost_row_effect(context, call.actor_id, origin->side, destination, 2);
    });

    registry.register_handler(std::string(kWhiteFrostCardPlayedEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* leader = context.state.find_card(call.source_entity_id);
        const RuntimeCard* played = context.state.find_card(call.event_target_entity_id);
        const auto leader_location = context.state.location_of(call.source_entity_id);
        const auto played_location = context.state.location_of(call.event_target_entity_id);
        if (leader == nullptr || played == nullptr || !leader_location.has_value() || leader_location->zone != Zone::Leader
            || leader_location->side != leader->controller_id || !played_location.has_value()
            || played_location->side != leader->controller_id || call.actor_id != leader->controller_id
            || !is_battle_zone(played_location->zone) || !played->is_unit_card() || !played->has_category(kWildHunt)) return;
        const PlayerId enemy = context.state.opponent_id(leader->controller_id);
        if (row_effect_duration(context.state, enemy, played_location->zone, "frost") > 0) {
            context.enqueue(Task::boost_card(call.actor_id, call.event_target_entity_id, 1, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kRegisDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "雷吉斯：重生：选择 1 个敌军单位汲食 3")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "regis deploy requires one enemy unit target");
            }
            return;
        }
        context.enqueue(Task::drain_card(call.actor_id, call.source_entity_id, call.target.entity_id, 3));
    });

    registry.register_handler(std::string(kRegisTurnEndEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr || call.actor_id != source->owner_id) {
            return;
        }
        const bool active = card_is_in_zone(context.state, call.source_entity_id, source->owner_id, Zone::Hand)
            || card_is_in_zone(context.state, call.source_entity_id, source->owner_id, Zone::Deck);
        if (!active || !enemy_side_has_bleeding_unit(context.state, source->owner_id)) {
            return;
        }
        change_base_power(*source, 1);
        context.emit(EventRecord{TaskType::DispatchTurnEnd, "regis_reborn_base_power_increased", source->owner_id, call.source_entity_id, call.source_entity_id, source->state.base_power, {}});
    });

    registry.register_handler(std::string(kUnseenElderDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "暗影长者：选择 1 个敌军单位重伤 4")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "unseen elder deploy requires one enemy unit target");
            }
            return;
        }
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 4, call.source_entity_id));
    });

    registry.register_handler(std::string(kUnseenElderTurnEndEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (source == nullptr || !source_location.has_value() || !is_battle_zone(source_location->zone)) {
            return;
        }
        if (call.actor_id != source->controller_id) {
            return;
        }

        const PlayerId enemy = context.state.opponent_id(source->controller_id);
        std::vector<EntityId> non_bleeding;
        std::vector<EntityId> devotion_tick_targets;
        for (Zone zone : kBattleZones) {
            for (EntityId target_id : context.state.player(enemy).row(zone)) {
                const RuntimeCard* target = context.state.find_card(target_id);
                if (target == nullptr || !target->is_unit_card()) {
                    continue;
                }
                if (!target->state.immune && target->state.bleeding <= 0) {
                    non_bleeding.push_back(target_id);
                }
                if (target->state.bleeding > 0) {
                    devotion_tick_targets.push_back(target_id);
                }
            }
        }

        // Unseen Elder resolves in card-text order: first give Bleeding (2) to a
        // random enemy without Bleeding, then Devotion triggers Bleeding once.
        // The freshly-bleeding unit must therefore be part of the same trigger.
        if (!non_bleeding.empty()) {
            const EntityId target_id = non_bleeding[context.random_index(non_bleeding.size())];
            const RuntimeCard* selected_target = context.state.find_card(target_id);
            context.enqueue(Task::add_status_card(source->controller_id, target_id, CardStatus::Bleeding, 2, call.source_entity_id));
            context.emit(EventRecord{TaskType::DispatchTurnEnd, "unseen_elder_bleeding_applied", source->controller_id, call.source_entity_id, target_id, 2, "FastRng"});

            // Veil blocks the freshly applied Bleeding. In that case this
            // target must not receive a phantom Devotion Bleeding tick. Keep
            // the random selection intact: selecting a Veiled unit correctly
            // causes the Bleeding application to fizzle instead of rerolling.
            if (selected_target != nullptr && !selected_target->state.veil) {
                devotion_tick_targets.push_back(target_id);
            }
        }

        if (!is_monsters_devotion(context.state, source->owner_id)) {
            return;
        }
        for (EntityId target_id : devotion_tick_targets) {
            // Queue the counter decrement after AddStatusCard so a freshly
            // selected target goes 0 -> 2 -> 1 before its Bleeding damage.
            context.enqueue(Task::modify_card_counter(
                source->controller_id,
                target_id,
                CardCounterField::Bleeding,
                -1,
                CardCounterMode::Add,
                call.source_entity_id
            ));
            context.enqueue(Task::damage_card(source->controller_id, target_id, 1, call.source_entity_id, "bleeding"));
            context.emit(EventRecord{TaskType::DispatchTurnEnd, "unseen_elder_devotion_bleeding_ticked", source->controller_id, call.source_entity_id, target_id, 1, {}});
        }
    });

    registry.register_handler(std::string(kArmorerWorkshopOrderEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = own_hand_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "选择己方手牌中的 1 张单位牌")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "armorer_workshop requires one allied hand unit target");
            }
            return;
        }
        context.enqueue(Task::boost_card(call.actor_id, call.target.entity_id, 3, call.source_entity_id));
        context.enqueue(Task::add_armor_card(call.actor_id, call.target.entity_id, 2, call.source_entity_id));
    });

    registry.register_handler(std::string(kCrystalSkullOrderEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = allied_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "水晶面甲：选择一个友军单位")) {
            if (!context.state.pending_choice.has_value()) emit_invalid_target(context, call, "crystal skull requires one allied unit target");
            return;
        }
        context.enqueue(Task::boost_card(call.actor_id, call.target.entity_id, 4, call.source_entity_id));
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Veil, 1, call.source_entity_id));
    });

    registry.register_handler(std::string(kGiantCentipedeDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr) {
            return;
        }
        const std::size_t hand_count = context.state.player(source->owner_id).hand.size();
        if (hand_count > 0) {
            context.enqueue(Task::add_armor_card(call.actor_id, call.source_entity_id, static_cast<int>(hand_count), call.source_entity_id));
        } else {
            context.enqueue(Task::destroy_card(call.actor_id, call.source_entity_id, "giant_centipede_no_hand_armor", call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kGiantCentipedeArmorBrokenEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (source == nullptr || !source_location.has_value() || !is_battle_zone(source_location->zone)) {
            return;
        }
        if (call.event_target_entity_id != call.source_entity_id || source->state.armor > 0) {
            return;
        }
        context.enqueue(Task::destroy_card(source->controller_id, call.source_entity_id, "giant_centipede_armor_broken", call.source_entity_id));
    });

    registry.register_handler(std::string(kDettlaffDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const PlayerId enemy = context.state.opponent_id(call.actor_id);
        if (call.target.kind == ActionTargetKind::Row && call.target.side == enemy) {
            const int duration = 2 + adjacent_friendly_vampires(context.state, call.source_entity_id);
            add_blood_moon_row_effect(context, call.actor_id, call.target.side, call.target.zone, duration);
            context.emit(EventRecord{TaskType::ResolveDeploy, "blood_moon_row_effect_added", call.actor_id, call.source_entity_id, kInvalidEntityId, duration, std::string("side=") + std::to_string(call.target.side) + ":" + std::string(to_string(call.target.zone))});
            return;
        }

        context.request_row_choice(
            call.actor_id,
            call.source_entity_id,
            call.effect_id,
            "狄拉夫：选择对方一排生成血月",
            {Location{enemy, Zone::Melee, 0}, Location{enemy, Zone::Ranged, 0}}
        );
    });

    registry.register_handler(std::string(kDettlaffOrderEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetContext target_context{call.actor_id, call.source_entity_id, call.target};
        const TargetSelector selector = enemy_unit_selector();
        auto is_bleeding_enemy_target = [&](EntityId target_id) {
            const RuntimeCard* target = context.state.find_card(target_id);
            return target != nullptr
                && target->state.bleeding > 0
                && action_card_target_is_valid(context.state, TargetContext{call.actor_id, call.source_entity_id, ActionTarget::card(target_id)}, selector);
        };
        if (call.target.kind == ActionTargetKind::Card) {
            if (!is_bleeding_enemy_target(call.target.entity_id)) {
                emit_invalid_target(context, call, "dettlaff order target must be a bleeding enemy unit");
                return;
            }
            context.enqueue(Task::damage_card(call.actor_id, call.target.entity_id, 1, call.source_entity_id));
            return;
        }
        std::vector<EntityId> legal_targets = select_card_targets(context.state, target_context, selector);
        legal_targets.erase(std::remove_if(legal_targets.begin(), legal_targets.end(), [&](EntityId target_id) {
            const RuntimeCard* target = context.state.find_card(target_id);
            return target == nullptr || target->state.bleeding <= 0;
        }), legal_targets.end());
        if (legal_targets.empty()) {
            emit_invalid_target(context, call, "dettlaff order requires one bleeding enemy unit target");
            return;
        }
        context.request_card_choice(call.actor_id, call.source_entity_id, call.effect_id, "狄拉夫：选择 1 个重伤敌军单位", std::move(legal_targets));
    });

    registry.register_handler(std::string(kDettlaffDeathblowEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr || call.event_source_entity_id != call.source_entity_id) {
            return;
        }
        const auto destination = same_row_end_location(context.state, call.source_entity_id);
        if (destination.has_value()) {
            context.enqueue_spawn_card(source->controller_id, make_ekimmara_definition(), destination.value(), call.source_entity_id);
        }
    });

    registry.register_handler(std::string(kLordRiptideDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (!source_location.has_value() || source_location->zone != Zone::Melee) {
            return;
        }
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto targets = strongest_enemy_units(context.state, call.actor_id);
        if (source == nullptr || targets.empty()) {
            return;
        }
        const EntityId target_id = targets[context.random_index(targets.size())];
        const RuntimeCard* target = context.state.find_card(target_id);
        if (target == nullptr) {
            return;
        }
        const int source_power = std::max(0, source->state.power);
        const int target_power = std::max(0, target->state.power);
        if (source_power > 0) {
            context.enqueue(Task::damage_card(call.actor_id, target_id, source_power, call.source_entity_id));
        }
        if (target_power > 0) {
            context.enqueue(Task::damage_card(call.actor_id, call.source_entity_id, target_power, target_id));
        }
    });

    registry.register_handler(std::string(kLordRiptideHandEndEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr || call.actor_id != source->owner_id) {
            return;
        }
        if (card_is_in_zone(context.state, call.source_entity_id, source->owner_id, Zone::Hand)
            && has_might(context.state, source->owner_id)) {
            context.enqueue(Task::add_armor_card(call.actor_id, call.source_entity_id, 1, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kKatakanDeployEffect), [](KernelContext& context, const EffectCall& call) {
        install_board_listener(context, call, "status_added", std::string(kKatakanBleedingAddedEffect), {{"status", "bleeding"}});
    });

    registry.register_handler(std::string(kKatakanOrderEffect), [](KernelContext& context, const EffectCall& call) {
        const auto destination = same_row_end_location(context.state, call.source_entity_id);
        if (!destination.has_value()) {
            context.emit(EventRecord{TaskType::SpawnCard, "supported.katakan:invalid_source_row", call.actor_id, call.source_entity_id, kInvalidEntityId, 0, {}});
            return;
        }
        context.enqueue_spawn_card(call.actor_id, make_ekimmara_definition(), destination.value(), call.source_entity_id);
    });

    registry.register_handler(std::string(kKatakanBleedingAddedEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const RuntimeCard* target = context.state.find_card(call.event_target_entity_id);
        if (source == nullptr || target == nullptr || target->controller_id != context.state.opponent_id(source->controller_id)) {
            return;
        }
        // Card-specific text: every successful enemy Bleeding application reduces
        // Katakan cooldown by 1, even if that enemy was already bleeding.  A
        // locked Katakan has no active text.
        if (source->state.locked) {
            return;
        }
        if (source->state.cooldown > 0) {
            const int cooldown = primitive_modify_card_counter(
                context.state, call.source_entity_id, CardCounterField::Cooldown, -1
            ).value_or(source->state.cooldown);
            context.emit(EventRecord{TaskType::ResolveOrder, "katakan_cooldown_reduced", source->controller_id, call.source_entity_id, call.event_target_entity_id, cooldown, {}});
        }
    });

    registry.register_handler(std::string(kVerenaDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "薇瑞娜：选择 1 个敌军单位重伤 4")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "verena requires one enemy unit target");
            }
            return;
        }
        install_board_listener(context, call, "status_added", std::string(kVerenaBleedingAddedEffect), {{"status", "bleeding"}});
        refresh_verena_boost_blocks(context.state, call.source_entity_id);
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 4, call.source_entity_id));
    });

    registry.register_handler(std::string(kVerenaBleedingAddedEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        RuntimeCard* target = context.state.find_card(call.event_target_entity_id);
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (source == nullptr || target == nullptr || !source_location.has_value() || !is_battle_zone(source_location->zone)) {
            return;
        }
        if (target->controller_id == context.state.opponent_id(source->controller_id)) {
            attach_verena_boost_block(context.state, call.source_entity_id, call.event_target_entity_id);
            context.emit(EventRecord{TaskType::AddStatusCard, "verena_boost_block_attached", source->controller_id, call.source_entity_id, call.event_target_entity_id, 0, {}});
        }
    });

    registry.register_handler(std::string(kNaglfarSpecialEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        std::vector<EntityId> candidates;
        if (source != nullptr) {
            const auto it = source->memory.find("naglfar_revealed_choices");
            if (it != source->memory.end()) {
                candidates = decode_entity_id_list(it->second);
            }
        }
        if (candidates.empty()) {
            candidates = random_gold_cards_in_deck(context, call.actor_id, 2);
        }
        if (candidates.empty()) {
            context.emit(EventRecord{TaskType::ResolveSpecial, "naglfar:no_gold_cards", call.actor_id, call.source_entity_id, kInvalidEntityId, 0, {}});
            return;
        }

        if (call.target.kind == ActionTargetKind::Card) {
            if (!entity_in_list(call.target.entity_id, candidates)) {
                emit_invalid_target(context, call, "naglfar target must be one of the revealed gold deck cards");
                return;
            }
            if (source != nullptr) {
                source->memory.erase("naglfar_revealed_choices");
            }
            for (EntityId candidate_id : candidates) {
                if (candidate_id != call.target.entity_id && context.state.location_of(candidate_id).has_value()) {
                    const auto location = context.state.location_of(candidate_id).value();
                    if (location.side == call.actor_id && location.zone == Zone::Deck) {
                        primitive_move_card(context.state, candidate_id, Location{call.actor_id, Zone::Deck, 0});
                        context.emit(EventRecord{TaskType::MoveCard, "naglfar_other_gold_to_deck_top", call.actor_id, call.source_entity_id, candidate_id, 0, {}});
                    }
                }
            }
            finish_source_special_now(context, call.actor_id, call.source_entity_id);
            enqueue_play_from_staging(context, call.actor_id, call.target.entity_id);
            return;
        }

        if (source != nullptr) {
            source->memory["naglfar_revealed_choices"] = encode_entity_id_list(candidates);
        }
        context.request_card_choice(call.actor_id, call.source_entity_id, call.effect_id, "纳吉尔法：选择 1 张展示的金色牌打出", candidates);
    });

    registry.register_handler(std::string(kGeelsDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const bool devotion = is_monsters_devotion(context.state, call.actor_id);
        const std::vector<EntityId> candidates = wild_hunt_play_candidates(context.state, call.actor_id, devotion);
        if (candidates.empty()) {
            context.emit(EventRecord{TaskType::ResolveDeploy, "geels:no_wild_hunt_card", call.actor_id, call.source_entity_id, kInvalidEntityId, 0, devotion ? "devotion" : "non_devotion"});
            return;
        }

        if (call.target.kind == ActionTargetKind::Card) {
            if (!entity_in_list(call.target.entity_id, candidates)) {
                emit_invalid_target(context, call, "geels target must be an eligible Wild Hunt deck card");
                return;
            }
            enqueue_play_from_staging(context, call.actor_id, call.target.entity_id);
            return;
        }

        context.request_card_choice(call.actor_id, call.source_entity_id, call.effect_id, devotion ? "盖尔：选择 1 张狂猎牌打出" : "盖尔：选择 1 张狂猎特殊牌打出", candidates);
    });

    registry.register_handler(std::string(kImlerithDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (!source_location.has_value() || source_location->zone != Zone::Melee) {
            return;
        }

        if (call.target.kind == ActionTargetKind::Card) {
            RuntimeCard* discarded = context.state.find_card(call.target.entity_id);
            const int boost_amount = (discarded != nullptr && discarded->is_unit_card()) ? std::max(0, discarded->state.power) : 0;
            context.enqueue(Task::discard_card(call.actor_id, call.target.entity_id, call.source_entity_id));
            if (boost_amount > 0) {
                context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, boost_amount, call.source_entity_id));
            }
            return;
        }

        draw_one_to_hand(context.state, call.actor_id);
        TargetSelector selector;
        selector.side = RelativeSide::Actor;
        selector.zone_scope = ZoneScope::Hand;
        selector.card_kind = TargetCardKind::Any;
        // Pending choices must expose every legal discard candidate.
        // max_targets is only for deterministic auto-selection effects, not for
        // player-facing card choices.
        selector.max_targets = 0;
        const TargetContext target_context{call.actor_id, call.source_entity_id, ActionTarget::none()};
        std::vector<EntityId> legal_targets = select_card_targets(context.state, target_context, selector);
        if (!legal_targets.empty()) {
            context.request_card_choice(call.actor_id, call.source_entity_id, call.effect_id, "伊勒瑞斯：选择 1 张手牌丢弃", std::move(legal_targets));
        }
    });

    registry.register_handler(std::string(kOzzrelDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (!source_location.has_value()) {
            return;
        }
        TargetSelector selector;
        if (source_location->zone == Zone::Melee) {
            selector = enemy_cemetery_unit_selector();
        } else if (source_location->zone == Zone::Ranged) {
            selector = own_cemetery_unit_selector();
        } else {
            return;
        }
        if (!require_or_request_card_target(context, call, selector, "欧兹瑞尔：选择墓场单位吞噬")) {
            return;
        }
        context.enqueue(Task::consume_card(call.actor_id, call.source_entity_id, call.target.entity_id));
    });

    registry.register_handler(std::string(kQueenDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (!source_location.has_value()) {
            return;
        }
        if (source_location->zone == Zone::Melee) {
            const TargetSelector selector = enemy_unit_selector();
            if (!require_or_request_card_target(context, call, selector, "夜之女王：选择 1 个敌军单位重伤 3")) {
                if (!context.state.pending_choice.has_value()) {
                    emit_invalid_target(context, call, "queen melee requires one enemy unit target");
                }
                return;
            }
            context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 3, call.source_entity_id));
        } else if (source_location->zone == Zone::Ranged) {
            TargetSelector selector = battlefield_unit_selector();
            selector.card_kind = TargetCardKind::Any;
            selector.exclude_source = true;
            if (!require_or_request_card_target(context, call, selector, "夜之女王：选择 1 个战场单位净化")) {
                if (!context.state.pending_choice.has_value()) {
                    emit_invalid_target(context, call, "queen ranged requires one battlefield unit target");
                }
                return;
            }
            context.enqueue(Task::purify_card(call.actor_id, call.target.entity_id, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kFlederDeployEffect), [](KernelContext& context, const EffectCall& call) {
        install_board_listener(context, call, "status_added", std::string(kFlederBleedingAddedEffect), {{"status", "bleeding"}});
        install_board_listener(context, call, "turn_start", std::string(kFlederTurnStartEffect));
    });

    registry.register_handler(std::string(kFlederBleedingAddedEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const RuntimeCard* target = context.state.find_card(call.event_target_entity_id);
        if (source == nullptr || target == nullptr) {
            return;
        }
        if (target->controller_id != context.state.opponent_id(source->controller_id) || source->state.countdown <= 0 || call.amount <= 0) {
            return;
        }
        (void)primitive_modify_card_counter(
            context.state, call.source_entity_id, CardCounterField::Countdown, -1
        );
        context.enqueue(Task::boost_card(source->controller_id, call.source_entity_id, call.amount, call.source_entity_id));
    });

    registry.register_handler(std::string(kFlederTurnStartEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source != nullptr && call.actor_id == source->controller_id) {
            const int countdown = primitive_modify_card_counter(
                context.state, call.source_entity_id, CardCounterField::Countdown, 1, CardCounterMode::Set
            ).value_or(1);
            context.emit(EventRecord{TaskType::DispatchRoundStart, "fleder_countdown_refreshed", call.actor_id, call.source_entity_id, call.source_entity_id, countdown, {}});
        }
    });

    registry.register_handler(std::string(kGarkainDeployEffect), [](KernelContext& context, const EffectCall& call) {
        install_board_listener(context, call, "card_played", std::string(kGarkainVampirePlayedEffect));
    });

    registry.register_handler(std::string(kGarkainOrderEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "garkain requires one enemy unit target");
            }
            return;
        }
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 2, call.source_entity_id));
    });

    registry.register_handler(std::string(kGarkainVampirePlayedEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const RuntimeCard* played = context.state.find_card(call.event_source_entity_id);
        if (source == nullptr || played == nullptr) {
            return;
        }
        if (call.actor_id != source->controller_id || call.event_source_entity_id == call.source_entity_id || !played->has_category(kVampire)) {
            return;
        }
        if (source->state.cooldown > 0) {
            const int cooldown = primitive_modify_card_counter(
                context.state, call.source_entity_id, CardCounterField::Cooldown, -1
            ).value_or(source->state.cooldown);
            context.emit(EventRecord{TaskType::ResolveOrder, "garkain_cooldown_reduced", source->controller_id, call.source_entity_id, call.event_source_entity_id, cooldown, {}});
        }
    });

    registry.register_handler(std::string(kBruxaDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "吸血鬼女：选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "bruxa deploy requires one enemy unit target");
            }
            return;
        }
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 3, call.source_entity_id));
    });

    registry.register_handler(std::string(kBruxaOrderEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "吸血鬼女指令：选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "bruxa order requires one enemy unit target");
            }
            return;
        }
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 3, call.source_entity_id));
    });

    registry.register_handler(std::string(kFeastOfBloodSpecialEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "猩红盛宴：选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "feast of blood requires one enemy unit target");
            }
            return;
        }

        context.enqueue(Task::purify_card(call.actor_id, call.target.entity_id, call.source_entity_id));
        context.enqueue(Task::damage_card(call.actor_id, call.target.entity_id, 3, call.source_entity_id));

        bool has_friendly_vampire = false;
        for (Zone zone : kBattleZones) {
            for (EntityId entity_id : context.state.player(call.actor_id).row(zone)) {
                const RuntimeCard* card = context.state.find_card(entity_id);
                if (card != nullptr && card->controller_id == call.actor_id && card->has_category(kVampire)) {
                    has_friendly_vampire = true;
                    break;
                }
            }
            if (has_friendly_vampire) {
                break;
            }
        }
        if (has_friendly_vampire) {
            context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, 3, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kAenElleConquerorDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr) {
            return;
        }
        if (!is_monsters_devotion(context.state, source->owner_id)) {
            context.enqueue(Task::destroy_card(call.actor_id, call.source_entity_id, "conqueror_non_devotion", call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kPredatorDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "渴血鸟怪：选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "bloodscented predator requires one enemy unit target");
            }
            return;
        }
        const RuntimeCard* target = context.state.find_card(call.target.entity_id);
        const int amount = (target != nullptr && has_bonded_same_name_on_side(context.state, call.source_entity_id))
            ? std::max(0, target->state.base_power)
            : 2;
        context.enqueue(Task::add_status_card(call.actor_id, call.target.entity_id, CardStatus::Bleeding, amount, call.source_entity_id));
    });

    registry.register_handler(std::string(kRiderDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (source == nullptr || !source_location.has_value() || !has_dominance(context.state, source->controller_id)) {
            return;
        }
        const std::vector<EntityId> deck_snapshot = context.state.player(source->owner_id).deck;
        std::size_t insertion_index = context.state.player(source_location->side).row(source_location->zone).size();
        for (EntityId entity_id : deck_snapshot) {
            const RuntimeCard* candidate = context.state.find_card(entity_id);
            if (candidate == nullptr || candidate->definition->id != source->definition->id) {
                continue;
            }
            const Location destination{source_location->side, source_location->zone, insertion_index++};
            context.enqueue(Task::move_card(source->controller_id, entity_id, destination, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kWildHuntNavigatorDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = allied_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "狂猎导航员：选择 1 个友军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "wild hunt navigator requires one allied unit target");
            }
            return;
        }

        const auto target_location = context.state.location_of(call.target.entity_id);
        if (!target_location.has_value() || target_location->side != call.actor_id || !is_battle_zone(target_location->zone)) {
            emit_invalid_target(context, call, "wild hunt navigator target must remain on an allied battle row");
            return;
        }
        const PlayerId enemy = context.state.opponent_id(call.actor_id);
        int amount = row_effect_duration(context.state, enemy, target_location->zone, "frost");
        if (has_dominance(context.state, call.actor_id)) {
            amount = row_effect_duration(context.state, enemy, Zone::Melee, "frost")
                + row_effect_duration(context.state, enemy, Zone::Ranged, "frost");
        }
        if (amount > 0) {
            context.enqueue(Task::boost_card(call.actor_id, call.target.entity_id, amount, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kWildHuntWarriorDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "狂猎战士：选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "wild hunt warrior requires one enemy unit target");
            }
            return;
        }

        const RuntimeCard* target = context.state.find_card(call.target.entity_id);
        const auto target_location = context.state.location_of(call.target.entity_id);
        if (target == nullptr || !target_location.has_value() || !is_battle_zone(target_location->zone)) {
            emit_invalid_target(context, call, "wild hunt warrior target must remain on a battle row");
            return;
        }
        if (has_dominance(context.state, call.actor_id)) {
            add_frost_row_effect(context, call.actor_id, target_location->side, target_location->zone, 1);
            context.emit(EventRecord{TaskType::ResolveDeploy, "frost_row_effect_added", call.actor_id, call.source_entity_id, kInvalidEntityId, 1, std::string("side=") + std::to_string(target_location->side) + ":" + std::string(to_string(target_location->zone))});
        }
        context.enqueue(Task::damage_card(call.actor_id, call.target.entity_id, 2, call.source_entity_id));
    });

    registry.register_handler(std::string(kWildHuntHoundTurnEndEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto location = context.state.location_of(call.source_entity_id);
        if (source == nullptr || !location.has_value() || !is_battle_zone(location->zone)
            || call.actor_id != source->controller_id || !has_dominance(context.state, source->controller_id)) {
            return;
        }
        context.enqueue(Task::boost_card(source->controller_id, call.source_entity_id, 1, call.source_entity_id));
    });

    registry.register_handler(std::string(kWildHuntBruiserDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "狂猎碾压者：选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "wild hunt bruiser requires one enemy unit target");
            }
            return;
        }

        const auto origin = context.state.location_of(call.target.entity_id);
        if (!origin.has_value() || !is_battle_zone(origin->zone)) {
            emit_invalid_target(context, call, "wild hunt bruiser target must be on an enemy battle row");
            return;
        }
        const Zone destination_zone = origin->zone == Zone::Melee ? Zone::Ranged : Zone::Melee;
        const Location destination{origin->side, destination_zone, context.state.player(origin->side).row(destination_zone).size()};
        const bool moved = primitive_move_card(context.state, call.target.entity_id, destination);
        if (moved) {
            context.emit(EventRecord{TaskType::MoveCard, "card_moved", call.actor_id, call.source_entity_id, call.target.entity_id, 0, "wild_hunt_bruiser"});
        } else {
            context.emit(EventRecord{TaskType::MoveCard, "card_move_blocked", call.actor_id, call.source_entity_id, call.target.entity_id, 0, "wild_hunt_bruiser_destination_full"});
        }
        if (moved && row_effect_duration(context.state, origin->side, destination_zone, "frost") > 0) {
            context.enqueue(Task::damage_card(call.actor_id, call.target.entity_id, 2, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kNaglfarCrewDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const PlayerId enemy = context.state.opponent_id(call.actor_id);
        if (call.target.kind == ActionTargetKind::Row && call.target.side == enemy && is_battle_zone(call.target.zone)) {
            add_frost_row_effect(context, call.actor_id, enemy, call.target.zone, 2);
            context.emit(EventRecord{TaskType::ResolveDeploy, "frost_row_effect_added", call.actor_id, call.source_entity_id, kInvalidEntityId, 2, std::string("side=") + std::to_string(enemy) + ":" + std::string(to_string(call.target.zone))});
            return;
        }
        context.request_row_choice(
            call.actor_id,
            call.source_entity_id,
            call.effect_id,
            "纳吉尔法船员：选择敌方一排生成霜",
            {Location{enemy, Zone::Melee, 0}, Location{enemy, Zone::Ranged, 0}}
        );
    });

    registry.register_handler(std::string(kNaglfarCrewTurnEndEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (source == nullptr || !source_location.has_value() || !is_battle_zone(source_location->zone)
            || call.actor_id != source->controller_id) {
            return;
        }
        const PlayerId enemy = context.state.opponent_id(source->controller_id);
        if (row_effect_duration(context.state, enemy, source_location->zone, "frost") > 0) {
            context.enqueue(Task::boost_card(source->controller_id, call.source_entity_id, 1, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kNaglfarTaskmasterDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = has_dominance(context.state, call.actor_id)
            ? battlefield_unit_selector()
            : enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "纳吉尔法工头：选择 1 个单位净化")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "naglfar taskmaster requires one valid battlefield unit target");
            }
            return;
        }
        context.enqueue(Task::purify_card(call.actor_id, call.target.entity_id, call.source_entity_id));
    });

    registry.register_handler(std::string(kAenElleAristocratOrderEffect), [](KernelContext& context, const EffectCall& call) {
        if (!has_dominance(context.state, call.actor_id)) {
            return;
        }
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "艾恩·艾尔贵族：选择 1 个敌军单位")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "aen elle aristocrat requires one enemy unit target while dominant");
            }
            return;
        }
        const auto origin = context.state.location_of(call.target.entity_id);
        if (!origin.has_value() || !is_battle_zone(origin->zone)) {
            emit_invalid_target(context, call, "aen elle aristocrat target must be on an enemy battle row");
            return;
        }
        const Zone destination_zone = origin->zone == Zone::Melee ? Zone::Ranged : Zone::Melee;
        const Location destination{origin->side, destination_zone, context.state.player(origin->side).row(destination_zone).size()};
        if (primitive_move_card(context.state, call.target.entity_id, destination)) {
            context.emit(EventRecord{TaskType::MoveCard, "card_moved", call.actor_id, call.source_entity_id, call.target.entity_id, 0, "aen_elle_aristocrat"});
        } else {
            // A full destination row does not invalidate the selected target.
            // The Order is still spent; only the movement component fails.
            context.emit(EventRecord{TaskType::MoveCard, "card_move_blocked", call.actor_id, call.source_entity_id, call.target.entity_id, 0, "aen_elle_aristocrat_destination_full"});
        }
    });

    registry.register_handler(std::string(kAenElleAristocratTurnEndEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (source == nullptr || !source_location.has_value() || !is_battle_zone(source_location->zone)
            || call.actor_id != source->controller_id) {
            return;
        }
        const PlayerId enemy = context.state.opponent_id(source->controller_id);
        const auto frost_rows = context.state.turn_contexts[static_cast<std::size_t>(source->controller_id)].opponent_rows_frosted_this_turn;
        for (Zone zone : kBattleZones) {
            if (frost_rows[row_index(zone)]) {
                add_frost_row_effect(context, source->controller_id, enemy, zone, 1);
            }
        }
    });

    registry.register_handler(std::string(kAenElleSlaveTraderDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const PlayerId enemy = context.state.opponent_id(call.actor_id);
        const int vitality = row_effect_duration(context.state, enemy, Zone::Melee, "frost")
            + row_effect_duration(context.state, enemy, Zone::Ranged, "frost");
        if (vitality > 0 && call.target.kind != ActionTargetKind::Card) {
            context.enqueue(Task::add_status_card(call.actor_id, call.source_entity_id, CardStatus::Vitality, vitality, call.source_entity_id));
        }

        TargetSelector selector = enemy_unit_selector();
        selector.require_not_veiled = true;
        if (!require_or_request_card_target(context, call, selector, "艾恩·艾尔奴隶商：选择 1 个敌军单位灌注")) {
            if (!context.state.pending_choice.has_value()) {
                emit_invalid_target(context, call, "aen elle slave trader requires one non-Veil enemy unit infusion target");
            }
            return;
        }

        const EntityId target_id = call.target.entity_id;
        const bool duplicate = std::any_of(context.state.listeners.begin(), context.state.listeners.end(), [&](const auto& entry) {
            const RuntimeListener& listener = entry.second;
            return listener.origin == RuntimeListenerOrigin::Infusion
                && listener.source_id == std::optional<EntityId>{target_id}
                && listener.handler == kAenElleSlaveTraderInfusionEffect;
        });
        if (!duplicate) {
            RuntimeListener listener;
            listener.event = "turn_end";
            listener.handler = std::string(kAenElleSlaveTraderInfusionEffect);
            listener.source_id = target_id;
            listener.once = false;
            listener.require_source = true;
            listener.source_must_be_on_board = true;
            listener.disabled_by_lock = true;
            listener.origin = RuntimeListenerOrigin::Infusion;
            context.add_listener(std::move(listener));
        }
    });

    registry.register_handler(std::string(kAenElleSlaveTraderInfusionEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* target = context.state.find_card(call.source_entity_id);
        const auto target_location = context.state.location_of(call.source_entity_id);
        if (target == nullptr || !target_location.has_value() || !is_battle_zone(target_location->zone)
            || call.actor_id != target->controller_id) {
            return;
        }
        const PlayerId enemy = context.state.opponent_id(target->controller_id);
        bool stronger_trader = false;
        for (Zone zone : kBattleZones) {
            for (EntityId entity_id : context.state.player(enemy).row(zone)) {
                const RuntimeCard* candidate = context.state.find_card(entity_id);
                if (candidate != nullptr && candidate->definition->id == kAenElleSlaveTraderId
                    && candidate->state.power > target->state.power) {
                    stronger_trader = true;
                    break;
                }
            }
            if (stronger_trader) break;
        }
        if (stronger_trader) {
            context.enqueue(Task::set_power(target->controller_id, call.source_entity_id, 1, call.source_entity_id));
            context.enqueue(Task::add_status_card(target->controller_id, call.source_entity_id, CardStatus::Locked, 1, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kImlerithsWrathSpecialEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "伊勒瑞斯之怒：选择一个敌军单位")) {
            if (!context.state.pending_choice.has_value()) emit_invalid_target(context, call, "imlerith's wrath requires one enemy unit target");
            return;
        }
        const auto target_location = context.state.location_of(call.target.entity_id);
        if (!target_location.has_value() || !is_battle_zone(target_location->zone)) return;
        if (row_effect_duration(context.state, target_location->side, target_location->zone, "frost") > 0) {
            context.enqueue(Task::destroy_card(call.actor_id, call.target.entity_id, "imleriths_wrath_frost", call.source_entity_id));
            return;
        }
        int strongest = 0;
        for (Zone zone : kBattleZones) {
            for (EntityId ally_id : context.state.player(call.actor_id).row(zone)) {
                const RuntimeCard* ally = context.state.find_card(ally_id);
                if (ally != nullptr && ally->has_power()) strongest = std::max(strongest, ally->state.power);
            }
        }
        context.enqueue(Task::damage_card(call.actor_id, call.target.entity_id, strongest, call.source_entity_id));
    });

    registry.register_handler(std::string(kApiarianPhantomOrderEffect), [](KernelContext& context, const EffectCall& call) {
        const TargetSelector selector = enemy_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "蜜蜂幽灵：选择一个敌军单位")) {
            if (!context.state.pending_choice.has_value()) emit_invalid_target(context, call, "apiarian phantom requires one enemy unit target");
            return;
        }
        context.enqueue(Task::damage_card(call.actor_id, call.target.entity_id, 3, call.source_entity_id));
    });

    registry.register_handler(std::string(kApiarianPhantomTurnEndEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto source_location = context.state.location_of(call.source_entity_id);
        if (source != nullptr && source_location.has_value() && is_battle_zone(source_location->zone)
            && call.actor_id == source->controller_id && !source->runtime.order_used) {
            context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, 1, call.source_entity_id));
        }
    });

    registry.register_handler(std::string(kEredinDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const PlayerId enemy = context.state.opponent_id(call.actor_id);

        // A unit deploy initially receives the row where the unit itself was
        // inserted.  Eredin's effect targets an *enemy* row, so treating any Row
        // target as the frost target incorrectly applies frost to his own row and
        // skips the intended follow-up choice.  Only an explicitly selected enemy
        // battle row resolves the effect; otherwise request that choice.
        if (call.target.kind == ActionTargetKind::Row
            && call.target.side == enemy
            && is_battle_zone(call.target.zone)) {
            add_frost_row_effect(context, call.actor_id, enemy, call.target.zone, 2);
            context.emit(EventRecord{
                TaskType::ResolveDeploy,
                "frost_row_effect_added",
                call.actor_id,
                call.source_entity_id,
                kInvalidEntityId,
                2,
                std::string("side=") + std::to_string(enemy) + ":" + std::string(to_string(call.target.zone))
            });
            return;
        }

        context.request_row_choice(
            call.actor_id,
            call.source_entity_id,
            call.effect_id,
            "艾瑞汀：选择敌方一排生成霜",
            {{enemy, Zone::Melee, 0}, {enemy, Zone::Ranged, 0}}
        );
    });

    registry.register_handler(std::string(kAncientFogletDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const PlayerId enemy = context.state.opponent_id(call.actor_id);
        int total = 0;
        for (Zone zone : kBattleZones) {
            for (const RowEffect& effect : context.state.player(enemy).effects_on_row(zone)) {
                total += std::max(0, row_effect_int(effect, "duration"));
            }
        }
        if (total > 0) context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, total, call.source_entity_id));
    });

    registry.register_handler(std::string(kWinterQueenTurnEndEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* queen = context.state.find_card(call.source_entity_id);
        const auto location = context.state.location_of(call.source_entity_id);
        if (queen == nullptr || !location.has_value() || location->zone != Zone::Deck
            || call.actor_id != queen->owner_id || !row_has_space(context.state, queen->owner_id, Zone::Ranged)) return;
        const PlayerId enemy = context.state.opponent_id(queen->owner_id);
        if (row_effect_duration(context.state, enemy, Zone::Melee, "frost") <= 0
            || row_effect_duration(context.state, enemy, Zone::Ranged, "frost") <= 0) return;
        primitive_move_card(context.state, call.source_entity_id,
            Location{queen->owner_id, Zone::Ranged, context.state.player(queen->owner_id).row(Zone::Ranged).size()});
        context.emit(EventRecord{TaskType::MoveCard, "winter_queen_summoned", queen->owner_id,
            call.source_entity_id, call.source_entity_id, 0, {}});
    });

    registry.register_handler(std::string(kWinterQueenRoundEndEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* queen = context.state.find_card(call.source_entity_id);
        const auto location = context.state.location_of(call.source_entity_id);
        if (queen == nullptr || !location.has_value() || !is_battle_zone(location->zone)
            || !is_monsters_devotion(context.state, queen->owner_id)) return;
        const PlayerId enemy = context.state.opponent_id(queen->owner_id);
        const int amount = 2 * (row_effect_duration(context.state, enemy, Zone::Melee, "frost")
            + row_effect_duration(context.state, enemy, Zone::Ranged, "frost"));
        if (amount > 0) context.enqueue(Task::boost_card(queen->owner_id, call.source_entity_id, amount, call.source_entity_id));
    });

    registry.register_handler(std::string(kRedRidersSpecialEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr || context.state.card_catalog == nullptr) return;
        const auto long_id = context.state.card_catalog->id_of(kRedRidersLongFrostId);
        const auto replay_id = context.state.card_catalog->id_of(kRedRidersReplayId);
        const auto both_id = context.state.card_catalog->id_of(kRedRidersBothRowsId);
        if (!long_id || !replay_id || !both_id) return;

        if (call.target.kind == ActionTargetKind::None) {
            context.request_card_definition_choice(call.actor_id, call.source_entity_id, call.effect_id,
                "红骑士：择一", {*long_id, *replay_id, *both_id});
            return;
        }
        const PlayerId enemy = context.state.opponent_id(call.actor_id);
        if (call.target.kind == ActionTargetKind::CardDefinition) {
            if (call.target.definition_id == *both_id) {
                add_frost_row_effect(context, call.actor_id, enemy, Zone::Melee, 2);
                add_frost_row_effect(context, call.actor_id, enemy, Zone::Ranged, 2);
                return;
            }
            source->memory["red_riders_mode"] = call.target.definition_id == *replay_id ? "replay" : "long";
            context.request_row_choice(call.actor_id, call.source_entity_id, call.effect_id,
                "红骑士：选择敌方一排", {{enemy, Zone::Melee, 0}, {enemy, Zone::Ranged, 0}});
            return;
        }
        if (call.target.kind == ActionTargetKind::Row) {
            const bool replay = source->memory["red_riders_mode"] == "replay";
            add_frost_row_effect(context, call.actor_id, enemy, call.target.zone, replay ? 2 : 4);
            if (!replay) return;
            source->memory["red_riders_row"] = call.target.zone == Zone::Melee ? "melee" : "ranged";
            std::vector<EntityId> candidates;
            for (Zone zone : kBattleZones) for (EntityId id : context.state.player(call.actor_id).row(zone)) {
                const RuntimeCard* card = context.state.find_card(id);
                if (card != nullptr && card->definition->card_type == CardType::Unit
                    && card->definition->color == "Bronze" && card->has_category(kWildHunt)) candidates.push_back(id);
            }
            if (!candidates.empty()) context.request_card_choice(call.actor_id, call.source_entity_id, call.effect_id,
                "红骑士：选择重新打出的铜色狂猎单位", std::move(candidates));
            return;
        }
        if (call.target.kind == ActionTargetKind::Card) {
            RuntimeCard* target = context.state.find_card(call.target.entity_id);
            const auto target_location = context.state.location_of(call.target.entity_id);
            if (target == nullptr || !target_location.has_value() || !is_battle_zone(target_location->zone)
                || target->controller_id != call.actor_id || !target->has_category(kWildHunt)
                || target->definition->color != "Bronze") {
                return;
            }

            // Replay semantics are intentionally different from a plain Move:
            // the unit leaves the battlefield first. A Doomed unit is banished
            // at that point and therefore cannot be replayed. Otherwise it is
            // reset to its printed state before entering the ordinary
            // row -> insert-position -> Deploy play chain again.
            if (target->state.doomed) {
                const PlayerId owner = target->owner_id;
                if (primitive_move_card(
                        context.state,
                        call.target.entity_id,
                        Location{owner, Zone::Banished, context.state.player(owner).banished.size()}
                    )) {
                    context.emit(EventRecord{
                        TaskType::MoveCard,
                        "red_riders_replay_doomed_banished",
                        call.actor_id,
                        call.source_entity_id,
                        call.target.entity_id,
                        0,
                        {}
                    });
                }
                return;
            }

            if (!primitive_move_card(
                    context.state,
                    call.target.entity_id,
                    Location{call.actor_id, Zone::Stay, context.state.player(call.actor_id).stay.size()}
                )) {
                return;
            }
            if (!primitive_reset_card(context.state, call.target.entity_id)) {
                return;
            }
            context.emit(EventRecord{
                TaskType::MoveCard,
                "red_riders_replay_reset",
                call.actor_id,
                call.source_entity_id,
                call.target.entity_id,
                0,
                {}
            });
            enqueue_play_from_staging(context, call.actor_id, call.target.entity_id);
        }
    });

    registry.register_handler(std::string(kCaranthirGoldenChildTurnStartEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto location = context.state.location_of(call.source_entity_id);
        if (source == nullptr || !location.has_value() || call.actor_id != source->owner_id
            || (location->zone != Zone::Hand && location->zone != Zone::Deck)) return;
        source->state.power = source->state.base_power;
        int total = 0;
        for (int index = 0; index < 5; ++index) total += memory_int(*source, "frost_damage_" + std::to_string(index));
        if (total > 0) context.enqueue(Task::boost_card(source->owner_id, call.source_entity_id, total, call.source_entity_id));
    });

    registry.register_handler(std::string(kTirNaLiaRoundEndEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr) return;
        const PlayerId enemy = context.state.opponent_id(source->owner_id);
        source->memory["saved_frost_melee"] = std::to_string(row_effect_duration(context.state, enemy, Zone::Melee, "frost"));
        source->memory["saved_frost_ranged"] = std::to_string(row_effect_duration(context.state, enemy, Zone::Ranged, "frost"));
    });

    registry.register_handler(std::string(kTirNaLiaDeployEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr) return;

        // Devotion is intentionally NOT resolved on Deploy.  Tir na Lia's
        // Devotion clause augments its Order: when the Order is used, restore
        // Frost lost during the previous round transition first, then spawn
        // and play Red Riders.
        const TargetSelector selector = allied_unit_selector();
        if (!require_or_request_card_target(context, call, selector, "提尔纳丽雅：选择友军单位获得增益")) return;
        const int amount = distinct_starting_bronze_wild_hunt(context.state, call.actor_id);
        if (amount > 0) context.enqueue(Task::boost_card(call.actor_id, call.target.entity_id, amount, call.source_entity_id));
    });

    registry.register_handler(std::string(kTirNaLiaOrderEffect), [](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source = context.state.find_card(call.source_entity_id);
        if (source == nullptr) return;

        // Devotion augments the Order, and Frost is restored before Red Riders
        // is created.  The saved values are captured by the round_end trigger
        // immediately before the round transition removes row effects.
        if (is_monsters_devotion(context.state, call.actor_id)) {
            const PlayerId enemy = context.state.opponent_id(call.actor_id);
            const int melee = memory_int(*source, "saved_frost_melee");
            const int ranged = memory_int(*source, "saved_frost_ranged");
            if (melee > 0) add_frost_row_effect(context, call.actor_id, enemy, Zone::Melee, melee);
            if (ranged > 0) add_frost_row_effect(context, call.actor_id, enemy, Zone::Ranged, ranged);
        }

        if (context.state.card_catalog == nullptr) return;
        const auto definition_id = context.state.card_catalog->id_of(kRedRidersId);
        if (!definition_id.has_value()) return;
        const EntityId created = primitive_spawn_card(context.state, definition_id.value(), call.actor_id,
            Location{call.actor_id, Zone::Stay, context.state.player(call.actor_id).stay.size()});
        if (created != kInvalidEntityId) {
            // The location Order finishes first. Playing the generated Red
            // Riders is a separate created-card chain, after charge consumption
            // and the intra-turn action marker have been resolved.
            context.append_continuation(ContinuationStep{
                ContinuationStepKind::PlayStagedCard,
                call.actor_id,
                created,
            });
        }
    });

    registry.register_handler(std::string(kArdGaethSpecialEffect), [](KernelContext& context, const EffectCall& call) {
        const PlayerId enemy = context.state.opponent_id(call.actor_id);
        add_frost_row_effect(context, call.actor_id, enemy, Zone::Melee, 3);
        add_frost_row_effect(context, call.actor_id, enemy, Zone::Ranged, 3);
    });

    registry.register_handler(std::string(kOberonKingDeployEffect), [](KernelContext& context, const EffectCall& call) {
        auto pool = bronze_wild_hunt_pool(context.state);
        if (!pool.empty()) spawn_and_play_created(context, call.actor_id, pool[context.random_index(pool.size())]);
    });
    registry.register_handler(std::string(kOberonKingRoundStartEffect), [](KernelContext& context, const EffectCall& call) {
        const auto loc = context.state.location_of(call.source_entity_id);
        if (context.state.card_catalog != nullptr && context.state.round_no == 2 && loc.has_value()
            && (loc->zone == Zone::Hand || loc->zone == Zone::Deck)) {
            const auto id = context.state.card_catalog->id_of(kOberonInvaderId);
            if (id) primitive_transform_card(context.state, call.source_entity_id, *id);
        }
    });
    registry.register_handler(std::string(kOberonInvaderDeployEffect), [](KernelContext& context, const EffectCall& call) {
        if (call.target.kind == ActionTargetKind::CardDefinition) {
            spawn_and_play_created(context, call.actor_id, call.target.definition_id);
            return;
        }
        auto pool = bronze_wild_hunt_pool(context.state);
        std::vector<CardDefId> shown;
        while (!pool.empty() && shown.size() < 3) {
            const std::size_t i = context.random_index(pool.size());
            shown.push_back(pool[i]);
            pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(i));
        }
        if (!shown.empty()) context.request_card_definition_choice(call.actor_id, call.source_entity_id, call.effect_id, "奥贝伦：入侵者，创造 1 个铜色狂猎单位", std::move(shown));
    });
    registry.register_handler(std::string(kOberonInvaderRoundStartEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const auto loc = context.state.location_of(call.source_entity_id);
        if (source != nullptr && context.state.card_catalog != nullptr && context.state.round_no == 3
            && loc.has_value() && (loc->zone == Zone::Hand || loc->zone == Zone::Deck)
            && starting_deck_devotion(context.state, source->owner_id)) {
            const auto id = context.state.card_catalog->id_of(kOberonConquerorId);
            if (id) primitive_transform_card(context.state, call.source_entity_id, *id);
        }
    });
    registry.register_handler(std::string(kOberonConquerorDeployEffect), [](KernelContext& context, const EffectCall& call) {
        if (call.target.kind == ActionTargetKind::CardDefinition) {
            spawn_and_play_created(context, call.actor_id, call.target.definition_id);
            return;
        }
        const auto all = bronze_wild_hunt_pool(context.state);
        std::vector<CardDefId> pool;
        for (CardDefId id : all) {
            const std::string& card_id = context.state.card_catalog->at(id).id;
            if (std::find(context.state.player(call.actor_id).starting_deck.begin(), context.state.player(call.actor_id).starting_deck.end(), card_id) != context.state.player(call.actor_id).starting_deck.end()) pool.push_back(id);
        }
        if (!pool.empty()) context.request_card_definition_choice(call.actor_id, call.source_entity_id, call.effect_id, "奥贝伦：征服者，从初始牌组生成狂猎单位", std::move(pool));
    });
    registry.register_handler(std::string(kOberonConquerorCardPlayedEffect), [](KernelContext& context, const EffectCall& call) {
        const RuntimeCard* source = context.state.find_card(call.source_entity_id);
        const RuntimeCard* played = context.state.find_card(call.event_target_entity_id);
        const auto loc = context.state.location_of(call.source_entity_id);
        if (source != nullptr && played != nullptr && loc.has_value() && is_battle_zone(loc->zone)
            && !source->state.locked
            && call.event_target_entity_id != call.source_entity_id
            && call.actor_id == source->controller_id && played->controller_id == source->controller_id
            && played->is_unit_card() && played->has_category(kWildHunt)) {
            context.enqueue(Task::boost_card(call.actor_id, call.event_target_entity_id, 1, call.source_entity_id));
        }
    });
}

void register_supported_card_effects(EffectRegistry& registry) {
    register_basic_supported_card_effects(registry);
}

}  // namespace supported_cards
}  // namespace gwent

