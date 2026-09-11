#pragma once

#include <stdint.h>
#include <stddef.h>
#include "gwent/c/rl_contract_versions.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(GWENT_CORE_SHARED)
#  if defined(GWENT_CORE_BUILDING_LIBRARY)
#    define GWENT_C_API __declspec(dllexport)
#  else
#    define GWENT_C_API __declspec(dllimport)
#  endif
#else
#  define GWENT_C_API
#endif

typedef enum gwent_c_result_code {
    GWENT_C_OK = 0,
    GWENT_C_INVALID_ARGUMENT = 1,
    GWENT_C_OUT_OF_RANGE = 2,
    GWENT_C_EXCEPTION = 3,
    GWENT_C_ALLOCATION_FAILURE = 4,
    GWENT_C_STALE_DECISION = 5,
    GWENT_C_OPTION_OVERFLOW = 6,
    GWENT_C_OBJECT_OVERFLOW = 7,
    GWENT_C_PREFIX_OVERFLOW = 8
} gwent_c_result_code;

typedef enum gwent_c_invariant_policy {
    GWENT_C_INVARIANT_DISABLED = 0,
    GWENT_C_INVARIANT_BEFORE_APPLY = 1,
    GWENT_C_INVARIANT_AFTER_APPLY = 2,
    GWENT_C_INVARIANT_BEFORE_AND_AFTER_APPLY = 3
} gwent_c_invariant_policy;

typedef enum gwent_c_action_status {
    GWENT_C_ACTION_APPLIED = 0,
    GWENT_C_ACTION_OK = GWENT_C_ACTION_APPLIED,
    GWENT_C_ACTION_ILLEGAL_ACTION = 1,
    GWENT_C_ACTION_INVALID_PLAYER = 2,
    GWENT_C_ACTION_NOT_CURRENT_PLAYER = 3,
    GWENT_C_ACTION_INVALID_PHASE = 4,
    GWENT_C_ACTION_INVALID_SOURCE = 5,
    GWENT_C_ACTION_INVALID_TARGET = 6,
    GWENT_C_ACTION_EMPTY_DECK = 7,
    GWENT_C_ACTION_UNSUPPORTED_ACTION = 8,
    GWENT_C_ACTION_TASK_LIMIT_EXCEEDED = 9,
    GWENT_C_ACTION_INVARIANT_VIOLATION = 10,
    GWENT_C_ACTION_UNKNOWN = 255
} gwent_c_action_status;

typedef struct gwent_c_string {
    char* data;
    size_t size;
} gwent_c_string;

typedef struct gwent_deck_a_match_config {
    uint64_t seed;
    int starting_player_id; /* -1 means use default deterministic setup. */
    int shuffle_decks;      /* 0 = false, non-zero = true. */
    gwent_c_invariant_policy invariant_policy;
} gwent_deck_a_match_config;

typedef struct gwent_deck_a_game gwent_deck_a_game;

GWENT_C_API gwent_deck_a_match_config gwent_deck_a_default_config(void);

GWENT_C_API gwent_deck_a_game* gwent_deck_a_game_create(const gwent_deck_a_match_config* config);
GWENT_C_API void gwent_deck_a_game_destroy(gwent_deck_a_game* game);

GWENT_C_API int gwent_deck_a_game_is_finished(const gwent_deck_a_game* game);
GWENT_C_API int gwent_deck_a_game_winner(const gwent_deck_a_game* game); /* -1 when absent or invalid. */

GWENT_C_API size_t gwent_deck_a_game_legal_action_count(const gwent_deck_a_game* game);
GWENT_C_API gwent_c_result_code gwent_deck_a_game_legal_actions_json(const gwent_deck_a_game* game, gwent_c_string* out_json);
GWENT_C_API gwent_c_result_code gwent_deck_a_game_snapshot_json(const gwent_deck_a_game* game, gwent_c_string* out_json);
GWENT_C_API gwent_c_result_code gwent_deck_a_game_checksum(const gwent_deck_a_game* game, gwent_c_string* out_checksum);

