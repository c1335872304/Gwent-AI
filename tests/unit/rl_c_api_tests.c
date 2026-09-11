#include "gwent/c/core.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint64_t lcg_next(uint64_t* state) {
    *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return *state;
}



static void finish_opening_mulligans(gwent_rl_env* env) {
    for (int player = 0; player < 2; ++player) {
        const gwent_rl_observation* obs = gwent_rl_env_observation(env);
        assert(obs != 0);
        assert(obs->decision_kind == GWENT_RL_DECISION_MULLIGAN);
        size_t keep_index = (size_t)-1;
        int saw_mulligan = 0;
        for (size_t i = 0; i < obs->option_count; ++i) {
            if (obs->option_kind_ids[i] == GWENT_RL_OPTION_KEEP_HAND) {
                keep_index = i;
            }
            if (obs->option_kind_ids[i] == GWENT_RL_OPTION_MULLIGAN) {
                saw_mulligan = 1;
            }
        }
        assert(keep_index != (size_t)-1);
        assert(saw_mulligan);
        gwent_rl_step_result step = {0};
        assert(gwent_rl_env_step_option(env, keep_index, &step) == GWENT_C_OK);
        assert(step.result_code == GWENT_C_OK);
        assert(step.action_status == GWENT_C_ACTION_APPLIED);
    }
    assert(gwent_rl_env_observation(env)->decision_kind == GWENT_RL_DECISION_TURN);
}

static void assert_no_deck_observation_features(const gwent_rl_observation* obs) {
    assert(obs != 0);
    for (size_t i = 0; i < obs->object_count; ++i) {
        /* gwent::Zone::Deck is the stable C++ enum value 0. */
        assert(obs->object_zone_ids[i] != 0);
    }
    for (size_t i = 0; i < GWENT_RL_GLOBAL_FEATURE_COUNT; ++i) {
        const char* name = gwent_rl_global_feature_name(i);
        assert(name != 0);
        assert(strstr(name, "deck") == 0);
    }
}


static void assert_fair_information_boundary(const gwent_rl_observation* obs) {
    assert(obs != 0);
    int own_hand_objects = 0;
    int opponent_hand_objects = 0;
    int actor_decklist_objects = 0;
    int opponent_decklist_objects = 0;
    for (size_t i = 0; i < obs->object_count; ++i) {
        const int zone = obs->object_zone_ids[i];
        const int owner = obs->object_owner_ids[i];
        if (zone == 1) { /* gwent::Zone::Hand */
            if (owner == obs->actor_id) ++own_hand_objects;
            else ++opponent_hand_objects;
        }
        if (zone == GWENT_RL_KNOWLEDGE_ZONE_PUBLIC_DECKLIST) {
            assert(obs->object_entity_ids[i] == -1);
            if (owner == obs->actor_id) ++actor_decklist_objects;
            else ++opponent_decklist_objects;
        }
    }
    assert(own_hand_objects == (int)obs->global_features[14]);
    assert(opponent_hand_objects == 0);
    assert(actor_decklist_objects > 0);
    assert(opponent_decklist_objects > 0);
}

