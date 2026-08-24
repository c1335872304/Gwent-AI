#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "gwent/cards/deck_a.hpp"
#include "gwent/core/card_definition.hpp"
#include "gwent/core/state.hpp"
#include "gwent/engine/kernel.hpp"
#include "gwent/engine/legal_actions.hpp"
#include "gwent/engine/row_effects.hpp"

using namespace gwent;

namespace {

GameState make_running_state(PlayerId current = kPlayerZero) {
    GameState state;
    state.status = MatchStatus::Running;
    state.phase = MatchPhase::Playing;
    state.round_no = 1;
    state.turn_no = 1;
    state.starting_player_id = current;
    state.round_starting_player_id = current;
    state.current_player_id = current;
    return state;
}

std::size_t count_events(const std::vector<EventRecord>& events, const std::string& name) {
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const EventRecord& event) {
        return event.name == name;
    }));
}


void test_play_card_source_then_row_choice_is_factorized() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId unit = state.add_card(
        make_unit_definition("factorized_unit", "Factorized Unit", 4, Faction::Monsters),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );

    EffectRegistry registry;
    KernelResult first = apply_action_with_kernel(state, Action::play_card(kPlayerZero, unit), registry);
    assert(first);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(state.pending_choice->resume_kind == PendingChoiceResumeKind::PlayCard);
    assert(state.pending_choice->origin_action_type == ActionType::PlayCard);
    assert(state.pending_choice->legal_row_targets.size() == 2);
    assert((state.location_of(unit) == Location{kPlayerZero, Zone::Hand, 0}));
    assert(!state.turn_contexts[0].consumed_hand_card);
    assert(first.next_decision.has_value());
    assert(first.next_decision->type == DecisionType::ChooseTarget);
    assert(first.next_decision->legal_actions.size() == 2);

    KernelResult second = apply_action_with_kernel(
        state,
        Action::choose_row_target(kPlayerZero, unit, kPlayerZero, Zone::Ranged),
        registry
    );
    assert(second);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::InsertPosition);
    assert(second.next_decision.has_value());
    assert(second.next_decision->legal_actions.size() == 1);
    assert(second.next_decision->legal_actions.front()
        == Action::choose_insert_position(kPlayerZero, unit, kPlayerZero, Zone::Ranged, 0));
    assert((state.location_of(unit) == Location{kPlayerZero, Zone::Hand, 0}));

    KernelResult third = apply_action_with_kernel(
        state,
        Action::choose_insert_position(kPlayerZero, unit, kPlayerZero, Zone::Ranged, 0),
        registry
    );
    assert(third);
    assert(!state.pending_choice.has_value());
    assert((state.location_of(unit) == Location{kPlayerZero, Zone::Ranged, 0}));
    assert(state.turn_contexts[0].consumed_hand_card);
    assert(state.turn_contexts[0].played_card_this_turn);
}

void test_eredin_real_play_requests_enemy_row_after_insert_position() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId eredin = state.add_card(
        deck_a::make_eredin_breacc_glas_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);

    // Real UI flow: choose the card, choose its own deployment row, then choose
    // its dynamic insertion position.  Eredin's deploy effect must *then* ask
    // for an enemy row instead of reusing the own deployment row as frost target.
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, eredin), registry));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->resume_kind == PendingChoiceResumeKind::PlayCard);

    assert(apply_action_with_kernel(
        state,
        Action::choose_row_target(kPlayerZero, eredin, kPlayerZero, Zone::Melee),
        registry
    ));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::InsertPosition);

    assert(apply_action_with_kernel(
        state,
        Action::choose_insert_position(kPlayerZero, eredin, kPlayerZero, Zone::Melee, 0),
        registry
    ));
    assert((state.location_of(eredin) == Location{kPlayerZero, Zone::Melee, 0}));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(state.pending_choice->resume_kind == PendingChoiceResumeKind::InvokeEffect);
    assert(state.pending_choice->legal_row_targets.size() == 2);
    assert((state.pending_choice->legal_row_targets[0] == Location{kPlayerOne, Zone::Melee, 0}));
    assert((state.pending_choice->legal_row_targets[1] == Location{kPlayerOne, Zone::Ranged, 0}));
    assert(row_effect_duration(state, kPlayerZero, Zone::Melee, "frost") == 0);

    assert(apply_action_with_kernel(
        state,
        Action::choose_row_target(kPlayerZero, eredin, kPlayerOne, Zone::Ranged),
        registry
    ));
    assert(!state.pending_choice.has_value());
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 2);
    assert(row_effect_duration(state, kPlayerZero, Zone::Melee, "frost") == 0);
}

