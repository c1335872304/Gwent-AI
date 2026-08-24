#include "gwent/c/core.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint64_t lcg_next(uint64_t* state) {
    *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return *state;
}

static size_t pick_legal_option(const gwent_rl_inference_batch* batch, size_t batch_row, uint64_t* rng) {
    const gwent_rl_batch_item* item = &batch->items[batch_row];
    assert(item->option_count > 0);
    assert(item->option_count <= GWENT_RL_MAX_OPTIONS);
    const size_t first = (size_t)(lcg_next(rng) % item->option_count);
    for (size_t offset = 0; offset < item->option_count; ++offset) {
        const size_t index = (first + offset) % item->option_count;
        const size_t flat = batch_row * GWENT_RL_MAX_OPTIONS + index;
        if (batch->option_mask[flat] != 0) {
            return index;
        }
    }
    assert(0 && "batch item had no legal option mask");
    return 0;
}

static void test_collector_can_batch_128_envs(void) {
    gwent_rl_collector_config config = gwent_rl_default_collector_config();
    config.num_envs = 128;
    config.max_batch_size = 128;
    config.base_seed = 1000;
    config.enable_invariants = 1;
    config.auto_reset_done_envs = 1;

    gwent_rl_collector* collector = gwent_rl_collector_create(&config);
    assert(collector != 0);
    assert(gwent_rl_collector_env_count(collector) == 128);
    assert(gwent_rl_collector_total_resets(collector) == 128);

    gwent_rl_inference_batch batch = {0};
    assert(gwent_rl_collector_collect(collector, &batch) == GWENT_C_OK);
    assert(batch.count == 128);
    assert(batch.max_count == 128);
    assert(batch.items != 0);
    assert(batch.global_features != 0);
    assert(batch.object_features != 0);
    assert(batch.object_controller_ids != 0);
    assert(batch.object_mask != 0);
    assert(batch.option_features != 0);
    assert(batch.option_card_ids != 0);
    assert(batch.option_target_row_ids != 0);
    assert(batch.option_insert_positions != 0);
    assert(batch.prefix_insert_positions != 0);
    assert(batch.prefix_mask != 0);
    assert(batch.prefix_card_ids != 0);
    assert(batch.option_mask != 0);

    for (size_t row = 0; row < batch.count; ++row) {
        const gwent_rl_batch_item* item = &batch.items[row];
        assert(item->env_index == row);
        assert(item->decision_serial != 0);
        assert(item->done == 0);
        assert(item->object_count > 0);
        assert(item->option_count > 0);
        assert(item->option_count <= GWENT_RL_MAX_OPTIONS);
        assert(item->prefix_count <= GWENT_RL_MAX_PREFIX);
        assert(batch.object_mask[row * GWENT_RL_MAX_OBJECTS] != 0);
        assert(batch.option_mask[row * GWENT_RL_MAX_OPTIONS] != 0);
    }

    gwent_rl_collector_destroy(collector);
}

static void test_collector_random_rollout_and_auto_reset(void) {
    gwent_rl_collector_config config = gwent_rl_default_collector_config();
    config.num_envs = 128;
    config.max_batch_size = 128;
    config.base_seed = 424242;
    config.enable_invariants = 1;
    config.auto_reset_done_envs = 1;

    gwent_rl_collector* collector = gwent_rl_collector_create(&config);
    assert(collector != 0);

    uint64_t rng = 777;
    size_t completed = 0;
    size_t applied = 0;

    for (int round = 0; round < 96; ++round) {
        gwent_rl_inference_batch batch = {0};
        assert(gwent_rl_collector_collect(collector, &batch) == GWENT_C_OK);
        assert(batch.count > 0);
        assert(batch.count <= 128);

        gwent_rl_batch_action actions[128];
        for (size_t row = 0; row < batch.count; ++row) {
            actions[row].env_index = batch.items[row].env_index;
            actions[row].decision_serial = batch.items[row].decision_serial;
            actions[row].option_index = pick_legal_option(&batch, row, &rng);
        }

        gwent_rl_apply_result_batch results = {0};
        assert(gwent_rl_collector_apply_actions(collector, actions, batch.count, &results) == GWENT_C_OK);
        assert(results.count == batch.count);
        assert(results.results != 0);
        applied += batch.count;

        for (size_t i = 0; i < results.count; ++i) {
            assert(results.results[i].result_code == GWENT_C_OK);
            assert(results.results[i].action_status == GWENT_C_ACTION_APPLIED);
            assert(results.results[i].actor_id == -1 || results.results[i].actor_id == 0 || results.results[i].actor_id == 1);
            if (results.results[i].done != 0) {
                ++completed;
                assert(results.results[i].winner_id == -1 || results.results[i].winner_id == 0 || results.results[i].winner_id == 1);
            }
        }
    }

    assert(applied >= 128);
    assert(gwent_rl_collector_total_steps(collector) == applied);
    assert(gwent_rl_collector_completed_episodes(collector) == completed);

    gwent_rl_collector_destroy(collector);
}

