from __future__ import annotations

import ctypes as ct
import os
import platform
from ctypes.util import find_library
from pathlib import Path
from typing import Optional

from ._contract_versions import REWARD_CONFIG_VERSION
from .schema import (
    ACTION_GRAMMAR_VERSION,
    GLOBAL_FEATURE_NAMES,
    MAX_OBJECTS,
    MAX_OPTIONS,
    MAX_PREFIX,
    OBJECT_FEATURE_NAMES,
    OPTION_FEATURE_NAMES,
    PREFIX_SEMANTICS,
    SCHEMA_VERSION,
)


# Keep the public ctypes names stable while deriving them from the single
# Python-side schema mirror in gwent_rl.schema.
GWENT_RL_SCHEMA_VERSION = SCHEMA_VERSION
GWENT_RL_REWARD_CONFIG_VERSION = REWARD_CONFIG_VERSION
GWENT_RL_ACTION_GRAMMAR_VERSION = ACTION_GRAMMAR_VERSION
GWENT_RL_PREFIX_SEMANTICS = PREFIX_SEMANTICS
GWENT_RL_GLOBAL_FEATURE_COUNT = len(GLOBAL_FEATURE_NAMES)
GWENT_RL_MAX_OBJECTS = MAX_OBJECTS
GWENT_RL_OBJECT_FEATURE_COUNT = len(OBJECT_FEATURE_NAMES)
GWENT_RL_MAX_OPTIONS = MAX_OPTIONS
GWENT_RL_OPTION_FEATURE_COUNT = len(OPTION_FEATURE_NAMES)
GWENT_RL_MAX_PREFIX = MAX_PREFIX

GWENT_C_OK = 0
GWENT_C_STALE_DECISION = 5
GWENT_C_OPTION_OVERFLOW = 6
GWENT_C_OBJECT_OVERFLOW = 7
GWENT_C_PREFIX_OVERFLOW = 8

GWENT_RL_REWARD_TERMINAL_ONLY = 0
GWENT_RL_REWARD_ROUND_SHAPING = 1
GWENT_RL_REWARD_DENSE_SHAPING = 2
GWENT_RL_REWARD_STRATEGIC_SHAPING = 3

GWENT_RL_DECK_A = 0
GWENT_RL_DECK_B = 1

GWENT_RL_PASS_NOT_APPLICABLE = 0
GWENT_RL_PASS_WIN = 1
GWENT_RL_PASS_DRAW = 2
GWENT_RL_PASS_LOSS = 3


class CResultCode:
    OK = 0
    INVALID_ARGUMENT = 1
    OUT_OF_RANGE = 2
    EXCEPTION = 3
    ALLOCATION_FAILURE = 4
    STALE_DECISION = 5
    OPTION_OVERFLOW = 6
    OBJECT_OVERFLOW = 7
    PREFIX_OVERFLOW = 8


class GwentRlRewardConfig(ct.Structure):
    _fields_ = [
        ("mode", ct.c_int),
        ("final_win", ct.c_float),
        ("final_loss", ct.c_float),
        ("final_draw", ct.c_float),
        ("round_win", ct.c_float),
        ("round_loss", ct.c_float),
        ("round_draw", ct.c_float),
        ("score_delta_weight", ct.c_float),
        ("damage_weight", ct.c_float),
        ("boost_weight", ct.c_float),
        ("armor_weight", ct.c_float),
        ("status_weight", ct.c_float),
        ("removal_weight", ct.c_float),
        ("card_play_cost", ct.c_float),
        ("leader_use_cost", ct.c_float),
        ("order_use_cost", ct.c_float),
        ("discard_card_cost", ct.c_float),
        ("secured_pass_reward", ct.c_float),
        ("secured_overplay_cost", ct.c_float),
        ("hand_delta_round_weight", ct.c_float),
        ("score_delta_contested_only", ct.c_int),
        ("round1_win_base", ct.c_float),
        ("round1_card_cost_weight", ct.c_float),
        ("round1_excess_card_penalty", ct.c_float),
        ("round2_win_base", ct.c_float),
        ("round2_card_cost_weight", ct.c_float),
        ("round2_excess_card_penalty", ct.c_float),
        ("close_round_win_bonus", ct.c_float),
        ("close_round_win_margin_cap", ct.c_int),
        ("max_step_reward_abs", ct.c_float),
    ]