static void test_rl_env_reset_and_observation(void) {
    gwent_rl_config config = gwent_rl_default_config();
    config.seed = 1234;
    config.enable_invariants = 1;

    gwent_rl_env* env = gwent_rl_env_create(&config);
    assert(env != 0);

    /* Product/debug row-effect introspection must not alter the RL schema. */
    assert(gwent_rl_env_row_effect_duration(env, 0, 0, "frost") == 0);
    assert(gwent_rl_env_row_effect_duration(env, 1, 1, "blood_moon") == 0);
    assert(gwent_rl_env_row_effect_duration(env, -1, 0, "frost") == -1);
    assert(gwent_rl_env_row_effect_duration(env, 0, 2, "frost") == -1);
    assert(gwent_rl_env_row_effect_duration(env, 0, 0, 0) == -1);

    const gwent_rl_observation* obs = gwent_rl_env_observation(env);
    assert(obs != 0);
    assert(obs->schema_version == GWENT_RL_SCHEMA_VERSION);
    assert(gwent_rl_schema_version() == GWENT_RL_SCHEMA_VERSION);
    assert(gwent_rl_action_grammar_version() == GWENT_RL_ACTION_GRAMMAR_VERSION);
    assert(gwent_rl_reward_config_version() == GWENT_RL_REWARD_CONFIG_VERSION);
    assert(gwent_rl_global_feature_count() == GWENT_RL_GLOBAL_FEATURE_COUNT);
    assert(gwent_rl_object_feature_count() == GWENT_RL_OBJECT_FEATURE_COUNT);
    assert(gwent_rl_option_feature_count() == GWENT_RL_OPTION_FEATURE_COUNT);
    assert(gwent_rl_global_feature_name(0) != 0);
    assert(gwent_rl_object_feature_name(0) != 0);
    assert(gwent_rl_option_feature_name(0) != 0);
    assert(obs->done == 0);
    assert(obs->actor_id == gwent_rl_env_actor_id(env));
    assert(obs->opponent_id == 0 || obs->opponent_id == 1);
    assert(obs->perspective_player_id == obs->actor_id);
    assert(obs->option_count == gwent_rl_env_option_count(env));
    assert(obs->option_count > 0);
    assert(obs->decision_kind == GWENT_RL_DECISION_MULLIGAN);
    int saw_keep = 0;
    int saw_mulligan = 0;
    for (size_t i = 0; i < obs->option_count; ++i) {
        saw_keep |= obs->option_kind_ids[i] == GWENT_RL_OPTION_KEEP_HAND;
        saw_mulligan |= obs->option_kind_ids[i] == GWENT_RL_OPTION_MULLIGAN;
    }
    assert(saw_keep);
    assert(saw_mulligan);
    assert(obs->object_count > 0);
    assert(obs->object_count <= GWENT_RL_MAX_OBJECTS);
    assert(obs->option_count <= GWENT_RL_MAX_OPTIONS);
    assert(obs->object_mask[0] == 1);
    assert(obs->option_mask[0] == 1);
    assert(obs->object_controller_ids[0] == 0 || obs->object_controller_ids[0] == 1);
    assert(obs->global_features[0] == 1.0f);
    assert_no_deck_observation_features(obs);
    assert_fair_information_boundary(obs);

    gwent_c_string checksum_a = {0};
    gwent_c_string checksum_b = {0};
    assert(gwent_rl_env_checksum(env, &checksum_a) == GWENT_C_OK);
    assert(checksum_a.data != 0);
    assert(checksum_a.size > 0);

    /* Product-side teacher rollouts must run on an independent clone. */
    gwent_rl_env* preview = gwent_rl_env_clone(env);
    assert(preview != 0);
    assert(gwent_rl_env_observation(preview)->option_count == obs->option_count);
    gwent_rl_step_result preview_step = {0};
    assert(gwent_rl_env_step_option(preview, 0, &preview_step) == GWENT_C_OK);
    gwent_c_string source_after_preview = {0};
    assert(gwent_rl_env_checksum(env, &source_after_preview) == GWENT_C_OK);
    assert(source_after_preview.size == checksum_a.size);
    for (size_t i = 0; i < checksum_a.size; ++i) {
        assert(source_after_preview.data[i] == checksum_a.data[i]);
    }
    gwent_c_string_free(&source_after_preview);
    gwent_rl_env_destroy(preview);

    gwent_rl_step_result reset_result = {0};
    assert(gwent_rl_env_reset(env, 1234, -1, &reset_result) == GWENT_C_OK);
    assert(reset_result.result_code == GWENT_C_OK);
    assert(reset_result.option_count > 0);
    assert(gwent_rl_env_checksum(env, &checksum_b) == GWENT_C_OK);
    assert(checksum_b.data != 0);
    assert(checksum_a.size == checksum_b.size);

    for (size_t i = 0; i < checksum_a.size; ++i) {
        assert(checksum_a.data[i] == checksum_b.data[i]);
    }

    gwent_c_string_free(&checksum_a);
    gwent_c_string_free(&checksum_b);
    gwent_rl_env_destroy(env);
}

static int observation_has_card(const gwent_rl_observation* obs, int card_id) {
    for (size_t i = 0; i < obs->object_count; ++i) {
        if (obs->object_card_ids[i] == card_id) {
            return 1;
        }
    }
    return 0;
}

static void test_rl_env_selects_deck_b(void) {
    gwent_rl_config config = gwent_rl_default_config();
    config.player0_deck_id = GWENT_RL_DECK_B;
    config.player1_deck_id = GWENT_RL_DECK_A;
    config.shuffle_decks = 0;
    gwent_rl_env* env = gwent_rl_env_create(&config);
    assert(env != 0);
    const gwent_rl_observation* obs = gwent_rl_env_observation(env);
    assert(observation_has_card(obs, 200055)); /* White Frost leader, deck B. */
    assert(observation_has_card(obs, 202185)); /* Blood Scent leader, deck A. */
    gwent_rl_env_destroy(env);

    config.player0_deck_id = 999;
    assert(gwent_rl_env_create(&config) == 0);
}