static void test_collector_rejects_stale_decision(void) {
    gwent_rl_collector_config config = gwent_rl_default_collector_config();
    config.num_envs = 4;
    config.max_batch_size = 4;
    config.base_seed = 9;
    config.auto_reset_done_envs = 1;

    gwent_rl_collector* collector = gwent_rl_collector_create(&config);
    assert(collector != 0);

    gwent_rl_inference_batch batch = {0};
    assert(gwent_rl_collector_collect(collector, &batch) == GWENT_C_OK);
    assert(batch.count == 4);

    gwent_rl_batch_action action = {0};
    action.env_index = batch.items[0].env_index;
    action.decision_serial = batch.items[0].decision_serial;
    action.option_index = 0;

    gwent_rl_apply_result_batch results = {0};
    assert(gwent_rl_collector_apply_actions(collector, &action, 1, &results) == GWENT_C_OK);
    assert(results.count == 1);
    assert(results.results[0].result_code == GWENT_C_OK);

    assert(gwent_rl_collector_apply_actions(collector, &action, 1, &results) == GWENT_C_STALE_DECISION);
    assert(results.count == 1);
    assert(results.results[0].result_code == GWENT_C_STALE_DECISION);

    gwent_rl_collector_destroy(collector);
}

static int row_has_card(const gwent_rl_inference_batch* batch, size_t row, int card_id, int owner_id) {
    const size_t base = row * GWENT_RL_MAX_OBJECTS;
    for (size_t i = 0; i < batch->items[row].object_count; ++i) {
        if (batch->object_card_ids[base + i] == card_id
            && batch->object_owner_ids[base + i] == owner_id) return 1;
    }
    return 0;
}

static void test_weighted_matchups_are_deterministic(void) {
    gwent_rl_collector_config config = gwent_rl_default_collector_config();
    config.num_envs = 100;
    config.max_batch_size = 100;
    gwent_rl_matchup_config matchups[] = {
        {GWENT_RL_DECK_A, GWENT_RL_DECK_B, 35},
        {GWENT_RL_DECK_B, GWENT_RL_DECK_A, 35},
        {GWENT_RL_DECK_A, GWENT_RL_DECK_A, 15},
        {GWENT_RL_DECK_B, GWENT_RL_DECK_B, 15},
    };
    gwent_rl_collector* collector = gwent_rl_collector_create_with_matchups(&config, matchups, 4);
    assert(collector != 0);
    gwent_rl_inference_batch batch = {0};
    assert(gwent_rl_collector_collect(collector, &batch) == GWENT_C_OK);
    size_t ab = 0, ba = 0, aa = 0, bb = 0;
    for (size_t row = 0; row < batch.count; ++row) {
        const int p0_a = row_has_card(&batch, row, 202185, 0);
        const int p0_b = row_has_card(&batch, row, 200055, 0);
        const int p1_a = row_has_card(&batch, row, 202185, 1);
        const int p1_b = row_has_card(&batch, row, 200055, 1);
        if (row < 35) { assert(p0_a && p1_b); ++ab; }
        else if (row < 70) { assert(p0_b && p1_a); ++ba; }
        else if (row < 85) { assert(p0_a && p1_a); ++aa; }
        else { assert(p0_b && p1_b); ++bb; }
    }
    assert(ab == 35 && ba == 35 && aa == 15 && bb == 15);
    gwent_rl_collector_destroy(collector);

    matchups[0].weight = 0;
    assert(gwent_rl_collector_create_with_matchups(&config, matchups, 4) == 0);
}

int main(void) {
    assert(gwent_c_result_code_name(GWENT_C_STALE_DECISION) != 0);
    test_collector_can_batch_128_envs();
    test_collector_random_rollout_and_auto_reset();
    test_collector_rejects_stale_decision();
    test_weighted_matchups_are_deterministic();
    return 0;
}