class GwentRlConfig(ct.Structure):
    """Mirror of the single-environment ``gwent_rl_config`` C ABI struct.

    Keep this definition beside the collector types.  Product-side inference
    adapters import it instead of maintaining a second ctypes layout.
    """

    _fields_ = [
        ("seed", ct.c_uint64),
        ("starting_player_id", ct.c_int),
        ("shuffle_decks", ct.c_int),
        ("enable_invariants", ct.c_int),
        ("current_player_perspective", ct.c_int),
        ("include_private_info", ct.c_int),
        ("reward_config", GwentRlRewardConfig),
        ("player0_deck_id", ct.c_int),
        ("player1_deck_id", ct.c_int),
    ]


class GwentRlStepResult(ct.Structure):
    """Mirror of the single-environment ``gwent_rl_step_result`` C ABI struct."""

    _fields_ = [
        ("result_code", ct.c_int),
        ("action_status", ct.c_int),
        ("done", ct.c_int),
        ("actor_id", ct.c_int),
        ("winner_id", ct.c_int),
        ("reward", ct.c_float * 2),
        ("option_count", ct.c_size_t),
    ]


class GwentRlObservation(ct.Structure):
    """Mirror of the fixed-size ``gwent_rl_observation`` C ABI struct."""

    _fields_ = [
        ("schema_version", ct.c_int),
        ("actor_id", ct.c_int),
        ("opponent_id", ct.c_int),
        ("perspective_player_id", ct.c_int),
        ("decision_kind", ct.c_int),
        ("done", ct.c_int),
        ("winner_id", ct.c_int),
        ("source_object_index", ct.c_int),
        ("source_entity_id", ct.c_int),
        ("source_card_id", ct.c_int),
        ("object_count", ct.c_size_t),
        ("option_count", ct.c_size_t),
        ("prefix_count", ct.c_size_t),
        ("global_features", ct.c_float * GWENT_RL_GLOBAL_FEATURE_COUNT),
        ("object_entity_ids", ct.c_int * GWENT_RL_MAX_OBJECTS),
        ("object_card_ids", ct.c_int * GWENT_RL_MAX_OBJECTS),
        ("object_owner_ids", ct.c_int * GWENT_RL_MAX_OBJECTS),
        ("object_controller_ids", ct.c_int * GWENT_RL_MAX_OBJECTS),
        ("object_zone_ids", ct.c_int * GWENT_RL_MAX_OBJECTS),
        ("object_row_ids", ct.c_int * GWENT_RL_MAX_OBJECTS),
        ("object_slot_indices", ct.c_int * GWENT_RL_MAX_OBJECTS),
        ("object_mask", ct.c_ubyte * GWENT_RL_MAX_OBJECTS),
        ("object_features", ct.c_float * (GWENT_RL_MAX_OBJECTS * GWENT_RL_OBJECT_FEATURE_COUNT)),
        ("option_kind_ids", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_card_ids", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_source_object_indices", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_target_object_indices", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_target_side_ids", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_target_zone_ids", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_target_row_ids", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_insert_positions", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_hand_slot_indices", ct.c_int * GWENT_RL_MAX_OPTIONS),
        ("option_stable_hashes", ct.c_uint64 * GWENT_RL_MAX_OPTIONS),
        ("option_mask", ct.c_ubyte * GWENT_RL_MAX_OPTIONS),
        ("option_features", ct.c_float * (GWENT_RL_MAX_OPTIONS * GWENT_RL_OPTION_FEATURE_COUNT)),
        ("prefix_kind_ids", ct.c_int * GWENT_RL_MAX_PREFIX),
        ("prefix_source_object_indices", ct.c_int * GWENT_RL_MAX_PREFIX),
        ("prefix_target_object_indices", ct.c_int * GWENT_RL_MAX_PREFIX),
        ("prefix_card_ids", ct.c_int * GWENT_RL_MAX_PREFIX),
        ("prefix_row_ids", ct.c_int * GWENT_RL_MAX_PREFIX),
        ("prefix_insert_positions", ct.c_int * GWENT_RL_MAX_PREFIX),
        ("prefix_mask", ct.c_ubyte * GWENT_RL_MAX_PREFIX),
    ]