void test_order_without_target_pauses_then_resumes_after_choice() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId garkain = state.add_card(deck_a::make_garkain_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("pending_garkain_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy_a = state.add_card(make_unit_definition("enemy_a", "Enemy A", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId enemy_b = state.add_card(make_unit_definition("enemy_b", "Enemy B", 4, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Ranged, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelConfig config;
    config.order_action_consumes_turn = false;

    KernelResult pending = apply_action_with_kernel(state, Action::use_order(kPlayerZero, garkain), registry, config);

    assert(pending);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::CardTarget);
    assert(state.pending_choice->player_id == kPlayerZero);
    assert(state.pending_choice->source_entity_id == garkain);
    assert(state.pending_choice->effect_id == std::string(deck_a::kGarkainOrderEffect));
    assert(state.pending_choice->origin_action_type == ActionType::UseOrder);
    assert(state.pending_choice->legal_card_targets.size() == 2);
    assert(state.pending_choice->continuation.size() == 2);
    assert(state.pending_choice->continuation[0].kind == ContinuationStepKind::ConsumeOrderSource);
    assert(state.pending_choice->continuation[1].kind == ContinuationStepKind::MarkIntraTurnFollowup);
    assert(state.find_card(enemy_a)->state.bleeding == 0);
    assert(state.find_card(enemy_b)->state.bleeding == 0);
    assert(pending.next_decision.has_value());
    assert(pending.next_decision->type == DecisionType::ChooseTarget);
    assert(pending.next_decision->legal_actions.size() == 2);
    assert(count_events(pending.events, "pending_card_choice_requested") == 1);

    // A normal turn action cannot cut in front of the pending target choice.
    KernelResult rejected = apply_action_with_kernel(state, Action::pass(kPlayerZero), registry, config);
    assert(!rejected);
    assert(rejected.status == ActionStatus::IllegalAction);
    assert(state.pending_choice.has_value());

    KernelResult resumed = apply_action_with_kernel(
        state,
        Action::choose_card_target(kPlayerZero, garkain, enemy_b),
        registry,
        config
    );

    assert(resumed);
    assert(!state.pending_choice.has_value());
    assert(state.find_card(enemy_a)->state.bleeding == 0);
    assert(state.find_card(enemy_b)->state.bleeding == 2);
    assert(!can_use_order_source(state, kPlayerZero, garkain));
    assert(state.current_player_id == kPlayerZero);
    assert(count_events(resumed.events, "pending_card_choice_resolved") == 1);
    assert(count_events(resumed.events, "status_added") == 1);
    assert(count_events(resumed.events, "order_cooldown_started") == 1);
}

GameState make_insert_position_state() {
    GameState state = make_running_state(kPlayerZero);
    state.add_card(make_unit_definition("insert_a", "A", 1), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("insert_b", "B", 1), kPlayerZero, Location{kPlayerZero, Zone::Melee, 1});
    state.add_card(make_unit_definition("insert_c", "C", 1), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    state.add_card(make_unit_definition("insert_x", "X", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    return state;
}

void select_insert_row(GameState& state, EntityId source, Zone row, EffectRegistry& registry) {
    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, source), registry));
    assert(apply_action_with_kernel(state, Action::choose_row_target(kPlayerZero, source, kPlayerZero, row), registry));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::InsertPosition);
}