GWENT_C_API gwent_c_result_code gwent_deck_a_game_apply_legal_action_at(
    gwent_deck_a_game* game,
    size_t legal_action_index,
    gwent_c_action_status* out_status);


/* --- RL environment C adapter -------------------------------------------------
 * This is a low-allocation environment surface for Python/CFFI/pybind callers.
 * It deliberately does not expose Kernel/GameState internals and does not use
 * JSON on the rollout hot path. Returned observation pointers are owned by the
 * env and remain valid until the next reset/step/destroy call on that env.
 */

#define GWENT_RL_GLOBAL_FEATURE_COUNT 34
#define GWENT_RL_MAX_OBJECTS 128
#define GWENT_RL_OBJECT_FEATURE_COUNT 27
#define GWENT_RL_KNOWLEDGE_ZONE_PUBLIC_DECKLIST 8
#define GWENT_RL_MAX_OPTIONS 256
#define GWENT_RL_OPTION_FEATURE_COUNT 16
#define GWENT_RL_MAX_PREFIX 16

typedef enum gwent_rl_decision_kind {
    GWENT_RL_DECISION_NONE = 0,
    GWENT_RL_DECISION_TURN = 1,
    GWENT_RL_DECISION_MULLIGAN = 2,
    GWENT_RL_DECISION_CARD_TARGET = 3,
    GWENT_RL_DECISION_ROW_TARGET = 4,
    GWENT_RL_DECISION_FINISHED = 5,
    GWENT_RL_DECISION_INSERT_POSITION = 6
} gwent_rl_decision_kind;

typedef enum gwent_rl_option_kind {
    GWENT_RL_OPTION_UNKNOWN = 0,
    GWENT_RL_OPTION_PASS = 1,
    GWENT_RL_OPTION_END_TURN = 2,
    GWENT_RL_OPTION_PLAY_CARD = 3,
    GWENT_RL_OPTION_DISCARD_CARD = 9,
    GWENT_RL_OPTION_MULLIGAN = 4,
    GWENT_RL_OPTION_USE_LEADER = 5,
    GWENT_RL_OPTION_USE_ORDER = 6,
    GWENT_RL_OPTION_CHOOSE_CARD = 7,
    GWENT_RL_OPTION_CHOOSE_ROW = 8,
    GWENT_RL_OPTION_KEEP_HAND = 10,
    GWENT_RL_OPTION_CHOOSE_INSERT_POSITION = 11
} gwent_rl_option_kind;


typedef enum gwent_rl_reward_mode {
    GWENT_RL_REWARD_TERMINAL_ONLY = 0,
    GWENT_RL_REWARD_ROUND_SHAPING = 1,
    GWENT_RL_REWARD_DENSE_SHAPING = 2,
    /* Gwent-specific strategic shaping: reward round efficiency/resources,
       not raw damage/boost events. Match outcome remains the dominant signal. */
    GWENT_RL_REWARD_STRATEGIC_SHAPING = 3
} gwent_rl_reward_mode;

typedef struct gwent_rl_reward_config {
    int mode;

    float final_win;
    float final_loss;
    float final_draw;

    float round_win;
    float round_loss;
    float round_draw;

    float score_delta_weight;
    float damage_weight;
    float boost_weight;
    float armor_weight;
    float status_weight;
    float removal_weight;
    float card_play_cost;
    float leader_use_cost;
    float order_use_cost;
    float discard_card_cost;
    float secured_pass_reward;
    float secured_overplay_cost;
    float hand_delta_round_weight;
    int score_delta_contested_only;

    /* Strategic shaping (mode=GWENT_RL_REWARD_STRATEGIC_SHAPING).
       card_cost = winner cards spent this round - loser cards spent this round.
       Positive card_cost means the winner paid card disadvantage to win. */
    float round1_win_base;
    float round1_card_cost_weight;
    float round1_excess_card_penalty;
    float round2_win_base;
    float round2_card_cost_weight;
    float round2_excess_card_penalty;
    float close_round_win_bonus;
    int close_round_win_margin_cap;

    float max_step_reward_abs;
} gwent_rl_reward_config;

