from __future__ import annotations

import ctypes as ct
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional, Sequence, Union

import numpy as np

from ._ctypes import (
    GWENT_C_OK,
    GWENT_RL_DECK_A,
    GWENT_RL_DECK_B,
    GWENT_RL_REWARD_TERMINAL_ONLY,
    GWENT_RL_REWARD_ROUND_SHAPING,
    GWENT_RL_REWARD_DENSE_SHAPING,
    GWENT_RL_REWARD_STRATEGIC_SHAPING,
    GWENT_RL_GLOBAL_FEATURE_COUNT,
    GWENT_RL_MAX_OBJECTS,
    GWENT_RL_MAX_OPTIONS,
    GWENT_RL_OBJECT_FEATURE_COUNT,
    GWENT_RL_OPTION_FEATURE_COUNT,
    GWENT_RL_MAX_PREFIX,
    GwentRlApplyResultBatch,
    GwentRlRewardConfig,
    GwentRlBatchAction,
    GwentRlBatchItem,
    GwentRlBatchStepResult,
    GwentRlCollectorConfig,
    GwentRlInferenceBatch,
    GwentRlMatchupConfig,
    GwentRlPassProbeResult,
    load_library,
    result_code_name,
)

def _deck_ids() -> dict[str, int]:
    root = Path(__file__).resolve().parents[3]
    ids = [json.loads(path.read_text(encoding="utf-8-sig"))["id"]
           for path in sorted((root / "data" / "decks").glob("*.json"))]
    mapping = {str(deck_id).lower(): index for index, deck_id in enumerate(ids)}
    for deck_id, index in list(mapping.items()):
        if deck_id.startswith("deck_"):
            mapping[deck_id.removeprefix("deck_")] = index
    return mapping

REWARD_MODE_NAMES = {
    "terminal": GWENT_RL_REWARD_TERMINAL_ONLY,
    "terminal_only": GWENT_RL_REWARD_TERMINAL_ONLY,
    "round": GWENT_RL_REWARD_ROUND_SHAPING,
    "round_shaping": GWENT_RL_REWARD_ROUND_SHAPING,
    "dense": GWENT_RL_REWARD_DENSE_SHAPING,
    "dense_shaping": GWENT_RL_REWARD_DENSE_SHAPING,
    "strategic": GWENT_RL_REWARD_STRATEGIC_SHAPING,
    "strategic_shaping": GWENT_RL_REWARD_STRATEGIC_SHAPING,
    "resource": GWENT_RL_REWARD_STRATEGIC_SHAPING,
    "resource_shaping": GWENT_RL_REWARD_STRATEGIC_SHAPING,
}


def _resolve_reward_mode(reward_mode: Union[str, int]) -> int:
    if isinstance(reward_mode, int):
        if reward_mode in (
            GWENT_RL_REWARD_TERMINAL_ONLY,
            GWENT_RL_REWARD_ROUND_SHAPING,
            GWENT_RL_REWARD_DENSE_SHAPING,
            GWENT_RL_REWARD_STRATEGIC_SHAPING,
        ):
            return int(reward_mode)
        raise ValueError(f"unknown reward mode: {reward_mode}")
    key = str(reward_mode).strip().lower().replace("-", "_")
    if key not in REWARD_MODE_NAMES:
        raise ValueError(f"unknown reward mode: {reward_mode!r}")
    return REWARD_MODE_NAMES[key]


def _resolve_deck_id(deck: Union[str, int]) -> int:
    if isinstance(deck, int):
        if deck in (GWENT_RL_DECK_A, GWENT_RL_DECK_B):
            return int(deck)
        raise ValueError(f"unknown deck id: {deck}")
    key = str(deck).strip().lower().replace("-", "_")
    deck_ids = _deck_ids()
    if key not in deck_ids:
        raise ValueError(f"unknown deck: {deck!r}")
    return deck_ids[key]