void test_dynamic_insert_positions_and_execution() {
    EffectRegistry registry;
    for (int position = 0; position <= 2; ++position) {
        GameState state = make_insert_position_state();
        const EntityId x = state.player(kPlayerZero).hand.front();
        const EntityId a = state.player(kPlayerZero).row(Zone::Melee)[0];
        const EntityId b = state.player(kPlayerZero).row(Zone::Melee)[1];
        select_insert_row(state, x, Zone::Melee, registry);
        const auto actions = legal_pending_choice_actions(state);
        assert(actions.size() == 3);
        assert(actions[0] == Action::choose_insert_position(kPlayerZero, x, kPlayerZero, Zone::Melee, 0));
        assert(actions[1] == Action::choose_insert_position(kPlayerZero, x, kPlayerZero, Zone::Melee, 1));
        assert(actions[2] == Action::choose_insert_position(kPlayerZero, x, kPlayerZero, Zone::Melee, 2));
        assert(actions[0] != actions[1]);
        assert(apply_action_with_kernel(
            state,
            Action::choose_insert_position(kPlayerZero, x, kPlayerZero, Zone::Melee, position),
            registry
        ));
        const std::vector<EntityId> expected = position == 0
            ? std::vector<EntityId>{x, a, b}
            : (position == 1 ? std::vector<EntityId>{a, x, b} : std::vector<EntityId>{a, b, x});
        assert(state.player(kPlayerZero).row(Zone::Melee) == expected);
    }

    GameState independent = make_insert_position_state();
    const EntityId x = independent.player(kPlayerZero).hand.front();
    select_insert_row(independent, x, Zone::Ranged, registry);
    const auto ranged_actions = legal_pending_choice_actions(independent);
    assert(ranged_actions.size() == 2);
    assert(ranged_actions[0].target.zone == Zone::Ranged);
    assert(ranged_actions[1].insert_position == 1);

    GameState removed = make_insert_position_state();
    const EntityId removed_b = removed.player(kPlayerZero).row(Zone::Melee)[1];
    removed.move_card(removed_b, Location{kPlayerZero, Zone::Cemetery, 0});
    const EntityId removed_x = removed.player(kPlayerZero).hand.front();
    select_insert_row(removed, removed_x, Zone::Melee, registry);
    assert(legal_pending_choice_actions(removed).size() == 2);

    GameState invalid = make_insert_position_state();
    const EntityId invalid_x = invalid.player(kPlayerZero).hand.front();
    select_insert_row(invalid, invalid_x, Zone::Melee, registry);
    const KernelResult rejected = apply_action_with_kernel(
        invalid,
        Action::choose_insert_position(kPlayerZero, invalid_x, kPlayerZero, Zone::Melee, 3),
        registry
    );
    assert(!rejected);
    assert(rejected.status == ActionStatus::InvalidTarget);
}

void test_tir_na_lia_has_no_zeal_and_order_waits_one_turn() {
    GameState state = make_running_state(kPlayerZero);
    state.engine_phase = EnginePhase::ActionWindow;
    const EntityId tir = state.add_card(
        deck_a::make_tir_na_lia_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );

    RuntimeCard* card = state.find_card(tir);
    assert(card != nullptr);
    card->runtime.entered_this_turn = true;
    assert(!can_use_order_source(state, kPlayerZero, tir));

    card->runtime.entered_this_turn = false;
    assert(can_use_order_source(state, kPlayerZero, tir));
}

void test_tir_na_lia_order_finishes_before_created_red_riders() {
    GameState state = make_running_state(kPlayerZero);
    state.engine_phase = EnginePhase::ActionWindow;
    state.card_catalog = std::make_shared<CardCatalog>(deck_a::make_catalog());
    const EntityId tir = state.add_card(
        deck_a::make_tir_na_lia_definition(),
        kPlayerZero,
        Location{kPlayerZero, Zone::Melee, 0}
    );
    state.add_card(
        make_unit_definition("tir_followup", "Tir Followup", 1, Faction::Monsters),
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );

    RuntimeCard* tir_card = state.find_card(tir);
    assert(tir_card != nullptr);
    tir_card->memory["saved_frost_melee"] = "2";
    tir_card->memory["saved_frost_ranged"] = "1";

    EffectRegistry registry;
    deck_a::register_deck_a_effects(registry);
    KernelConfig config;
    config.order_action_consumes_turn = false;
    config.invariant_policy = InvariantPolicy::AfterApply;

    KernelResult generated = apply_action_with_kernel(
        state,
        Action::use_order(kPlayerZero, tir),
        registry,
        config
    );

    assert(generated);
    // Devotion Frost is restored as part of the Order before Red Riders asks
    // for its generated-card choice.
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 2);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 1);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::CardDefinitionChoice);
    assert(state.pending_choice->effect_id == std::string(deck_a::kRedRidersSpecialEffect));
    const EntityId riders = state.pending_choice->source_entity_id;
    assert(state.find_card(tir)->state.order_charges == 0);
    assert(state.find_card(tir)->runtime.order_used);
    assert(!state.find_card(tir)->runtime.order_pending_consume);
    assert(state.turn_contexts[0].used_intra_turn_action);
    assert(!state.turn_contexts[0].consumed_hand_card);
    assert(!state.turn_contexts[0].played_card_this_turn);
    assert(state.location_of(riders)->zone == Zone::Stay);

    const CardDefId both_rows = state.card_catalog->id_of(deck_a::kRedRidersBothRowsId).value();
    KernelResult resolved = apply_action_with_kernel(
        state,
        Action::choose_card_definition(kPlayerZero, riders, both_rows),
        registry,
        config
    );

    assert(resolved);
    assert(!state.pending_choice.has_value());
    assert(state.location_of(riders)->zone == Zone::Cemetery);
    // Red Riders' both-rows mode adds 2 more turns after the Devotion
    // restoration, so the previously restored 2/1 becomes 4/3.
    assert(row_effect_duration(state, kPlayerOne, Zone::Melee, "frost") == 4);
    assert(row_effect_duration(state, kPlayerOne, Zone::Ranged, "frost") == 3);
    assert(!state.turn_contexts[0].consumed_hand_card);
    assert(!state.turn_contexts[0].played_card_this_turn);
}