typedef struct gwent_rl_config {
    uint64_t seed;
    int starting_player_id;      /* -1 means use default deterministic setup. */
    int shuffle_decks;           /* 0 = false, non-zero = true. */
    int enable_invariants;       /* 0 = disabled, non-zero = AfterApply. */
    int current_player_perspective; /* reserved for stable tensor perspective; defaults to 1. */
    int include_private_info;    /* 0=fair tournament observation (default); nonzero=oracle/debug opponent-hand visibility. */
    gwent_rl_reward_config reward_config;
    int player0_deck_id;        /* GWENT_RL_DECK_A or GWENT_RL_DECK_B. */
    int player1_deck_id;        /* GWENT_RL_DECK_A or GWENT_RL_DECK_B. */
} gwent_rl_config;

typedef enum gwent_rl_deck_id {
    GWENT_RL_DECK_A = 0,
    GWENT_RL_DECK_B = 1
} gwent_rl_deck_id;

typedef struct gwent_rl_step_result {
    gwent_c_result_code result_code;
    gwent_c_action_status action_status;
    int done;
    int actor_id;       /* player expected to act after the step, or -1 when done/invalid. */
    int winner_id;      /* -1 when no winner yet or draw/unknown. */
    float reward[2];    /* player-indexed RL reward according to gwent_rl_reward_config. */
    size_t option_count;
} gwent_rl_step_result;

typedef struct gwent_rl_observation {
    int schema_version;
    int actor_id;
    int opponent_id;
    int perspective_player_id;
    int decision_kind;
    int done;
    int winner_id;
    int source_object_index;
    int source_entity_id;
    int source_card_id;
    size_t object_count;
    size_t option_count;
    size_t prefix_count;

    float global_features[GWENT_RL_GLOBAL_FEATURE_COUNT];

    int object_entity_ids[GWENT_RL_MAX_OBJECTS];
    int object_card_ids[GWENT_RL_MAX_OBJECTS];
    int object_owner_ids[GWENT_RL_MAX_OBJECTS];
    int object_controller_ids[GWENT_RL_MAX_OBJECTS];
    int object_zone_ids[GWENT_RL_MAX_OBJECTS];
    int object_row_ids[GWENT_RL_MAX_OBJECTS];
    int object_slot_indices[GWENT_RL_MAX_OBJECTS];
    unsigned char object_mask[GWENT_RL_MAX_OBJECTS];
    float object_features[GWENT_RL_MAX_OBJECTS * GWENT_RL_OBJECT_FEATURE_COUNT];

    int option_kind_ids[GWENT_RL_MAX_OPTIONS];
    int option_card_ids[GWENT_RL_MAX_OPTIONS];
    int option_source_object_indices[GWENT_RL_MAX_OPTIONS];
    int option_target_object_indices[GWENT_RL_MAX_OPTIONS];
    int option_target_side_ids[GWENT_RL_MAX_OPTIONS];
    int option_target_zone_ids[GWENT_RL_MAX_OPTIONS];
    int option_target_row_ids[GWENT_RL_MAX_OPTIONS];
    int option_insert_positions[GWENT_RL_MAX_OPTIONS]; /* -1 except CHOOSE_INSERT_POSITION / concrete PLAY. */
    int option_hand_slot_indices[GWENT_RL_MAX_OPTIONS];
    uint64_t option_stable_hashes[GWENT_RL_MAX_OPTIONS];
    unsigned char option_mask[GWENT_RL_MAX_OPTIONS];
    float option_features[GWENT_RL_MAX_OPTIONS * GWENT_RL_OPTION_FEATURE_COUNT];

    int prefix_kind_ids[GWENT_RL_MAX_PREFIX];
    int prefix_source_object_indices[GWENT_RL_MAX_PREFIX];
    int prefix_target_object_indices[GWENT_RL_MAX_PREFIX];
    int prefix_card_ids[GWENT_RL_MAX_PREFIX];
    int prefix_row_ids[GWENT_RL_MAX_PREFIX];
    int prefix_insert_positions[GWENT_RL_MAX_PREFIX];
    unsigned char prefix_mask[GWENT_RL_MAX_PREFIX];
} gwent_rl_observation;