static void test_rl_oracle_mode_can_expose_opponent_hand_for_debug(void) {
    gwent_rl_config config = gwent_rl_default_config();
    config.include_private_info = 1;
    config.seed = 1234;
    gwent_rl_env* env = gwent_rl_env_create(&config);
    assert(env != 0);
    const gwent_rl_observation* obs = gwent_rl_env_observation(env);
    int opponent_hand_objects = 0;
    for (size_t i = 0; i < obs->object_count; ++i) {
        if (obs->object_zone_ids[i] == 1 && obs->object_owner_ids[i] != obs->actor_id) {
            ++opponent_hand_objects;
        }
    }
    assert(opponent_hand_objects == (int)obs->global_features[15]);
    gwent_rl_env_destroy(env);
}

static void test_rl_unit_play_uses_row_subdecision_and_prefix(void) {
    gwent_rl_config config = gwent_rl_default_config();
    config.seed = 777;
    config.enable_invariants = 1;

    gwent_rl_env* env = gwent_rl_env_create(&config);
    assert(env != 0);
    finish_opening_mulligans(env);

    const gwent_rl_observation* obs = gwent_rl_env_observation(env);
    assert(obs != 0);
    size_t play_index = (size_t)-1;
    int source_index = -1;
    for (size_t i = 0; i < obs->option_count; ++i) {
        if (obs->option_kind_ids[i] != GWENT_RL_OPTION_PLAY_CARD) {
            continue;
        }
        const int candidate_source = obs->option_source_object_indices[i];
        if (candidate_source < 0 || (size_t)candidate_source >= obs->object_count) {
            continue;
        }
        const float is_unit = obs->object_features[(size_t)candidate_source * GWENT_RL_OBJECT_FEATURE_COUNT + 22];
        if (is_unit > 0.5f) {
            play_index = i;
            source_index = candidate_source;
            break;
        }
    }
    assert(play_index != (size_t)-1);

    const int source_entity_id = obs->object_entity_ids[source_index];
    gwent_rl_step_result step = {0};
    assert(gwent_rl_env_step_option(env, play_index, &step) == GWENT_C_OK);
    assert(step.result_code == GWENT_C_OK);
    assert(step.action_status == GWENT_C_ACTION_APPLIED);

    obs = gwent_rl_env_observation(env);
    assert(obs != 0);
    assert(obs->decision_kind == GWENT_RL_DECISION_ROW_TARGET);
    assert(obs->source_entity_id == source_entity_id);
    assert(obs->prefix_count == 1);
    assert(obs->prefix_mask[0] == 1);
    assert(obs->prefix_kind_ids[0] == GWENT_RL_OPTION_PLAY_CARD);
    assert(obs->option_count > 0);
    for (size_t i = 0; i < obs->option_count; ++i) {
        assert(obs->option_kind_ids[i] == GWENT_RL_OPTION_CHOOSE_ROW);
        assert(obs->option_insert_positions[i] == -1);
    }

    assert(gwent_rl_env_step_option(env, 0, &step) == GWENT_C_OK);
    assert(step.action_status == GWENT_C_ACTION_APPLIED);
    obs = gwent_rl_env_observation(env);
    assert(obs->decision_kind == GWENT_RL_DECISION_INSERT_POSITION);
    assert(obs->prefix_count == 2);
    assert(obs->prefix_kind_ids[1] == GWENT_RL_OPTION_CHOOSE_ROW);
    assert(obs->option_count > 0);
    for (size_t i = 0; i < obs->option_count; ++i) {
        assert(obs->option_kind_ids[i] == GWENT_RL_OPTION_CHOOSE_INSERT_POSITION);
        assert(obs->option_insert_positions[i] == (int)i);
        if (i > 0) {
            assert(obs->option_stable_hashes[i] != obs->option_stable_hashes[i - 1]);
        }
    }
    assert(gwent_rl_env_step_option(env, obs->option_count - 1, &step) == GWENT_C_OK);
    assert(step.action_status == GWENT_C_ACTION_APPLIED);

    gwent_rl_env_destroy(env);
}