void test_invalid_pending_target_is_rejected() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId bruxa = state.add_card(deck_a::make_bruxa_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("pending_bruxa_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("enemy", "Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId ally = state.add_card(make_unit_definition("ally", "Ally", 5, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelConfig config;
    config.order_action_consumes_turn = false;

    KernelResult pending = apply_action_with_kernel(state, Action::use_order(kPlayerZero, bruxa), registry, config);
    assert(pending);
    assert(state.pending_choice.has_value());

    KernelResult invalid = apply_action_with_kernel(
        state,
        Action::choose_card_target(kPlayerZero, bruxa, ally),
        registry,
        config
    );
    assert(!invalid);
    assert(invalid.status == ActionStatus::InvalidTarget);
    assert(state.pending_choice.has_value());
    assert(state.find_card(enemy)->state.bleeding == 0);
    assert(state.find_card(ally)->state.bleeding == 0);
}

void test_stratagem_order_can_pause_for_hand_target_then_banish() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId stratagem = state.add_card(deck_a::make_armorer_workshop_definition(), kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    const EntityId hand_unit = state.add_card(make_unit_definition("hand_unit", "Hand Unit", 4, Faction::Monsters), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    EffectRegistry registry;
    deck_a::register_basic_deck_a_effects(registry);

    KernelConfig config;
    config.order_action_consumes_turn = false;

    KernelResult pending = apply_action_with_kernel(state, Action::use_order(kPlayerZero, stratagem), registry, config);
    assert(pending);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->legal_card_targets.size() == 1);
    assert((state.location_of(stratagem) == Location{kPlayerZero, Zone::Melee, 0}));

    KernelResult resumed = apply_action_with_kernel(
        state,
        Action::choose_card_target(kPlayerZero, stratagem, hand_unit),
        registry,
        config
    );

    assert(resumed);
    assert(!state.pending_choice.has_value());
    assert((state.location_of(stratagem) == Location{kPlayerZero, Zone::Banished, 0}));
    assert(state.find_card(hand_unit)->state.power == 7);
    assert(state.find_card(hand_unit)->state.armor == 2);
    assert(state.board_score(kPlayerZero) == 0);
}


void test_pending_choice_preserves_full_queued_task_tail() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition order_card = make_unit_definition("queued_order", "Queued Order", 4, Faction::Monsters);
    order_card.effect_ids.push_back("order:queued.choice");
    order_card.metadata["order"] = "true";
    const EntityId source = state.add_card(order_card, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("queued_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("queued_enemy", "Enemy", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    registry.register_handler("queued.choice", [enemy](KernelContext& context, const EffectCall& call) {
        if (call.target.kind == ActionTargetKind::None) {
            // This used to be silently lost when the kernel cleared TaskQueue on
            // seeing pending_choice. It must survive behind the choice.
            context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, 1, call.source_entity_id));
            context.request_card_choice(call.actor_id, call.source_entity_id, call.effect_id, "choose enemy", {enemy});
            return;
        }
    });

    KernelResult first = apply_action_with_kernel(state, Action::use_order(kPlayerZero, source), registry);
    assert(first);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->suspended_tasks.size() == 1);
    assert(state.pending_choice->suspended_tasks.front().type == TaskType::BoostCard);
    assert(state.find_card(source)->state.power == 4);

    KernelResult resumed = apply_action_with_kernel(state, Action::choose_card_target(kPlayerZero, source, enemy), registry);
    assert(resumed);
    assert(!state.pending_choice.has_value());
    assert(state.find_card(source)->state.power == 5);
    assert(state.find_card(source)->runtime.order_used);
    assert(state.turn_contexts[0].used_intra_turn_action);
}

