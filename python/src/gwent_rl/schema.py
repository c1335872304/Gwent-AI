"""Stable RL observation schema names for gwent-cpp-core.

Feature names and tensor dimensions live here. Mutable contract version numbers
come from the generated ``_contract_versions`` module, whose source of truth is
``config/rl_contract.json``. Breaking observation changes must bump the schema.
"""

from ._contract_versions import ACTION_GRAMMAR_VERSION, SCHEMA_VERSION

PREFIX_SEMANTICS = "ordered_full_resolution_prefix"
MAX_OBJECTS = 128
MAX_OPTIONS = 256
MAX_PREFIX = 16

GLOBAL_FEATURE_NAMES = [
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
]

OBJECT_FEATURE_NAMES = [
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
]

OPTION_FEATURE_NAMES = [
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
]

DECISION_KIND_NAMES = {
    0: "none",
    1: "turn",
    2: "mulligan",
    3: "card_target",
    4: "row_target",
    5: "finished",
    6: "insert_position",
}

OPTION_KIND_NAMES = {
    0: "unknown",
    1: "pass",
    2: "end_turn",
    3: "play_card",
    4: "mulligan",
    5: "use_leader",
    6: "use_order",
    7: "choose_card",
    8: "choose_row",
    9: "discard_card",
    10: "keep_hand",
    11: "choose_insert_position",
}