typedef struct gwent_rl_env gwent_rl_env;
typedef struct gwent_rl_collector gwent_rl_collector;

typedef struct gwent_rl_collector_config {
    size_t num_envs;
    size_t max_batch_size;
    uint64_t base_seed;
    int starting_player_id;      /* -1 means use default deterministic setup. */
    int shuffle_decks;           /* 0 = false, non-zero = true. */
    int auto_reset_done_envs;    /* 0 = keep terminal envs out of batches. */
    int enable_invariants;       /* 0 = disabled, non-zero = AfterApply. */
    int current_player_perspective;
    int include_private_info;
    gwent_rl_reward_config reward_config;
    int player0_deck_id;        /* GWENT_RL_DECK_A or GWENT_RL_DECK_B. */
    int player1_deck_id;        /* GWENT_RL_DECK_A or GWENT_RL_DECK_B. */
} gwent_rl_collector_config;

typedef struct gwent_rl_matchup_config {
    int player0_deck_id;        /* Index into generated supported_deck_ids(). */
    int player1_deck_id;
    uint32_t weight;            /* Positive deterministic sampling weight. */
} gwent_rl_matchup_config;

typedef struct gwent_rl_batch_item {
    size_t env_index;
    uint64_t decision_serial;
    int actor_id;
    int decision_kind;
    int done;
    int winner_id;
    int source_object_index;
    int source_entity_id;
    int source_card_id;
    size_t object_count;
    size_t option_count;
    size_t prefix_count;
} gwent_rl_batch_item;

typedef struct gwent_rl_inference_batch {
    size_t count;
    size_t max_count;

    const gwent_rl_batch_item* items;

    const float* global_features;        /* [B, GWENT_RL_GLOBAL_FEATURE_COUNT] */
    const int* object_entity_ids;        /* [B, GWENT_RL_MAX_OBJECTS] */
    const int* object_card_ids;          /* [B, GWENT_RL_MAX_OBJECTS] */
    const int* object_owner_ids;         /* [B, GWENT_RL_MAX_OBJECTS] */
    const int* object_controller_ids;    /* [B, GWENT_RL_MAX_OBJECTS] */
    const int* object_zone_ids;          /* [B, GWENT_RL_MAX_OBJECTS] */
    const int* object_row_ids;           /* [B, GWENT_RL_MAX_OBJECTS] */
    const int* object_slot_indices;      /* [B, GWENT_RL_MAX_OBJECTS] */
    const unsigned char* object_mask;    /* [B, GWENT_RL_MAX_OBJECTS] */
    const float* object_features;        /* [B, GWENT_RL_MAX_OBJECTS, GWENT_RL_OBJECT_FEATURE_COUNT] */

    const int* option_kind_ids;          /* [B, GWENT_RL_MAX_OPTIONS] */
    const int* option_card_ids;          /* [B, GWENT_RL_MAX_OPTIONS] */
    const int* option_source_object_indices; /* [B, GWENT_RL_MAX_OPTIONS] */
    const int* option_target_object_indices; /* [B, GWENT_RL_MAX_OPTIONS] */
    const int* option_target_side_ids;   /* [B, GWENT_RL_MAX_OPTIONS] */
    const int* option_target_zone_ids;   /* [B, GWENT_RL_MAX_OPTIONS] */
    const int* option_target_row_ids;    /* [B, GWENT_RL_MAX_OPTIONS] */
    const int* option_insert_positions;  /* [B, GWENT_RL_MAX_OPTIONS] */
    const int* option_hand_slot_indices; /* [B, GWENT_RL_MAX_OPTIONS] */
    const uint64_t* option_stable_hashes;/* [B, GWENT_RL_MAX_OPTIONS] */
    const unsigned char* option_mask;    /* [B, GWENT_RL_MAX_OPTIONS] */
    const float* option_features;        /* [B, GWENT_RL_MAX_OPTIONS, GWENT_RL_OPTION_FEATURE_COUNT] */

    const int* prefix_kind_ids;          /* [B, GWENT_RL_MAX_PREFIX] */
    const int* prefix_source_object_indices; /* [B, GWENT_RL_MAX_PREFIX] */
    const int* prefix_target_object_indices; /* [B, GWENT_RL_MAX_PREFIX] */
    const int* prefix_card_ids;          /* [B, GWENT_RL_MAX_PREFIX] */
    const int* prefix_row_ids;           /* [B, GWENT_RL_MAX_PREFIX] */
    const int* prefix_insert_positions;  /* [B, GWENT_RL_MAX_PREFIX] */
    const unsigned char* prefix_mask;    /* [B, GWENT_RL_MAX_PREFIX] */
} gwent_rl_inference_batch;