static void test_rl_env_clone_detaches_pending_resolution_frame(void) {
    gwent_rl_config config = gwent_rl_default_config();
    config.seed = 777;
    config.enable_invariants = 1;

    gwent_rl_env* env = gwent_rl_env_create(&config);
    assert(env != 0);
    finish_opening_mulligans(env);

    const gwent_rl_observation* obs = gwent_rl_env_observation(env);
    size_t play_index = (size_t)-1;
    for (size_t i = 0; i < obs->option_count; ++i) {
        if (obs->option_kind_ids[i] != GWENT_RL_OPTION_PLAY_CARD) {
            continue;
        }
        const int source_index = obs->option_source_object_indices[i];
        if (source_index < 0 || (size_t)source_index >= obs->object_count) {
            continue;
        }
        const float is_unit = obs->object_features[(size_t)source_index * GWENT_RL_OBJECT_FEATURE_COUNT + 22];
        if (is_unit > 0.5f) {
            play_index = i;
            break;
        }
    }
    assert(play_index != (size_t)-1);

    gwent_rl_step_result step = {0};
    assert(gwent_rl_env_step_option(env, play_index, &step) == GWENT_C_OK);
    obs = gwent_rl_env_observation(env);
    assert(obs->decision_kind == GWENT_RL_DECISION_ROW_TARGET);
    assert(obs->prefix_count == 1);

    gwent_c_string checksum_before = {0};
    assert(gwent_rl_env_checksum(env, &checksum_before) == GWENT_C_OK);

    /* Repeated Teacher-like previews must not append choices to the live
     * pending resolution. Before the clone fix, these copies shared the
     * ResolutionFrame and overflowed the live prefix after eight previews. */
    for (int preview_number = 0; preview_number < 10; ++preview_number) {
        gwent_rl_env* preview = gwent_rl_env_clone(env);
        assert(preview != 0);

        const gwent_rl_observation* preview_obs = gwent_rl_env_observation(preview);
        assert(preview_obs->decision_kind == GWENT_RL_DECISION_ROW_TARGET);
        assert(gwent_rl_env_step_option(preview, 0, &step) == GWENT_C_OK);

        preview_obs = gwent_rl_env_observation(preview);
        assert(preview_obs->decision_kind == GWENT_RL_DECISION_INSERT_POSITION);
        assert(gwent_rl_env_step_option(preview, 0, &step) == GWENT_C_OK);
        gwent_rl_env_destroy(preview);
    }

    obs = gwent_rl_env_observation(env);
    assert(obs->decision_kind == GWENT_RL_DECISION_ROW_TARGET);
    assert(obs->prefix_count == 1);
    gwent_c_string checksum_after = {0};
    assert(gwent_rl_env_checksum(env, &checksum_after) == GWENT_C_OK);
    assert(checksum_after.size == checksum_before.size);
    assert(memcmp(checksum_after.data, checksum_before.data, checksum_before.size) == 0);
    gwent_c_string_free(&checksum_after);
    gwent_c_string_free(&checksum_before);

    /* The live choice remains independently resumable after every preview. */
    assert(gwent_rl_env_step_option(env, 0, &step) == GWENT_C_OK);
    obs = gwent_rl_env_observation(env);
    assert(obs->decision_kind == GWENT_RL_DECISION_INSERT_POSITION);

    /* Clone from the next pending-choice phase as well.  This catches a
     * shallow copy introduced only after the row choice resumes the root
     * resolution and prepares the insert-position continuation. */
    gwent_c_string insert_checksum_before = {0};
    assert(gwent_rl_env_checksum(env, &insert_checksum_before) == GWENT_C_OK);
    for (int preview_number = 0; preview_number < 10; ++preview_number) {
        gwent_rl_env* preview = gwent_rl_env_clone(env);
        assert(preview != 0);
        const gwent_rl_observation* preview_obs = gwent_rl_env_observation(preview);
        assert(preview_obs->decision_kind == GWENT_RL_DECISION_INSERT_POSITION);
        assert(gwent_rl_env_step_option(preview, 0, &step) == GWENT_C_OK);
        gwent_rl_env_destroy(preview);
    }
    obs = gwent_rl_env_observation(env);
    assert(obs->decision_kind == GWENT_RL_DECISION_INSERT_POSITION);
    gwent_c_string insert_checksum_after = {0};
    assert(gwent_rl_env_checksum(env, &insert_checksum_after) == GWENT_C_OK);
    assert(insert_checksum_after.size == insert_checksum_before.size);
    assert(memcmp(insert_checksum_after.data, insert_checksum_before.data, insert_checksum_before.size) == 0);
    gwent_c_string_free(&insert_checksum_after);
    gwent_c_string_free(&insert_checksum_before);

    assert(gwent_rl_env_step_option(env, 0, &step) == GWENT_C_OK);
    gwent_rl_env_destroy(env);
}