void test_nested_choices_preserve_structured_order_continuation() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition order_card = make_unit_definition("nested_order", "Nested Order", 4, Faction::Monsters);
    order_card.effect_ids.push_back("order:nested.order");
    order_card.metadata["order"] = "true";
    const EntityId source = state.add_card(order_card, kPlayerZero, Location{kPlayerZero, Zone::Melee, 0});
    state.add_card(make_unit_definition("nested_followup", "Followup", 1), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("nested_enemy", "Nested Enemy", 5, Faction::Monsters), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    registry.register_handler("nested.order", [enemy](KernelContext& context, const EffectCall& call) {
        if (call.target.kind == ActionTargetKind::None) {
            context.request_card_choice(call.actor_id, call.source_entity_id, call.effect_id, "first choice", {enemy});
            return;
        }
        if (call.target.kind == ActionTargetKind::Card) {
            context.request_row_choice(
                call.actor_id,
                call.source_entity_id,
                call.effect_id,
                "second choice",
                {Location{call.actor_id, Zone::Melee, 0}, Location{call.actor_id, Zone::Ranged, 0}}
            );
            return;
        }
        if (call.target.kind == ActionTargetKind::Row) {
            context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, 2, call.source_entity_id));
        }
    });

    KernelConfig config;
    config.order_action_consumes_turn = false;

    KernelResult first = apply_action_with_kernel(state, Action::use_order(kPlayerZero, source), registry, config);
    assert(first);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::CardTarget);
    assert(state.pending_choice->continuation.size() == 2);

    KernelResult second = apply_action_with_kernel(
        state,
        Action::choose_card_target(kPlayerZero, source, enemy),
        registry,
        config
    );
    assert(second);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::RowTarget);
    assert(state.pending_choice->continuation.size() == 2);
    assert(state.pending_choice->continuation[0].kind == ContinuationStepKind::ConsumeOrderSource);
    assert(state.pending_choice->continuation[1].kind == ContinuationStepKind::MarkIntraTurnFollowup);

    KernelResult third = apply_action_with_kernel(
        state,
        Action::choose_row_target(kPlayerZero, source, kPlayerZero, Zone::Ranged),
        registry,
        config
    );
    assert(third);
    assert(!state.pending_choice.has_value());
    assert(state.find_card(source)->state.power == 6);
    assert(state.find_card(source)->runtime.order_used);
    assert(state.turn_contexts[static_cast<std::size_t>(kPlayerZero)].used_intra_turn_action);
    assert(state.current_player_id == kPlayerZero);
    assert(count_events(third.events, "intra_turn_followup_required") == 1);
}