class GwentRlCollectorConfig(ct.Structure):
    _fields_ = [
        ("num_envs", ct.c_size_t),
        ("max_batch_size", ct.c_size_t),
        ("base_seed", ct.c_uint64),
        ("starting_player_id", ct.c_int),
        ("shuffle_decks", ct.c_int),
        ("auto_reset_done_envs", ct.c_int),
        ("enable_invariants", ct.c_int),
        ("current_player_perspective", ct.c_int),
        ("include_private_info", ct.c_int),
        ("reward_config", GwentRlRewardConfig),
        ("player0_deck_id", ct.c_int),
        ("player1_deck_id", ct.c_int),
    ]


class GwentRlMatchupConfig(ct.Structure):
    _fields_ = [
        ("player0_deck_id", ct.c_int),
        ("player1_deck_id", ct.c_int),
        ("weight", ct.c_uint32),
    ]


class GwentRlBatchItem(ct.Structure):
    _fields_ = [
        ("env_index", ct.c_size_t),
        ("decision_serial", ct.c_uint64),
        ("actor_id", ct.c_int),
        ("decision_kind", ct.c_int),
        ("done", ct.c_int),
        ("winner_id", ct.c_int),
        ("source_object_index", ct.c_int),
        ("source_entity_id", ct.c_int),
        ("source_card_id", ct.c_int),
        ("object_count", ct.c_size_t),
        ("option_count", ct.c_size_t),
        ("prefix_count", ct.c_size_t),
    ]


class GwentRlInferenceBatch(ct.Structure):
    _fields_ = [
        ("count", ct.c_size_t),
        ("max_count", ct.c_size_t),
        ("items", ct.POINTER(GwentRlBatchItem)),
        ("global_features", ct.POINTER(ct.c_float)),
        ("object_entity_ids", ct.POINTER(ct.c_int)),
        ("object_card_ids", ct.POINTER(ct.c_int)),
        ("object_owner_ids", ct.POINTER(ct.c_int)),
        ("object_controller_ids", ct.POINTER(ct.c_int)),
        ("object_zone_ids", ct.POINTER(ct.c_int)),
        ("object_row_ids", ct.POINTER(ct.c_int)),
        ("object_slot_indices", ct.POINTER(ct.c_int)),
        ("object_mask", ct.POINTER(ct.c_ubyte)),
        ("object_features", ct.POINTER(ct.c_float)),
        ("option_kind_ids", ct.POINTER(ct.c_int)),
        ("option_card_ids", ct.POINTER(ct.c_int)),
        ("option_source_object_indices", ct.POINTER(ct.c_int)),
        ("option_target_object_indices", ct.POINTER(ct.c_int)),
        ("option_target_side_ids", ct.POINTER(ct.c_int)),
        ("option_target_zone_ids", ct.POINTER(ct.c_int)),
        ("option_target_row_ids", ct.POINTER(ct.c_int)),
        ("option_insert_positions", ct.POINTER(ct.c_int)),
        ("option_hand_slot_indices", ct.POINTER(ct.c_int)),
        ("option_stable_hashes", ct.POINTER(ct.c_uint64)),
        ("option_mask", ct.POINTER(ct.c_ubyte)),
        ("option_features", ct.POINTER(ct.c_float)),
        ("prefix_kind_ids", ct.POINTER(ct.c_int)),
        ("prefix_source_object_indices", ct.POINTER(ct.c_int)),
        ("prefix_target_object_indices", ct.POINTER(ct.c_int)),
        ("prefix_card_ids", ct.POINTER(ct.c_int)),
        ("prefix_row_ids", ct.POINTER(ct.c_int)),
        ("prefix_insert_positions", ct.POINTER(ct.c_int)),
        ("prefix_mask", ct.POINTER(ct.c_ubyte)),
    ]


class GwentRlBatchAction(ct.Structure):
    _fields_ = [
        ("env_index", ct.c_size_t),
        ("decision_serial", ct.c_uint64),
        ("option_index", ct.c_size_t),
    ]