typedef struct gwent_rl_batch_action {
    size_t env_index;
    uint64_t decision_serial;
    size_t option_index;
} gwent_rl_batch_action;

typedef struct gwent_rl_batch_step_result {
    size_t env_index;
    uint64_t decision_serial;
    gwent_c_result_code result_code;
    gwent_c_action_status action_status;
    int done;
    int actor_id;
    int winner_id;
    float reward[2];
    size_t option_count;
} gwent_rl_batch_step_result;

typedef struct gwent_rl_apply_result_batch {
    size_t count;
    const gwent_rl_batch_step_result* results;
} gwent_rl_apply_result_batch;

/* Counterfactual PASS classification for diagnostics/evaluation only.
   The live environment is never mutated. outcome is relative to the current actor. */
typedef enum gwent_rl_pass_outcome {
    GWENT_RL_PASS_NOT_APPLICABLE = 0,
    GWENT_RL_PASS_WIN = 1,
    GWENT_RL_PASS_DRAW = 2,
    GWENT_RL_PASS_LOSS = 3
} gwent_rl_pass_outcome;

typedef struct gwent_rl_pass_probe_result {
    int applicable;
    int actor_id;
    int outcome;
    int actor_hand_count;
    int opponent_hand_count;
    int hand_diff_actor_minus_opponent;
} gwent_rl_pass_probe_result;

GWENT_C_API gwent_rl_reward_config gwent_rl_default_reward_config(void);
GWENT_C_API gwent_rl_config gwent_rl_default_config(void);
GWENT_C_API gwent_rl_collector_config gwent_rl_default_collector_config(void);
GWENT_C_API gwent_rl_env* gwent_rl_env_create(const gwent_rl_config* config);
/* Creates an independent copy of an RL environment for read-only product
 * simulations. Stepping the clone never mutates the source environment. */
GWENT_C_API gwent_rl_env* gwent_rl_env_clone(const gwent_rl_env* source);
GWENT_C_API void gwent_rl_env_destroy(gwent_rl_env* env);

GWENT_C_API gwent_c_result_code gwent_rl_env_reset(
    gwent_rl_env* env,
    uint64_t seed,
    int starting_player_id,
    gwent_rl_step_result* out_result);

GWENT_C_API gwent_c_result_code gwent_rl_env_step_option(
    gwent_rl_env* env,
    size_t option_index,
    gwent_rl_step_result* out_result);