void test_resolution_frame_accumulates_full_ordered_decision_prefix() {
    GameState state = make_running_state(kPlayerZero);
    CardDefinition chained = make_unit_definition("prefix_unit", "Prefix Unit", 4, Faction::Monsters);
    chained.effect_ids.push_back("deploy:prefix.deploy");
    const EntityId source = state.add_card(chained, kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});
    const EntityId enemy = state.add_card(make_unit_definition("prefix_enemy", "Prefix Enemy", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});

    EffectRegistry registry;
    registry.register_handler("prefix.deploy", [enemy](KernelContext& context, const EffectCall& call) {
        context.request_card_choice(call.actor_id, call.source_entity_id, "prefix.card_choice", "card", {enemy});
    });
    registry.register_handler("prefix.card_choice", [](KernelContext& context, const EffectCall& call) {
        context.request_row_choice(
            call.actor_id,
            call.source_entity_id,
            "prefix.row_choice",
            "row",
            {Location{call.actor_id, Zone::Melee, 0}, Location{call.actor_id, Zone::Ranged, 0}}
        );
    });
    registry.register_handler("prefix.row_choice", [](KernelContext& context, const EffectCall& call) {
        context.enqueue(Task::boost_card(call.actor_id, call.source_entity_id, 1, call.source_entity_id));
    });

    KernelResult first = apply_action_with_kernel(state, Action::play_card(kPlayerZero, source), registry);
    assert(first);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->resolution_frame);
    assert(state.pending_choice->resolution_frame->decision_prefix.size() == 1);
    assert(state.pending_choice->resolution_frame->decision_prefix[0].kind == ActionType::PlayCard);

    KernelResult second = apply_action_with_kernel(
        state,
        Action::choose_row_target(kPlayerZero, source, kPlayerZero, Zone::Melee),
        registry
    );
    assert(second);
    assert(state.pending_choice.has_value());
    const auto& prefix2 = state.pending_choice->resolution_frame->decision_prefix;
    assert(prefix2.size() == 2);
    assert(prefix2[0].kind == ActionType::PlayCard);
    assert(prefix2[1].kind == ActionType::ChooseRowTarget);
    assert(prefix2[1].row.has_value());
    assert(prefix2[1].row->zone == Zone::Melee);

    KernelResult third = apply_action_with_kernel(
        state,
        Action::choose_insert_position(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    );
    assert(third);
    assert(state.pending_choice.has_value());
    const auto& prefix3 = state.pending_choice->resolution_frame->decision_prefix;
    assert(prefix3.size() == 3);
    assert(prefix3[2].kind == ActionType::ChooseInsertPosition);
    assert(prefix3[2].row.has_value());
    assert(prefix3[2].row->index == 0);

    KernelResult fourth = apply_action_with_kernel(
        state,
        Action::choose_card_target(kPlayerZero, source, enemy),
        registry
    );
    assert(fourth);
    assert(state.pending_choice.has_value());
    const auto& prefix4 = state.pending_choice->resolution_frame->decision_prefix;
    assert(prefix4.size() == 4);
    assert(prefix4[3].kind == ActionType::ChooseCardTarget);
    assert(prefix4[3].target_entity_id == enemy);

    KernelResult fifth = apply_action_with_kernel(
        state,
        Action::choose_row_target(kPlayerZero, source, kPlayerZero, Zone::Ranged),
        registry
    );
    assert(fifth);
    assert(!state.pending_choice.has_value());
    assert(state.find_card(source)->state.power == 5);
}

void test_deploy_starts_after_insert_with_no_structural_target_and_excludes_self() {
    GameState state = make_running_state(kPlayerZero);

    const EntityId ally = state.add_card(
        make_unit_definition("deploy_ally", "Deploy Ally", 4, Faction::Monsters),
        kPlayerZero,
        Location{kPlayerZero, Zone::Ranged, 0}
    );

    CardDefinition source_definition = make_unit_definition(
        "deploy_source",
        "Deploy Source",
        3,
        Faction::Monsters
    );
    source_definition.effect_ids = {"deploy:test.deploy.target"};
    const EntityId source = state.add_card(
        source_definition,
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );

    EffectRegistry registry;
    registry.register_handler("test.deploy.target", [ally](KernelContext& context, const EffectCall& call) {
        RuntimeCard* source_card = context.state.find_card(call.source_entity_id);
        assert(source_card != nullptr);
        source_card->memory["initial_deploy_target"] = call.target.kind == ActionTargetKind::None ? "none" : "not_none";

        // Deliberately pass the source as a candidate. The kernel's deploy
        // choice boundary must remove it so a newly deployed card can never
        // select itself as its own Deploy target.
        context.request_card_choice(
            call.actor_id,
            call.source_entity_id,
            call.effect_id,
            "choose allied unit",
            {call.source_entity_id, ally}
        );
    });

    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, source), registry));
    assert(apply_action_with_kernel(
        state,
        Action::choose_row_target(kPlayerZero, source, kPlayerZero, Zone::Melee),
        registry
    ));
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::InsertPosition);

    assert(apply_action_with_kernel(
        state,
        Action::choose_insert_position(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    ));

    assert((state.location_of(source) == Location{kPlayerZero, Zone::Melee, 0}));
    assert(state.find_card(source)->memory["initial_deploy_target"] == "none");
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->kind == PendingChoiceKind::CardTarget);
    assert(state.pending_choice->legal_card_targets.size() == 1);
    assert(state.pending_choice->legal_card_targets.front() == ally);
    assert(std::find(
        state.pending_choice->legal_card_targets.begin(),
        state.pending_choice->legal_card_targets.end(),
        source
    ) == state.pending_choice->legal_card_targets.end());
}