static void test_rl_env_random_rollout(void) {
    gwent_rl_config config = gwent_rl_default_config();
    config.seed = 424242;
    config.enable_invariants = 1;

    gwent_rl_env* env = gwent_rl_env_create(&config);
    assert(env != 0);

    uint64_t rng = 99;
    int saw_terminal = 0;
    for (int step = 0; step < 512; ++step) {
        if (gwent_rl_env_done(env)) {
            saw_terminal = 1;
            break;
        }
        const gwent_rl_observation* rollout_obs = gwent_rl_env_observation(env);
        assert_no_deck_observation_features(rollout_obs);
        size_t n = gwent_rl_env_option_count(env);
        assert(n > 0);
        assert(n <= GWENT_RL_MAX_OPTIONS);
        const size_t pick = (size_t)(lcg_next(&rng) % n);
        gwent_rl_step_result result = {0};
        assert(gwent_rl_env_step_option(env, pick, &result) == GWENT_C_OK);
        assert(result.result_code == GWENT_C_OK);
        assert(result.action_status == GWENT_C_ACTION_APPLIED);
        const gwent_rl_observation* obs = gwent_rl_env_observation(env);
        assert(obs != 0);
        assert(obs->option_count == gwent_rl_env_option_count(env));
        assert(obs->object_count <= GWENT_RL_MAX_OBJECTS);
        assert(obs->option_count <= GWENT_RL_MAX_OPTIONS);
    }

    assert(saw_terminal || gwent_rl_env_done(env));
    const int winner = gwent_rl_env_winner_id(env);
    assert(winner == -1 || winner == 0 || winner == 1);
    gwent_rl_env_destroy(env);
}

static void test_rl_dense_reward_mode_can_emit_step_rewards(void) {
    gwent_rl_config config = gwent_rl_default_config();
    config.seed = 2026048;
    config.enable_invariants = 1;
    config.reward_config = gwent_rl_default_reward_config();
    config.reward_config.mode = GWENT_RL_REWARD_DENSE_SHAPING;
    config.reward_config.score_delta_weight = 0.25f;
    config.reward_config.damage_weight = 0.05f;
    config.reward_config.boost_weight = 0.02f;
    config.reward_config.max_step_reward_abs = 2.0f;

    gwent_rl_env* env = gwent_rl_env_create(&config);
    assert(env != 0);

    uint64_t rng = 2027;
    int saw_nonzero = 0;
    for (int step = 0; step < 256 && !gwent_rl_env_done(env); ++step) {
        size_t n = gwent_rl_env_option_count(env);
        assert(n > 0);
        const size_t pick = (size_t)(lcg_next(&rng) % n);
        gwent_rl_step_result result = {0};
        assert(gwent_rl_env_step_option(env, pick, &result) == GWENT_C_OK);
        assert(result.result_code == GWENT_C_OK);
        if (result.reward[0] != 0.0f || result.reward[1] != 0.0f) {
            saw_nonzero = 1;
            break;
        }
    }

    assert(saw_nonzero);
    gwent_rl_env_destroy(env);
}

int main(void) {
    assert(gwent_rl_decision_kind_name(GWENT_RL_DECISION_TURN) != 0);
    assert(gwent_rl_option_kind_name(GWENT_RL_OPTION_PLAY_CARD) != 0);
    assert(gwent_rl_option_kind_name(GWENT_RL_OPTION_CHOOSE_INSERT_POSITION) != 0);
    assert(gwent_rl_reward_mode_name(GWENT_RL_REWARD_DENSE_SHAPING) != 0);
    assert(gwent_rl_reward_mode_name(GWENT_RL_REWARD_STRATEGIC_SHAPING) != 0);
    {
        const gwent_rl_reward_config reward = gwent_rl_default_reward_config();
        assert(reward.mode == GWENT_RL_REWARD_TERMINAL_ONLY);
        assert(reward.round1_win_base > 0.0f);
        assert(reward.round2_card_cost_weight > reward.round1_card_cost_weight);
        assert(reward.close_round_win_margin_cap > 1);
    }
    test_rl_env_reset_and_observation();
    test_rl_env_selects_deck_b();
    test_rl_oracle_mode_can_expose_opponent_hand_for_debug();
    test_rl_unit_play_uses_row_subdecision_and_prefix();
    test_rl_env_clone_detaches_pending_resolution_frame();
    test_rl_env_random_rollout();
    test_rl_dense_reward_mode_can_emit_step_rewards();
    return 0;
}