def _apply_reward_overrides(config: GwentRlRewardConfig, overrides: Optional[dict[str, float]]) -> GwentRlRewardConfig:
    if not overrides:
        return config
    field_types = {name: ctype for name, ctype in config._fields_ if name != "mode"}
    for key, value in overrides.items():
        ctype = field_types.get(key)
        if ctype is None:
            raise ValueError(f"unknown reward config field: {key}")
        if ctype is ct.c_int:
            setattr(config, key, int(value))
        else:
            setattr(config, key, float(value))
    return config


@dataclass(frozen=True)
class BatchActions:
    """Actions returned to C for a batch produced by `RlCollector.collect()`."""

    env_indices: np.ndarray
    decision_serials: np.ndarray
    option_indices: np.ndarray


@dataclass(frozen=True)
class PassProbe:
    applicable: bool
    actor_id: int
    outcome: int
    actor_hand_count: int
    opponent_hand_count: int
    hand_diff: int


@dataclass(frozen=True)
class ApplyResults:
    """Copied result arrays from `gwent_rl_collector_apply_actions`."""

    env_indices: np.ndarray
    decision_serials: np.ndarray
    result_codes: np.ndarray
    action_statuses: np.ndarray
    dones: np.ndarray
    actor_ids: np.ndarray
    winner_ids: np.ndarray
    rewards: np.ndarray
    option_counts: np.ndarray

    @property
    def count(self) -> int:
        return int(self.env_indices.shape[0])