void test_real_deploy_card_targets_exclude_newly_inserted_source() {
    {
        GameState state = make_running_state(kPlayerZero);
        const EntityId ally = state.add_card(
            make_unit_definition("navigator_ally", "Navigator Ally", 6, Faction::Monsters),
            kPlayerZero,
            Location{kPlayerZero, Zone::Ranged, 0}
        );
        const EntityId navigator = state.add_card(
            deck_a::make_wild_hunt_navigator_definition(),
            kPlayerZero,
            Location{kPlayerZero, Zone::Hand, 0}
        );

        EffectRegistry registry;
        deck_a::register_deck_a_effects(registry);
        assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, navigator), registry));
        assert(apply_action_with_kernel(
            state,
            Action::choose_row_target(kPlayerZero, navigator, kPlayerZero, Zone::Melee),
            registry
        ));
        assert(apply_action_with_kernel(
            state,
            Action::choose_insert_position(kPlayerZero, navigator, kPlayerZero, Zone::Melee, 0),
            registry
        ));

        assert(state.pending_choice.has_value());
        assert(state.pending_choice->kind == PendingChoiceKind::CardTarget);
        assert(state.pending_choice->legal_card_targets.size() == 1);
        assert(state.pending_choice->legal_card_targets.front() == ally);
        assert(std::find(
            state.pending_choice->legal_card_targets.begin(),
            state.pending_choice->legal_card_targets.end(),
            navigator
        ) == state.pending_choice->legal_card_targets.end());
    }

    {
        GameState state = make_running_state(kPlayerZero);
        const EntityId ally = state.add_card(
            make_unit_definition("taskmaster_ally", "Taskmaster Ally", 10, Faction::Monsters),
            kPlayerZero,
            Location{kPlayerZero, Zone::Ranged, 0}
        );
        const EntityId enemy = state.add_card(
            make_unit_definition("taskmaster_enemy", "Taskmaster Enemy", 6, Faction::Monsters),
            kPlayerOne,
            Location{kPlayerOne, Zone::Melee, 0}
        );
        const EntityId taskmaster = state.add_card(
            deck_a::make_naglfar_taskmaster_definition(),
            kPlayerZero,
            Location{kPlayerZero, Zone::Hand, 0}
        );

        EffectRegistry registry;
        deck_a::register_deck_a_effects(registry);
        assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, taskmaster), registry));
        assert(apply_action_with_kernel(
            state,
            Action::choose_row_target(kPlayerZero, taskmaster, kPlayerZero, Zone::Melee),
            registry
        ));
        assert(apply_action_with_kernel(
            state,
            Action::choose_insert_position(kPlayerZero, taskmaster, kPlayerZero, Zone::Melee, 0),
            registry
        ));

        assert(state.pending_choice.has_value());
        assert(state.pending_choice->kind == PendingChoiceKind::CardTarget);
        const auto& targets = state.pending_choice->legal_card_targets;
        assert(std::find(targets.begin(), targets.end(), ally) != targets.end());
        assert(std::find(targets.begin(), targets.end(), enemy) != targets.end());
        assert(std::find(targets.begin(), targets.end(), taskmaster) == targets.end());
    }
}