GWENT_C_API const gwent_rl_observation* gwent_rl_env_observation(const gwent_rl_env* env);
GWENT_C_API size_t gwent_rl_env_option_count(const gwent_rl_env* env);
GWENT_C_API int gwent_rl_env_actor_id(const gwent_rl_env* env);
GWENT_C_API int gwent_rl_env_done(const gwent_rl_env* env);
GWENT_C_API int gwent_rl_env_winner_id(const gwent_rl_env* env);
/* Product/debug introspection only: returns the remaining duration for an active
 * battle-row effect. row_id uses 0=Melee, 1=Ranged. Returns -1 for invalid
 * arguments and 0 when the requested effect is not active. This is deliberately
 * separate from the RL observation schema so UI visibility does not change the
 * training contract. */
GWENT_C_API int gwent_rl_env_row_effect_duration(
    const gwent_rl_env* env,
    int player_id,
    int row_id,
    const char* effect_id);
GWENT_C_API gwent_c_result_code gwent_rl_env_checksum(const gwent_rl_env* env, gwent_c_string* out_checksum);

GWENT_C_API gwent_rl_collector* gwent_rl_collector_create(const gwent_rl_collector_config* config);
GWENT_C_API gwent_rl_collector* gwent_rl_collector_create_with_matchups(
    const gwent_rl_collector_config* config,
    const gwent_rl_matchup_config* matchups,
    size_t matchup_count);
GWENT_C_API void gwent_rl_collector_destroy(gwent_rl_collector* collector);
GWENT_C_API gwent_c_result_code gwent_rl_collector_set_game_limit(
    gwent_rl_collector* collector,
    size_t max_games);

GWENT_C_API gwent_c_result_code gwent_rl_collector_collect(
    gwent_rl_collector* collector,
    gwent_rl_inference_batch* out_batch);

GWENT_C_API gwent_c_result_code gwent_rl_collector_apply_actions(
    gwent_rl_collector* collector,
    const gwent_rl_batch_action* actions,
    size_t action_count,
    gwent_rl_apply_result_batch* out_results);

GWENT_C_API gwent_c_result_code gwent_rl_collector_probe_pass(
    const gwent_rl_collector* collector,
    size_t env_index,
    gwent_rl_pass_probe_result* out_probe);

GWENT_C_API size_t gwent_rl_collector_env_count(const gwent_rl_collector* collector);
GWENT_C_API size_t gwent_rl_collector_completed_episodes(const gwent_rl_collector* collector);
GWENT_C_API size_t gwent_rl_collector_total_steps(const gwent_rl_collector* collector);
GWENT_C_API size_t gwent_rl_collector_total_resets(const gwent_rl_collector* collector);

GWENT_C_API const char* gwent_rl_decision_kind_name(gwent_rl_decision_kind kind);
GWENT_C_API const char* gwent_rl_option_kind_name(gwent_rl_option_kind kind);
GWENT_C_API const char* gwent_rl_reward_mode_name(gwent_rl_reward_mode mode);

GWENT_C_API uint32_t gwent_rl_schema_version(void);
GWENT_C_API uint32_t gwent_rl_action_grammar_version(void);
GWENT_C_API uint32_t gwent_rl_reward_config_version(void);
GWENT_C_API size_t gwent_rl_global_feature_count(void);
GWENT_C_API size_t gwent_rl_max_objects(void);
GWENT_C_API size_t gwent_rl_object_feature_count(void);
GWENT_C_API size_t gwent_rl_max_options(void);
GWENT_C_API size_t gwent_rl_option_feature_count(void);
GWENT_C_API size_t gwent_rl_max_prefix(void);
GWENT_C_API const char* gwent_rl_global_feature_name(size_t index);
GWENT_C_API const char* gwent_rl_object_feature_name(size_t index);
GWENT_C_API const char* gwent_rl_option_feature_name(size_t index);

GWENT_C_API void gwent_c_string_free(gwent_c_string* value);
GWENT_C_API const char* gwent_c_result_code_name(gwent_c_result_code code);
GWENT_C_API const char* gwent_c_action_status_name(gwent_c_action_status status);

#ifdef __cplusplus
}  /* extern "C" */
#endif