class InferenceBatch:
    """Live view over C-owned inference batch buffers.

    The arrays exposed by this object are views into the collector's internal
    buffers. They are valid until the next collector call that refreshes those
    buffers. Copy arrays before storing them in a rollout buffer.
    """

    def __init__(self, collector: "RlCollector", c_batch: GwentRlInferenceBatch):
        self._collector = collector
        self._c = c_batch

    @property
    def count(self) -> int:
        return int(self._c.count)

    @property
    def max_count(self) -> int:
        return int(self._c.max_count)

    @property
    def items(self):
        if self.count == 0:
            return []
        return [self._c.items[i] for i in range(self.count)]

    @property
    def env_indices(self) -> np.ndarray:
        return np.array([item.env_index for item in self.items], dtype=np.uint64)

    @property
    def decision_serials(self) -> np.ndarray:
        return np.array([item.decision_serial for item in self.items], dtype=np.uint64)

    @property
    def actor_ids(self) -> np.ndarray:
        return np.array([item.actor_id for item in self.items], dtype=np.int32)

    @property
    def actor_deck_ids(self) -> np.ndarray:
        """Python-side routing metadata for shared A/B policy heads.

        This does not change the C observation schema.  The collector mirrors
        the C matchup schedule and maps each batch row's (env, actor) pair to
        deck A=0 or deck B=1.
        """
        return self._collector.deck_ids_for_rows(self.env_indices, self.actor_ids)

    @property
    def opponent_deck_ids(self) -> np.ndarray:
        return self._collector.deck_ids_for_rows(self.env_indices, 1 - self.actor_ids)

    @property
    def decision_kinds(self) -> np.ndarray:
        return np.array([item.decision_kind for item in self.items], dtype=np.int32)

    @property
    def option_counts(self) -> np.ndarray:
        return np.array([item.option_count for item in self.items], dtype=np.uint64)

    @property
    def object_counts(self) -> np.ndarray:
        return np.array([item.object_count for item in self.items], dtype=np.uint64)

    @property
    def prefix_counts(self) -> np.ndarray:
        return np.array([item.prefix_count for item in self.items], dtype=np.uint64)

    @property
    def source_object_indices(self) -> np.ndarray:
        return np.array([item.source_object_index for item in self.items], dtype=np.int32)

    @property
    def source_entity_ids(self) -> np.ndarray:
        return np.array([item.source_entity_id for item in self.items], dtype=np.int32)

    @property
    def source_card_ids(self) -> np.ndarray:
        return np.array([item.source_card_id for item in self.items], dtype=np.int32)

    def _view_float(self, ptr, shape: tuple[int, ...]) -> np.ndarray:
        if self.count == 0:
            return np.empty(shape, dtype=np.float32)
        return np.ctypeslib.as_array(ptr, shape=(int(np.prod(shape)),)).reshape(shape)

    def _view_int(self, ptr, shape: tuple[int, ...]) -> np.ndarray:
        if self.count == 0:
            return np.empty(shape, dtype=np.int32)
        return np.ctypeslib.as_array(ptr, shape=(int(np.prod(shape)),)).reshape(shape)

    def _view_u8(self, ptr, shape: tuple[int, ...]) -> np.ndarray:
        if self.count == 0:
            return np.empty(shape, dtype=np.uint8)
        return np.ctypeslib.as_array(ptr, shape=(int(np.prod(shape)),)).reshape(shape)

    @property
    def global_features(self) -> np.ndarray:
        return self._view_float(self._c.global_features, (self.count, GWENT_RL_GLOBAL_FEATURE_COUNT))

    @property
    def object_entity_ids(self) -> np.ndarray:
        return self._view_int(self._c.object_entity_ids, (self.count, GWENT_RL_MAX_OBJECTS))

    @property
    def object_card_ids(self) -> np.ndarray:
        return self._view_int(self._c.object_card_ids, (self.count, GWENT_RL_MAX_OBJECTS))

    @property
    def object_owner_ids(self) -> np.ndarray:
        return self._view_int(self._c.object_owner_ids, (self.count, GWENT_RL_MAX_OBJECTS))

    @property
    def object_controller_ids(self) -> np.ndarray:
        return self._view_int(self._c.object_controller_ids, (self.count, GWENT_RL_MAX_OBJECTS))

    @property
    def object_zone_ids(self) -> np.ndarray:
        return self._view_int(self._c.object_zone_ids, (self.count, GWENT_RL_MAX_OBJECTS))

    @property
    def object_row_ids(self) -> np.ndarray:
        return self._view_int(self._c.object_row_ids, (self.count, GWENT_RL_MAX_OBJECTS))

    @property
    def object_slot_indices(self) -> np.ndarray:
        return self._view_int(self._c.object_slot_indices, (self.count, GWENT_RL_MAX_OBJECTS))

    @property
    def object_mask(self) -> np.ndarray:
        return self._view_u8(self._c.object_mask, (self.count, GWENT_RL_MAX_OBJECTS))

    @property
    def object_features(self) -> np.ndarray:
        return self._view_float(
            self._c.object_features,
            (self.count, GWENT_RL_MAX_OBJECTS, GWENT_RL_OBJECT_FEATURE_COUNT),
        )

    @property
    def option_kind_ids(self) -> np.ndarray:
        return self._view_int(self._c.option_kind_ids, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_card_ids(self) -> np.ndarray:
        return self._view_int(self._c.option_card_ids, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_source_object_indices(self) -> np.ndarray:
        return self._view_int(self._c.option_source_object_indices, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_target_object_indices(self) -> np.ndarray:
        return self._view_int(self._c.option_target_object_indices, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_target_side_ids(self) -> np.ndarray:
        return self._view_int(self._c.option_target_side_ids, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_target_zone_ids(self) -> np.ndarray:
        return self._view_int(self._c.option_target_zone_ids, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_target_row_ids(self) -> np.ndarray:
        return self._view_int(self._c.option_target_row_ids, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_insert_positions(self) -> np.ndarray:
        return self._view_int(self._c.option_insert_positions, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_hand_slot_indices(self) -> np.ndarray:
        return self._view_int(self._c.option_hand_slot_indices, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_stable_hashes(self) -> np.ndarray:
        if self.count == 0:
            return np.empty((self.count, GWENT_RL_MAX_OPTIONS), dtype=np.uint64)
        return np.ctypeslib.as_array(self._c.option_stable_hashes, shape=(self.count * GWENT_RL_MAX_OPTIONS,)).reshape((self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_mask(self) -> np.ndarray:
        return self._view_u8(self._c.option_mask, (self.count, GWENT_RL_MAX_OPTIONS))

    @property
    def option_features(self) -> np.ndarray:
        return self._view_float(
            self._c.option_features,
            (self.count, GWENT_RL_MAX_OPTIONS, GWENT_RL_OPTION_FEATURE_COUNT),
        )

    @property
    def prefix_kind_ids(self) -> np.ndarray:
        return self._view_int(self._c.prefix_kind_ids, (self.count, GWENT_RL_MAX_PREFIX))

    @property
    def prefix_source_object_indices(self) -> np.ndarray:
        return self._view_int(self._c.prefix_source_object_indices, (self.count, GWENT_RL_MAX_PREFIX))

    @property
    def prefix_target_object_indices(self) -> np.ndarray:
        return self._view_int(self._c.prefix_target_object_indices, (self.count, GWENT_RL_MAX_PREFIX))

    @property
    def prefix_card_ids(self) -> np.ndarray:
        return self._view_int(self._c.prefix_card_ids, (self.count, GWENT_RL_MAX_PREFIX))

    @property
    def prefix_row_ids(self) -> np.ndarray:
        return self._view_int(self._c.prefix_row_ids, (self.count, GWENT_RL_MAX_PREFIX))

    @property
    def prefix_insert_positions(self) -> np.ndarray:
        return self._view_int(self._c.prefix_insert_positions, (self.count, GWENT_RL_MAX_PREFIX))

    @property
    def prefix_mask(self) -> np.ndarray:
        return self._view_u8(self._c.prefix_mask, (self.count, GWENT_RL_MAX_PREFIX))

    def copy_numpy(self) -> dict[str, np.ndarray]:
        """Return a safe copy suitable for rollout storage."""
        return {
            "env_indices": self.env_indices.copy(),
            "decision_serials": self.decision_serials.copy(),
            "actor_ids": self.actor_ids.copy(),
            "actor_deck_ids": self.actor_deck_ids.copy(),
            "opponent_deck_ids": self.opponent_deck_ids.copy(),
            "decision_kinds": self.decision_kinds.copy(),
            "option_counts": self.option_counts.copy(),
            "object_counts": self.object_counts.copy(),
            "prefix_counts": self.prefix_counts.copy(),
            "source_object_indices": self.source_object_indices.copy(),
            "source_entity_ids": self.source_entity_ids.copy(),
            "source_card_ids": self.source_card_ids.copy(),
            "global_features": self.global_features.copy(),
            "object_features": self.object_features.copy(),
            "object_mask": self.object_mask.copy(),
            "option_features": self.option_features.copy(),
            "option_mask": self.option_mask.copy(),
            "option_kind_ids": self.option_kind_ids.copy(),
            "object_entity_ids": self.object_entity_ids.copy(),
            "object_card_ids": self.object_card_ids.copy(),
            "object_owner_ids": self.object_owner_ids.copy(),
            "object_controller_ids": self.object_controller_ids.copy(),
            "object_zone_ids": self.object_zone_ids.copy(),
            "object_row_ids": self.object_row_ids.copy(),
            "object_slot_indices": self.object_slot_indices.copy(),
            "option_card_ids": self.option_card_ids.copy(),
            "option_source_object_indices": self.option_source_object_indices.copy(),
            "option_target_object_indices": self.option_target_object_indices.copy(),
            "option_target_side_ids": self.option_target_side_ids.copy(),
            "option_target_zone_ids": self.option_target_zone_ids.copy(),
            "option_target_row_ids": self.option_target_row_ids.copy(),
            "option_insert_positions": self.option_insert_positions.copy(),
            "option_hand_slot_indices": self.option_hand_slot_indices.copy(),
            "option_stable_hashes": self.option_stable_hashes.copy(),
            "prefix_kind_ids": self.prefix_kind_ids.copy(),
            "prefix_source_object_indices": self.prefix_source_object_indices.copy(),
            "prefix_target_object_indices": self.prefix_target_object_indices.copy(),
            "prefix_card_ids": self.prefix_card_ids.copy(),
            "prefix_row_ids": self.prefix_row_ids.copy(),
            "prefix_insert_positions": self.prefix_insert_positions.copy(),
            "prefix_mask": self.prefix_mask.copy(),
        }

    def to_torch(self, device=None) -> dict[str, object]:
        """Convert the live batch view into torch tensors.

        This imports torch lazily so the base package only depends on numpy.
        The tensors are copies because the C buffers are owned by the collector
        and can be refreshed by later calls.
        """
        import torch

        copied = self.copy_numpy()
        tensors = {}
        for key, value in copied.items():
            tensor = torch.from_numpy(value)
            if device is not None:
                tensor = tensor.to(device)
            tensors[key] = tensor
        return tensors


def _copy_apply_results(c_results: GwentRlApplyResultBatch) -> ApplyResults:
    count = int(c_results.count)
    rows = [c_results.results[i] for i in range(count)] if count else []
    rewards = np.zeros((count, 2), dtype=np.float32)
    for i, row in enumerate(rows):
        rewards[i, 0] = row.reward[0]
        rewards[i, 1] = row.reward[1]
    return ApplyResults(
        env_indices=np.array([row.env_index for row in rows], dtype=np.uint64),
        decision_serials=np.array([row.decision_serial for row in rows], dtype=np.uint64),
        result_codes=np.array([row.result_code for row in rows], dtype=np.int32),
        action_statuses=np.array([row.action_status for row in rows], dtype=np.int32),
        dones=np.array([row.done for row in rows], dtype=np.int32),
        actor_ids=np.array([row.actor_id for row in rows], dtype=np.int32),
        winner_ids=np.array([row.winner_id for row in rows], dtype=np.int32),
        rewards=rewards,
        option_counts=np.array([row.option_count for row in rows], dtype=np.uint64),
    )


class RlCollector:
    """Python owner for a C `gwent_rl_collector`."""

    def __init__(
        self,
        num_envs: int = 128,
        max_batch_size: Optional[int] = None,
        base_seed: int = 0,
        starting_player_id: int = -1,
        shuffle_decks: bool = True,
        auto_reset_done_envs: bool = True,
        enable_invariants: bool = False,
        include_private_info: bool = False,
        reward_mode: Union[str, int] = "terminal",
        reward_overrides: Optional[dict[str, float]] = None,
        player0_deck: Union[str, int] = "a",
        player1_deck: Union[str, int] = "a",
        matchups: Optional[Mapping[str, Mapping[str, object]] | Sequence[Mapping[str, object]]] = None,
        library_path: Optional[str] = None,
    ):
        self._lib = load_library(library_path)
        cfg = self._lib.gwent_rl_default_collector_config()
        cfg.num_envs = int(num_envs)
        cfg.max_batch_size = int(max_batch_size if max_batch_size is not None else num_envs)
        cfg.base_seed = int(base_seed)
        cfg.starting_player_id = int(starting_player_id)
        cfg.shuffle_decks = 1 if shuffle_decks else 0
        cfg.auto_reset_done_envs = 1 if auto_reset_done_envs else 0
        cfg.enable_invariants = 1 if enable_invariants else 0
        cfg.include_private_info = 1 if include_private_info else 0
        cfg.reward_config.mode = _resolve_reward_mode(reward_mode)
        cfg.reward_config = _apply_reward_overrides(cfg.reward_config, reward_overrides)
        cfg.player0_deck_id = _resolve_deck_id(player0_deck)
        cfg.player1_deck_id = _resolve_deck_id(player1_deck)
        self.reward_mode = int(cfg.reward_config.mode)
        self._default_decks = (int(cfg.player0_deck_id), int(cfg.player1_deck_id))
        self._auto_reset_done_envs = bool(cfg.auto_reset_done_envs)
        self._max_total_resets_py: int | None = None
        self._pending_done_envs: set[int] = set()

        matchup_values = list(matchups.values()) if isinstance(matchups, Mapping) else list(matchups or [])
        self._matchup_cycle: list[tuple[int, int]] = []
        c_matchups = (GwentRlMatchupConfig * len(matchup_values))()
        for i, matchup in enumerate(matchup_values):
            p0_id = _resolve_deck_id(matchup["player0"])
            p1_id = _resolve_deck_id(matchup["player1"])
            c_matchups[i].player0_deck_id = p0_id
            c_matchups[i].player1_deck_id = p1_id
            weight = int(matchup.get("weight", 1))
            if weight <= 0:
                raise ValueError("matchup weight must be positive")
            c_matchups[i].weight = weight
            self._matchup_cycle.extend([(int(p0_id), int(p1_id))] * weight)

        env_count = max(int(cfg.num_envs), 1)
        self._env_decks: dict[int, tuple[int, int]] = {
            env_index: self._select_matchup_py(env_index)
            for env_index in range(env_count)
        }
        self._total_resets_py = env_count
        if matchup_values:
            self._handle = self._lib.gwent_rl_collector_create_with_matchups(
                ct.byref(cfg), c_matchups, len(matchup_values)
            )
        else:
            self._handle = self._lib.gwent_rl_collector_create(ct.byref(cfg))
        if not self._handle:
            raise RuntimeError("gwent_rl_collector_create returned null")
        self._closed = False

    def _select_matchup_py(self, game_ordinal: int) -> tuple[int, int]:
        if not self._matchup_cycle:
            return self._default_decks
        return self._matchup_cycle[int(game_ordinal) % len(self._matchup_cycle)]

    def _sync_pending_resets_py(self) -> None:
        if not self._pending_done_envs:
            return
        pending = sorted(self._pending_done_envs)
        self._pending_done_envs.clear()
        if not self._auto_reset_done_envs:
            return
        for env_index in pending:
            if self._max_total_resets_py is not None and self._total_resets_py >= self._max_total_resets_py:
                break
            self._env_decks[int(env_index)] = self._select_matchup_py(self._total_resets_py)
            self._total_resets_py += 1

    def deck_ids_for_rows(self, env_indices: np.ndarray, actor_ids: np.ndarray) -> np.ndarray:
        env_indices = np.asarray(env_indices).reshape(-1)
        actor_ids = np.asarray(actor_ids).reshape(-1)
        if env_indices.shape[0] != actor_ids.shape[0]:
            raise ValueError("env_indices and actor_ids must have the same length")
        out = np.empty((env_indices.shape[0],), dtype=np.int32)
        for i, (env_index, actor_id) in enumerate(zip(env_indices, actor_ids)):
            actor = int(actor_id)
            if actor not in (0, 1):
                raise ValueError(f"invalid actor id for deck routing: {actor}")
            decks = self._env_decks.get(int(env_index))
            if decks is None:
                raise KeyError(f"missing deck routing metadata for env {int(env_index)}")
            out[i] = int(decks[actor])
        return out

    def close(self) -> None:
        if not self._closed:
            self._lib.gwent_rl_collector_destroy(self._handle)
            self._handle = None
            self._closed = True

    def __enter__(self) -> "RlCollector":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    def _require_open(self) -> None:
        if self._closed or not self._handle:
            raise RuntimeError("RlCollector is closed")

    def set_game_limit(self, max_games: int) -> None:
        """Allow exactly max_games game starts, then drain active games."""
        self._require_open()
        max_games = int(max_games)
        if max_games <= 0:
            raise ValueError("max_games must be positive")
        code = self._lib.gwent_rl_collector_set_game_limit(self._handle, max_games)
        if code == GWENT_C_OK:
            self._max_total_resets_py = max_games
        if code != GWENT_C_OK:
            raise RuntimeError(
                f"gwent_rl_collector_set_game_limit failed: {result_code_name(self._lib, code)}"
            )

    def collect(self) -> InferenceBatch:
        self._require_open()
        # The C collector resets completed envs at the beginning of collect(),
        # in ascending env-index order. Mirror the same deterministic schedule
        # before requesting the new batch so actor_deck_ids stay aligned.
        self._sync_pending_resets_py()
        c_batch = GwentRlInferenceBatch()
        code = self._lib.gwent_rl_collector_collect(self._handle, ct.byref(c_batch))
        if code != GWENT_C_OK:
            raise RuntimeError(f"gwent_rl_collector_collect failed: {result_code_name(self._lib, code)}")
        return InferenceBatch(self, c_batch)

    def apply_actions(self, batch: InferenceBatch, option_indices: Sequence[int] | np.ndarray) -> ApplyResults:
        self._require_open()
        option_indices = np.asarray(option_indices, dtype=np.uint64)
        if option_indices.shape[0] != batch.count:
            raise ValueError(f"expected {batch.count} option indices, got {option_indices.shape[0]}")

        env_indices = batch.env_indices
        decision_serials = batch.decision_serials
        actions = (GwentRlBatchAction * batch.count)()
        for i in range(batch.count):
            actions[i].env_index = int(env_indices[i])
            actions[i].decision_serial = int(decision_serials[i])
            actions[i].option_index = int(option_indices[i])

        c_results = GwentRlApplyResultBatch()
        code = self._lib.gwent_rl_collector_apply_actions(
            self._handle,
            actions,
            int(batch.count),
            ct.byref(c_results),
        )
        # STALE_DECISION is an important normal error path. Return row details
        # to the caller instead of discarding them.
        results = _copy_apply_results(c_results)
        for env_index, done in zip(results.env_indices, results.dones):
            if int(done) != 0:
                self._pending_done_envs.add(int(env_index))
        if code not in (GWENT_C_OK,):
            raise RuntimeError(f"gwent_rl_collector_apply_actions failed: {result_code_name(self._lib, code)}")
        return results

    def probe_pass(self, env_index: int) -> PassProbe:
        """Simulate PASS on a copy of one env and classify the resulting round.

        This is diagnostic-only and never mutates the live collector environment.
        A DRAW is only considered a safe draw by callers when hand_diff == 0.
        """
        self._require_open()
        raw = GwentRlPassProbeResult()
        code = self._lib.gwent_rl_collector_probe_pass(
            self._handle, int(env_index), ct.byref(raw)
        )
        if code != GWENT_C_OK:
            raise RuntimeError(
                f"gwent_rl_collector_probe_pass failed: {result_code_name(self._lib, code)}"
            )
        return PassProbe(
            applicable=bool(raw.applicable),
            actor_id=int(raw.actor_id),
            outcome=int(raw.outcome),
            actor_hand_count=int(raw.actor_hand_count),
            opponent_hand_count=int(raw.opponent_hand_count),
            hand_diff=int(raw.hand_diff_actor_minus_opponent),
        )

    @property
    def env_count(self) -> int:
        self._require_open()
        return int(self._lib.gwent_rl_collector_env_count(self._handle))

    @property
    def completed_episodes(self) -> int:
        self._require_open()
        return int(self._lib.gwent_rl_collector_completed_episodes(self._handle))

    @property
    def total_steps(self) -> int:
        self._require_open()
        return int(self._lib.gwent_rl_collector_total_steps(self._handle))

    @property
    def total_resets(self) -> int:
        self._require_open()
        return int(self._lib.gwent_rl_collector_total_resets(self._handle))


def random_legal_actions(batch: InferenceBatch, rng: Optional[np.random.Generator] = None) -> np.ndarray:
    """Sample one legal option index per batch row."""
    if rng is None:
        rng = np.random.default_rng()
    actions = np.zeros(batch.count, dtype=np.uint64)
    mask = batch.option_mask
    counts = batch.option_counts
    for row in range(batch.count):
        n = int(counts[row])
        legal = np.flatnonzero(mask[row, :n] != 0)
        if legal.size == 0:
            raise RuntimeError(f"batch row {row} has no legal option")
        actions[row] = int(rng.choice(legal))
    return actions