class GwentRlBatchStepResult(ct.Structure):
    _fields_ = [
        ("env_index", ct.c_size_t),
        ("decision_serial", ct.c_uint64),
        ("result_code", ct.c_int),
        ("action_status", ct.c_int),
        ("done", ct.c_int),
        ("actor_id", ct.c_int),
        ("winner_id", ct.c_int),
        ("reward", ct.c_float * 2),
        ("option_count", ct.c_size_t),
    ]


class GwentRlApplyResultBatch(ct.Structure):
    _fields_ = [
        ("count", ct.c_size_t),
        ("results", ct.POINTER(GwentRlBatchStepResult)),
    ]


class GwentRlPassProbeResult(ct.Structure):
    _fields_ = [
        ("applicable", ct.c_int),
        ("actor_id", ct.c_int),
        ("outcome", ct.c_int),
        ("actor_hand_count", ct.c_int),
        ("opponent_hand_count", ct.c_int),
        ("hand_diff_actor_minus_opponent", ct.c_int),
    ]


class GwentCString(ct.Structure):
    _fields_ = [("data", ct.c_char_p), ("size", ct.c_size_t)]


def _platform_library_names() -> list[str]:
    system = platform.system().lower()
    if system == "windows":
        return ["gwent_core.dll", "libgwent_core.dll"]
    if system == "darwin":
        return ["libgwent_core.dylib"]
    return ["libgwent_core.so"]


def candidate_library_paths(explicit_path: Optional[str] = None) -> list[Path]:
    candidates: list[Path] = []
    if explicit_path:
        candidates.append(Path(explicit_path))
    env_path = os.environ.get("GWENT_CORE_LIBRARY")
    if env_path:
        candidates.append(Path(env_path))

    here = Path(__file__).resolve()
    roots = [
        Path.cwd(),
        here.parents[1],
        here.parents[2] if len(here.parents) > 2 else here.parent,
        here.parents[3] if len(here.parents) > 3 else here.parent,
    ]
    subdirs = ["", "build", "build-shared", "cmake-build-debug", "cmake-build-release", "lib", "bin"]
    for root in roots:
        for sub in subdirs:
            base = root / sub if sub else root
            for name in _platform_library_names():
                candidates.append(base / name)

    # Preserve order while removing duplicates.
    seen: set[str] = set()
    unique: list[Path] = []
    for path in candidates:
        key = str(path)
        if key not in seen:
            unique.append(path)
            seen.add(key)
    return unique


def load_library(path: Optional[str] = None) -> ct.CDLL:
    errors: list[str] = []
    for candidate in candidate_library_paths(path):
        if candidate.exists():
            try:
                lib = ct.CDLL(str(candidate))
                bind_library(lib)
                _validate_library_versions(lib, candidate)
                return lib
            except (OSError, AttributeError, RuntimeError) as exc:
                errors.append(f"{candidate}: {exc}")

    found = find_library("gwent_core")
    if found:
        try:
            lib = ct.CDLL(found)
            bind_library(lib)
            _validate_library_versions(lib, found)
            return lib
        except (OSError, AttributeError, RuntimeError) as exc:
            errors.append(f"{found}: {exc}")

    searched = "\n".join(f"  - {p}" for p in candidate_library_paths(path))
    detail = "\n".join(errors)
    raise RuntimeError(
        "Could not load gwent_core shared library. Build with "
        "-DBUILD_SHARED_LIBS=ON and set GWENT_CORE_LIBRARY.\n"
        f"Searched:\n{searched}\n{detail}"
    )



def _validate_library_versions(lib: ct.CDLL, source) -> None:
    schema = int(lib.gwent_rl_schema_version())
    if schema != GWENT_RL_SCHEMA_VERSION:
        raise RuntimeError(
            f"{source}: RL schema v{schema}, expected v{GWENT_RL_SCHEMA_VERSION}"
        )
    grammar_version = int(lib.gwent_rl_action_grammar_version())
    if grammar_version != GWENT_RL_ACTION_GRAMMAR_VERSION:
        raise RuntimeError(
            f"{source}: action grammar v{grammar_version}, expected "
            f"v{GWENT_RL_ACTION_GRAMMAR_VERSION}"
        )
    reward_version = int(lib.gwent_rl_reward_config_version())
    if reward_version != GWENT_RL_REWARD_CONFIG_VERSION:
        raise RuntimeError(
            f"{source}: reward config ABI v{reward_version}, expected "
            f"v{GWENT_RL_REWARD_CONFIG_VERSION}"
        )