void test_deploy_with_only_self_candidate_does_not_open_empty_choice() {
    GameState state = make_running_state(kPlayerZero);

    CardDefinition source_definition = make_unit_definition(
        "deploy_self_only",
        "Deploy Self Only",
        3,
        Faction::Monsters
    );
    source_definition.effect_ids = {"deploy:test.deploy.self_only"};
    const EntityId source = state.add_card(
        source_definition,
        kPlayerZero,
        Location{kPlayerZero, Zone::Hand, 0}
    );

    EffectRegistry registry;
    registry.register_handler("test.deploy.self_only", [](KernelContext& context, const EffectCall& call) {
        context.request_card_choice(
            call.actor_id,
            call.source_entity_id,
            call.effect_id,
            "self must be excluded",
            {call.source_entity_id}
        );
    });

    assert(apply_action_with_kernel(state, Action::play_card(kPlayerZero, source), registry));
    assert(apply_action_with_kernel(
        state,
        Action::choose_row_target(kPlayerZero, source, kPlayerZero, Zone::Melee),
        registry
    ));
    assert(apply_action_with_kernel(
        state,
        Action::choose_insert_position(kPlayerZero, source, kPlayerZero, Zone::Melee, 0),
        registry
    ));

    assert(!state.pending_choice.has_value());
    assert((state.location_of(source) == Location{kPlayerZero, Zone::Melee, 0}));
    assert(state.turn_contexts[0].consumed_hand_card);
}

void test_resolution_budget_is_shared_across_choice_and_rolls_back_root_action() {
    GameState state = make_running_state(kPlayerZero);
    const EntityId watcher = state.add_card(make_unit_definition("budget_watcher", "Budget Watcher", 2), kPlayerZero, Location{kPlayerZero, Zone::Ranged, 0});
    const EntityId enemy = state.add_card(make_unit_definition("budget_enemy", "Budget Enemy", 5), kPlayerOne, Location{kPlayerOne, Zone::Melee, 0});
    const EntityId played = state.add_card(make_unit_definition("budget_played", "Budget Played", 3), kPlayerZero, Location{kPlayerZero, Zone::Hand, 0});

    RuntimeListener listener;
    listener.listener_id = ++state.next_listener_id;
    listener.event = "card_played";
    listener.handler = "budget.choice";
    listener.source_id = watcher;
    listener.once = true;
    state.listeners[listener.listener_id] = listener;

    EffectRegistry registry;
    registry.register_handler("budget.choice", [enemy](KernelContext& context, const EffectCall& call) {
        context.request_card_choice(call.actor_id, call.source_entity_id, "budget.resume", "choose", {enemy});
    });
    registry.register_handler("budget.resume", [](KernelContext& context, const EffectCall& call) {
        context.emit(EventRecord{TaskType::PlayCard, "budget_choice_resumed", call.actor_id, call.source_entity_id});
    });

    KernelConfig config;
    config.max_tasks_per_resolution = 2;

    KernelResult first = apply_action_with_kernel(
        state,
        Action::play_card(kPlayerZero, played, kPlayerZero, Zone::Melee, 0),
        registry,
        config
    );
    assert(first);
    assert(state.pending_choice.has_value());
    assert(state.pending_choice->resolution_frame->budget.tasks_processed == 1);
    assert(state.location_of(played)->zone == Zone::Melee);

    KernelResult second = apply_action_with_kernel(
        state,
        Action::choose_card_target(kPlayerZero, watcher, enemy),
        registry,
        config
    );
    assert(!second);
    assert(second.status == ActionStatus::TaskLimitExceeded);
    assert(!state.pending_choice.has_value());
    assert((state.location_of(played) == Location{kPlayerZero, Zone::Hand, 0}));
    assert(state.find_card(played)->state.power == 3);
    assert(state.current_player_id == kPlayerZero);
}

}  // namespace

int main() {
    test_play_card_source_then_row_choice_is_factorized();
    test_dynamic_insert_positions_and_execution();
    test_eredin_real_play_requests_enemy_row_after_insert_position();
    test_deploy_starts_after_insert_with_no_structural_target_and_excludes_self();
    test_real_deploy_card_targets_exclude_newly_inserted_source();
    test_deploy_with_only_self_candidate_does_not_open_empty_choice();
    test_order_without_target_pauses_then_resumes_after_choice();
    test_tir_na_lia_has_no_zeal_and_order_waits_one_turn();
    test_tir_na_lia_order_finishes_before_created_red_riders();
    test_invalid_pending_target_is_rejected();
    test_stratagem_order_can_pause_for_hand_target_then_banish();
    test_pending_choice_preserves_full_queued_task_tail();
    test_nested_choices_preserve_structured_order_continuation();
    test_resolution_frame_accumulates_full_ordered_decision_prefix();
    test_resolution_budget_is_shared_across_choice_and_rolls_back_root_action();

    std::cout << "gwent_pending_decision_tests: OK\n";
    return 0;
}