def bind_library(lib: ct.CDLL) -> None:
    lib.gwent_rl_default_reward_config.argtypes = []
    lib.gwent_rl_default_reward_config.restype = GwentRlRewardConfig

    lib.gwent_rl_default_collector_config.argtypes = []
    lib.gwent_rl_default_collector_config.restype = GwentRlCollectorConfig

    lib.gwent_rl_collector_create.argtypes = [ct.POINTER(GwentRlCollectorConfig)]
    lib.gwent_rl_collector_create.restype = ct.c_void_p
    lib.gwent_rl_collector_create_with_matchups.argtypes = [
        ct.POINTER(GwentRlCollectorConfig),
        ct.POINTER(GwentRlMatchupConfig),
        ct.c_size_t,
    ]
    lib.gwent_rl_collector_create_with_matchups.restype = ct.c_void_p

    lib.gwent_rl_collector_destroy.argtypes = [ct.c_void_p]
    lib.gwent_rl_collector_destroy.restype = None

    lib.gwent_rl_collector_set_game_limit.argtypes = [ct.c_void_p, ct.c_size_t]
    lib.gwent_rl_collector_set_game_limit.restype = ct.c_int

    lib.gwent_rl_collector_collect.argtypes = [ct.c_void_p, ct.POINTER(GwentRlInferenceBatch)]
    lib.gwent_rl_collector_collect.restype = ct.c_int

    lib.gwent_rl_collector_apply_actions.argtypes = [
        ct.c_void_p,
        ct.POINTER(GwentRlBatchAction),
        ct.c_size_t,
        ct.POINTER(GwentRlApplyResultBatch),
    ]
    lib.gwent_rl_collector_apply_actions.restype = ct.c_int

    lib.gwent_rl_collector_probe_pass.argtypes = [
        ct.c_void_p,
        ct.c_size_t,
        ct.POINTER(GwentRlPassProbeResult),
    ]
    lib.gwent_rl_collector_probe_pass.restype = ct.c_int

    lib.gwent_rl_collector_env_count.argtypes = [ct.c_void_p]
    lib.gwent_rl_collector_env_count.restype = ct.c_size_t

    lib.gwent_rl_collector_completed_episodes.argtypes = [ct.c_void_p]
    lib.gwent_rl_collector_completed_episodes.restype = ct.c_size_t

    lib.gwent_rl_collector_total_steps.argtypes = [ct.c_void_p]
    lib.gwent_rl_collector_total_steps.restype = ct.c_size_t

    lib.gwent_rl_collector_total_resets.argtypes = [ct.c_void_p]
    lib.gwent_rl_collector_total_resets.restype = ct.c_size_t

    lib.gwent_c_result_code_name.argtypes = [ct.c_int]
    lib.gwent_c_result_code_name.restype = ct.c_char_p

    lib.gwent_rl_reward_mode_name.argtypes = [ct.c_int]
    lib.gwent_rl_reward_mode_name.restype = ct.c_char_p

    lib.gwent_rl_schema_version.argtypes = []
    lib.gwent_rl_schema_version.restype = ct.c_uint32
    lib.gwent_rl_action_grammar_version.argtypes = []
    lib.gwent_rl_action_grammar_version.restype = ct.c_uint32
    lib.gwent_rl_reward_config_version.argtypes = []
    lib.gwent_rl_reward_config_version.restype = ct.c_uint32
    lib.gwent_rl_global_feature_name.argtypes = [ct.c_size_t]
    lib.gwent_rl_global_feature_name.restype = ct.c_char_p
    lib.gwent_rl_object_feature_name.argtypes = [ct.c_size_t]
    lib.gwent_rl_object_feature_name.restype = ct.c_char_p
    lib.gwent_rl_option_feature_name.argtypes = [ct.c_size_t]
    lib.gwent_rl_option_feature_name.restype = ct.c_char_p


def result_code_name(lib: ct.CDLL, code: int) -> str:
    try:
        raw = lib.gwent_c_result_code_name(int(code))
        if raw:
            return raw.decode("utf-8", errors="replace")
    except Exception:
        pass
    return f"result_code_{code}"
