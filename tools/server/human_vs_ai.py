"""Local Gwent Core HTTP adapter.

The default configuration is intentionally local/manual-test friendly:
- P0 and P1 are both controlled from the browser;
- no AI checkpoint is required or loaded;
- PyTorch is imported lazily only when human_vs_ai mode is used;
- the Core binds to 127.0.0.1 and never requires a remote server.

No command-line arguments are required.  Edit the SERVER_* globals below and
run this file directly from an IDE or with ``python tools/server/human_vs_ai.py``.
"""

from __future__ import annotations

import ctypes as ct
import os
import sys
import threading
import time
from pathlib import Path
from typing import Any, Optional

import numpy as np
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, ConfigDict, Field

# =============================================================================
# Local runtime configuration — edit these globals; no CLI parameters required.
# =============================================================================

# "manual_test": P0/P1 are both controlled by the browser; no model is loaded.
# "human_vs_ai": P0 is human, P1 uses SERVER_CHECKPOINT.
SERVER_MODE = "manual_test"

# Leave as None for manual_test.  Set to a .pt file only for human_vs_ai.
SERVER_CHECKPOINT: str | None = None

# Empty string means auto-detect the Core library from common local build dirs.
# You can also set an explicit relative/absolute path here, e.g.
# r"build-vs\Release\gwent_core.dll" on Windows.
SERVER_LIBRARY = ""

# AI inference device.  Ignored in manual_test mode.
SERVER_DEVICE = "cpu"
SERVER_SEED = 123

# Loopback only: this is a local process, not a remote training server.
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 8008

# Used only when switching to human_vs_ai and SERVER_CHECKPOINT is still None.
DEFAULT_CHECKPOINT = "models/v3/policy.pt"

# Product-facing HTTP contract version. Keep independent from RL schema versions.
CORE_API_VERSION = 1



def find_project_root() -> Optional[Path]:
    here = Path(__file__).resolve()
    for root in [Path.cwd(), here.parent, *here.parents]:
        if (root / "CMakeLists.txt").exists() and (root / "python" / "src" / "gwent_rl").exists():
            return root
    return None


PROJECT_ROOT = find_project_root()
if PROJECT_ROOT is not None:
    python_src = PROJECT_ROOT / "python" / "src"
    if str(python_src) not in sys.path:
        sys.path.insert(0, str(python_src))

from gwent_rl._ctypes import (  # noqa: E402
    GWENT_C_OK,
    GWENT_RL_GLOBAL_FEATURE_COUNT,
    GWENT_RL_MAX_OBJECTS,
    GWENT_RL_OBJECT_FEATURE_COUNT,
    GWENT_RL_MAX_OPTIONS,
    GWENT_RL_OPTION_FEATURE_COUNT,
    GWENT_RL_MAX_PREFIX,
    GWENT_RL_REWARD_STRATEGIC_SHAPING,
    GwentRlRewardConfig,
    load_library,
)
from gwent_rl.card_vocab import load_supported_card_records  # noqa: E402
from gwent_rl.schema import DECISION_KIND_NAMES, GLOBAL_FEATURE_NAMES, OPTION_KIND_NAMES  # noqa: E402


ZONE_NAMES = {
    0: "Deck",
    1: "Hand",
    2: "Stay",
    3: "Melee",
    4: "Ranged",
    5: "Cemetery",
    6: "Banished",
    7: "Leader",
}
ROW_NAMES = {-1: "-", 0: "Melee", 1: "Ranged"}
ROW_EFFECT_NAMES = {"frost": "霜", "blood_moon": "血月"}
ROW_EFFECT_IDS = tuple(ROW_EFFECT_NAMES)


class GwentRlConfig(ct.Structure):
    # Keep this layout byte-for-byte aligned with gwent_rl_config in include/gwent/c/core.h.
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


def c_array(a, dtype, shape):
    return np.ctypeslib.as_array(a).astype(dtype, copy=True).reshape(shape)


def object_feature(obs: GwentRlObservation, object_index: int, feature_index: int) -> float:
    return float(obs.object_features[object_index * GWENT_RL_OBJECT_FEATURE_COUNT + feature_index])


class HumanVsAiGame:
    HUMAN = 0
    AI = 1
    MODE_HUMAN_VS_AI = "human_vs_ai"
    MODE_MANUAL_TEST = "manual_test"
    MODES = {MODE_HUMAN_VS_AI, MODE_MANUAL_TEST}

    def __init__(
        self,
        library: Path,
        checkpoint: Path | None,
        device: str = "auto",
        seed: int = 123,
        mode: str = MODE_HUMAN_VS_AI,
    ):
        if mode not in self.MODES:
            raise ValueError(f"unsupported game mode: {mode}")
        self.library = Path(library).resolve()
        self.checkpoint = Path(checkpoint).resolve() if checkpoint is not None else None
        self.device_arg = device
        self.lib = load_library(str(self.library))
        self._bind_api()
        self.card_names: dict[int, str] = {}
        self.card_types: dict[int, str] = {}
        self._load_cards()
        self.handle: Optional[int] = None
        self.policy = None
        self.device = None
        self.metadata: dict[str, Any] = {}
        self.last_ai_actions: list[dict[str, Any]] = []
        self.last_human_action: Optional[dict[str, Any]] = None
        self.seed = int(seed)
        self.player0_deck_id = 0
        self.player1_deck_id = 0
        self.mode = mode
        if self.checkpoint is not None:
            self.reload_checkpoint(self.checkpoint)
        elif self.mode == self.MODE_HUMAN_VS_AI:
            raise ValueError("human_vs_ai mode requires an AI checkpoint")
        self._create_env(self.seed, -1, self.player0_deck_id, self.player1_deck_id)
        if self.mode == self.MODE_HUMAN_VS_AI:
            self._run_ai_until_human()

    def _bind_api(self) -> None:
        lib = self.lib
        lib.gwent_rl_default_config.argtypes = []
        lib.gwent_rl_default_config.restype = GwentRlConfig
        lib.gwent_rl_env_create.argtypes = [ct.POINTER(GwentRlConfig)]
        lib.gwent_rl_env_create.restype = ct.c_void_p
        lib.gwent_rl_env_destroy.argtypes = [ct.c_void_p]
        lib.gwent_rl_env_destroy.restype = None
        lib.gwent_rl_env_reset.argtypes = [ct.c_void_p, ct.c_uint64, ct.c_int, ct.POINTER(GwentRlStepResult)]
        lib.gwent_rl_env_reset.restype = ct.c_int
        lib.gwent_rl_env_step_option.argtypes = [ct.c_void_p, ct.c_size_t, ct.POINTER(GwentRlStepResult)]
        lib.gwent_rl_env_step_option.restype = ct.c_int
        lib.gwent_rl_env_observation.argtypes = [ct.c_void_p]
        lib.gwent_rl_env_observation.restype = ct.POINTER(GwentRlObservation)
        lib.gwent_rl_env_row_effect_duration.argtypes = [ct.c_void_p, ct.c_int, ct.c_int, ct.c_char_p]
        lib.gwent_rl_env_row_effect_duration.restype = ct.c_int
        lib.gwent_c_action_status_name.argtypes = [ct.c_int]
        lib.gwent_c_action_status_name.restype = ct.c_char_p
        lib.gwent_c_result_code_name.argtypes = [ct.c_int]
        lib.gwent_c_result_code_name.restype = ct.c_char_p

    def _load_cards(self) -> None:
        try:
            path = PROJECT_ROOT / "data" / "cards" / "supported_cards.json" if PROJECT_ROOT else None
            records = load_supported_card_records(path if path and path.exists() else None)
            self.card_names = {int(r.card_id): r.name for r in records}
            self.card_types = {int(r.card_id): r.type for r in records}
        except Exception:
            self.card_names = {}
            self.card_types = {}

    def close(self) -> None:
        if self.handle:
            self.lib.gwent_rl_env_destroy(self.handle)
            self.handle = None

    def _create_env(
        self,
        seed: int,
        starting_player_id: int,
        player0_deck_id: int = 0,
        player1_deck_id: int = 0,
    ) -> None:
        for deck_id in (player0_deck_id, player1_deck_id):
            if deck_id not in (0, 1):
                raise ValueError(f"unsupported deck id: {deck_id}")

        self.close()
        cfg = self.lib.gwent_rl_default_config()
        cfg.seed = int(seed)
        cfg.starting_player_id = int(starting_player_id)
        cfg.shuffle_decks = 1
        cfg.enable_invariants = 1
        cfg.current_player_perspective = 1
        # The policy receives the same observation surface used during training.
        # public_state() hides P1 hand only in human-vs-AI mode; manual test mode
        # intentionally exposes both hands to the local debugging UI.
        cfg.include_private_info = 1
        cfg.reward_config.mode = GWENT_RL_REWARD_STRATEGIC_SHAPING
        cfg.player0_deck_id = int(player0_deck_id)
        cfg.player1_deck_id = int(player1_deck_id)
        self.player0_deck_id = int(player0_deck_id)
        self.player1_deck_id = int(player1_deck_id)
        self.handle = self.lib.gwent_rl_env_create(ct.byref(cfg))
        if not self.handle:
            raise RuntimeError("gwent_rl_env_create returned null")

    def new_game(
        self,
        seed: int,
        starting_player_id: int = -1,
        player0_deck_id: int | None = None,
        player1_deck_id: int | None = None,
        mode: str = MODE_HUMAN_VS_AI,
    ) -> dict[str, Any]:
        if mode not in self.MODES:
            raise ValueError(f"unsupported game mode: {mode}")
        if mode == self.MODE_HUMAN_VS_AI and self.policy is None:
            raise ValueError("AI checkpoint is not loaded; use manual_test or load a checkpoint first")

        self.seed = int(seed)
        self.mode = mode
        self.last_ai_actions = []
        self.last_human_action = None
        p0 = self.player0_deck_id if player0_deck_id is None else int(player0_deck_id)
        p1 = self.player1_deck_id if player1_deck_id is None else int(player1_deck_id)

        if p0 != self.player0_deck_id or p1 != self.player1_deck_id:
            self._create_env(seed, starting_player_id, p0, p1)
        else:
            result = GwentRlStepResult()
            code = self.lib.gwent_rl_env_reset(self.handle, int(seed), int(starting_player_id), ct.byref(result))
            if code != GWENT_C_OK:
                raise RuntimeError(f"reset failed: {self.result_code_name(code)}")

        if self.mode == self.MODE_HUMAN_VS_AI:
            self._run_ai_until_human()
        return self.public_state()

    def reload_checkpoint(self, checkpoint: Path | str) -> dict[str, Any]:
        p = Path(checkpoint)
        if not p.is_absolute() and PROJECT_ROOT is not None:
            p = PROJECT_ROOT / p
        p = p.resolve()
        if not p.exists():
            raise FileNotFoundError(p)
        # Keep manual_test lightweight: importing this server must not require
        # PyTorch.  Training/inference dependencies are loaded only when a model
        # is explicitly requested.
        from gwent_rl.experiment import policy_from_checkpoint

        policy, metadata = policy_from_checkpoint(p, device=self.device_arg)
        policy.eval()
        self.policy = policy
        self.metadata = metadata
        self.checkpoint = p
        self.device = next(policy.parameters()).device
        return {
            "api_version": CORE_API_VERSION,
            "checkpoint": str(self.checkpoint),
            "device": str(self.device),
            "update": int(metadata.get("update", -1)),
            "total_decisions": int(metadata.get("total_decisions", 0)),
        }

    def observation(self) -> GwentRlObservation:
        if not self.handle:
            raise RuntimeError("env is closed")
        ptr = self.lib.gwent_rl_env_observation(self.handle)
        if not ptr:
            raise RuntimeError("gwent_rl_env_observation returned null")
        return ptr.contents

    def result_code_name(self, code: int) -> str:
        raw = self.lib.gwent_c_result_code_name(int(code))
        return raw.decode("utf-8", errors="replace") if raw else str(code)

    def action_status_name(self, status: int) -> str:
        raw = self.lib.gwent_c_action_status_name(int(status))
        return raw.decode("utf-8", errors="replace") if raw else str(status)

    def card_name(self, card_id: int) -> str:
        if card_id < 0:
            return "-"
        return self.card_names.get(card_id, f"Card#{card_id}")

    def object_name(self, obs: GwentRlObservation, index: int) -> str:
        if index < 0 or index >= int(obs.object_count):
            return "-"
        return f"{self.card_name(int(obs.object_card_ids[index]))} [E{int(obs.object_entity_ids[index])}]"

    def describe_option(self, obs: GwentRlObservation, i: int) -> dict[str, Any]:
        kind_id = int(obs.option_kind_ids[i])
        card_id = int(obs.option_card_ids[i])
        src_idx = int(obs.option_source_object_indices[i])
        tgt_idx = int(obs.option_target_object_indices[i])
        tgt_side = int(obs.option_target_side_ids[i])
        tgt_zone = int(obs.option_target_zone_ids[i])
        tgt_row = int(obs.option_target_row_ids[i])
        insert_position = int(obs.option_insert_positions[i])
        hand_slot = int(obs.option_hand_slot_indices[i])
        kind = OPTION_KIND_NAMES.get(kind_id, f"kind_{kind_id}")
        source = self.object_name(obs, src_idx)
        if source == "-" and card_id >= 0:
            source = self.card_name(card_id)
        if tgt_idx >= 0:
            target = self.object_name(obs, tgt_idx)
        elif tgt_side >= 0 or tgt_zone >= 0 or tgt_row >= 0:
            target = f"P{tgt_side} {ZONE_NAMES.get(tgt_zone, tgt_zone)}/{ROW_NAMES.get(tgt_row, tgt_row)}"
            if insert_position >= 0:
                target += f" @ position {insert_position}"
        else:
            target = "-"
        label = kind

        if card_id >= 0:
            if kind == "choose_card":
                label = f"选择卡牌 · {self.card_name(card_id)}"

                # Deck 中的牌不在 public object list，
                # 所以直接用 option_card_id 显示候选牌名。
                if tgt_idx < 0:
                    target = self.card_name(card_id)

            elif kind == "choose_insert_position":
                label = f"选择位置 · {insert_position} · {self.card_name(card_id)}"
            elif kind == "choose_row":
                label = f"选择部署排 · {self.card_name(card_id)}"
            else:
                label += f" · {self.card_name(card_id)}"

        if kind == "choose_insert_position" and card_id < 0:
            label = f"选择位置 · {insert_position}"

        return {
            "index": i,
            "kind_id": kind_id,
            "kind": kind,
            "label": label,
            "card_id": card_id,
            "source": source,
            "target": target,
            "source_object_index": src_idx,
            "target_object_index": tgt_idx,
            "target_side": tgt_side,
            "target_zone": tgt_zone,
            "target_row": tgt_row,
            "insert_position": insert_position,
            "hand_slot": hand_slot,
            "stable_hash": str(int(obs.option_stable_hashes[i])),
        }

    def _obs_to_torch(self, obs: GwentRlObservation) -> dict[str, Any]:
        import torch
        d = self.device
        assert d is not None
        N, K, P = GWENT_RL_MAX_OBJECTS, GWENT_RL_MAX_OPTIONS, GWENT_RL_MAX_PREFIX
        batch: dict[str, Any] = {
            "global_features": torch.from_numpy(c_array(obs.global_features, np.float32, (1, GWENT_RL_GLOBAL_FEATURE_COUNT))).to(d),
            "decision_kinds": torch.tensor([int(obs.decision_kind)], dtype=torch.long, device=d),
            "source_object_indices": torch.tensor([int(obs.source_object_index)], dtype=torch.long, device=d),
            "object_features": torch.from_numpy(c_array(obs.object_features, np.float32, (1, N, GWENT_RL_OBJECT_FEATURE_COUNT))).to(d),
            "object_mask": torch.from_numpy(c_array(obs.object_mask, np.uint8, (1, N))).to(d),
            "object_card_ids": torch.from_numpy(c_array(obs.object_card_ids, np.int32, (1, N))).to(d),
            "object_zone_ids": torch.from_numpy(c_array(obs.object_zone_ids, np.int32, (1, N))).to(d),
            "object_row_ids": torch.from_numpy(c_array(obs.object_row_ids, np.int32, (1, N))).to(d),
            "object_owner_ids": torch.from_numpy(c_array(obs.object_owner_ids, np.int32, (1, N))).to(d),
            "object_controller_ids": torch.from_numpy(c_array(obs.object_controller_ids, np.int32, (1, N))).to(d),
            "object_slot_indices": torch.from_numpy(c_array(obs.object_slot_indices, np.int32, (1, N))).to(d),
            "option_features": torch.from_numpy(c_array(obs.option_features, np.float32, (1, K, GWENT_RL_OPTION_FEATURE_COUNT))).to(d),
            "option_mask": torch.from_numpy(c_array(obs.option_mask, np.uint8, (1, K))).to(d),
            "option_kind_ids": torch.from_numpy(c_array(obs.option_kind_ids, np.int32, (1, K))).to(d),
            "option_card_ids": torch.from_numpy(c_array(obs.option_card_ids, np.int32, (1, K))).to(d),
            "option_source_object_indices": torch.from_numpy(c_array(obs.option_source_object_indices, np.int32, (1, K))).to(d),
            "option_target_object_indices": torch.from_numpy(c_array(obs.option_target_object_indices, np.int32, (1, K))).to(d),
            "option_target_side_ids": torch.from_numpy(c_array(obs.option_target_side_ids, np.int32, (1, K))).to(d),
            "option_target_zone_ids": torch.from_numpy(c_array(obs.option_target_zone_ids, np.int32, (1, K))).to(d),
            "option_target_row_ids": torch.from_numpy(c_array(obs.option_target_row_ids, np.int32, (1, K))).to(d),
            "option_insert_positions": torch.from_numpy(c_array(obs.option_insert_positions, np.int32, (1, K))).to(d),
            "option_hand_slot_indices": torch.from_numpy(c_array(obs.option_hand_slot_indices, np.int32, (1, K))).to(d),
            "prefix_kind_ids": torch.from_numpy(c_array(obs.prefix_kind_ids, np.int32, (1, P))).to(d),
            "prefix_source_object_indices": torch.from_numpy(c_array(obs.prefix_source_object_indices, np.int32, (1, P))).to(d),
            "prefix_target_object_indices": torch.from_numpy(c_array(obs.prefix_target_object_indices, np.int32, (1, P))).to(d),
            "prefix_card_ids": torch.from_numpy(c_array(obs.prefix_card_ids, np.int32, (1, P))).to(d),
            "prefix_row_ids": torch.from_numpy(c_array(obs.prefix_row_ids, np.int32, (1, P))).to(d),
            "prefix_insert_positions": torch.from_numpy(c_array(obs.prefix_insert_positions, np.int32, (1, P))).to(d),
            "prefix_mask": torch.from_numpy(c_array(obs.prefix_mask, np.uint8, (1, P))).to(d),
        }
        return batch

    def _step(self, option_index: int) -> GwentRlStepResult:
        result = GwentRlStepResult()
        code = self.lib.gwent_rl_env_step_option(self.handle, int(option_index), ct.byref(result))
        if code != GWENT_C_OK:
            raise RuntimeError(f"step failed: {self.result_code_name(code)}")
        return result

    def _choose_ai_action(self, obs: GwentRlObservation) -> tuple[int, float, float]:
        import torch
        from gwent_rl.policy import greedy_actions
        assert self.policy is not None
        with torch.inference_mode():
            tensors = self._obs_to_torch(obs)
            out = self.policy(tensors)
            action = int(greedy_actions(out.logits, tensors["option_mask"])[0].item())
            probs = torch.softmax(out.logits, dim=-1)
            confidence = float(probs[0, action].item())
            value = float(out.values[0].item())
            return action, confidence, value

    def _run_ai_until_human(self) -> None:
        self.last_ai_actions = []
        for _ in range(128):
            obs = self.observation()
            if int(obs.done) != 0 or int(obs.actor_id) != self.AI:
                return
            t0 = time.perf_counter()
            option_index, confidence, value = self._choose_ai_action(obs)
            desc = self.describe_option(obs, option_index)
            result = self._step(option_index)
            desc.update(
                {
                    "confidence": confidence,
                    "value": value,
                    "ms": (time.perf_counter() - t0) * 1000.0,
                    "status": self.action_status_name(int(result.action_status)),
                    "reward_p1": float(result.reward[1]),
                }
            )
            self.last_ai_actions.append(desc)
        raise RuntimeError("AI exceeded 128 sequential decisions without returning control")

    def player_step(self, option_index: int) -> dict[str, Any]:
        obs = self.observation()
        if int(obs.done) != 0:
            raise ValueError("game is already finished")

        actor = int(obs.actor_id)
        if self.mode == self.MODE_HUMAN_VS_AI and actor != self.HUMAN:
            raise ValueError(f"not P0 turn; actor=P{actor}")
        if actor not in (0, 1):
            raise ValueError(f"invalid actor=P{actor}")
        if option_index < 0 or option_index >= int(obs.option_count) or int(obs.option_mask[option_index]) == 0:
            raise ValueError(f"option {option_index} is not legal")

        desc = self.describe_option(obs, option_index)
        result = self._step(option_index)
        desc.update(
            {
                "status": self.action_status_name(int(result.action_status)),
                f"reward_p{actor}": float(result.reward[actor]),
            }
        )
        self.last_human_action = desc

        if self.mode == self.MODE_HUMAN_VS_AI:
            self._run_ai_until_human()
        else:
            # No policy stepping in manual test mode. Every sequential choice and
            # every turn is returned to the browser for explicit user control.
            self.last_ai_actions = []
        return self.public_state()

    def _summary(self, obs: GwentRlObservation) -> dict[str, Any]:
        g = obs.global_features
        actor = int(obs.actor_id)
        if actor not in (0, 1):
            actor = int(obs.perspective_player_id) if int(obs.perspective_player_id) in (0, 1) else 0
        # globals are actor-relative
        actor_score = int(round(float(g[7])))
        opponent_score = int(round(float(g[8])))
        actor_hand = int(round(float(g[14])))
        opponent_hand = int(round(float(g[15])))
        actor_wins = int(round(float(g[20])))
        opponent_wins = int(round(float(g[21])))
        actor_passed = bool(round(float(g[18])))
        opponent_passed = bool(round(float(g[19])))
        if actor == 0:
            p0 = dict(score=actor_score, hand=actor_hand, wins=actor_wins, passed=actor_passed)
            p1 = dict(score=opponent_score, hand=opponent_hand, wins=opponent_wins, passed=opponent_passed)
        else:
            p0 = dict(score=opponent_score, hand=opponent_hand, wins=opponent_wins, passed=opponent_passed)
            p1 = dict(score=actor_score, hand=actor_hand, wins=actor_wins, passed=actor_passed)
        return {
            "round": int(round(float(g[3]))),
            "turn": int(round(float(g[4]))),
            "actor": int(obs.actor_id),
            "decision_kind": int(obs.decision_kind),
            "decision": DECISION_KIND_NAMES.get(int(obs.decision_kind), str(int(obs.decision_kind))),
            "done": bool(int(obs.done)),
            "winner_id": int(obs.winner_id),
            "p0": p0,
            "p1": p1,
        }

    def _public_objects(self, obs: GwentRlObservation) -> list[dict[str, Any]]:
        out = []
        for i in range(int(obs.object_count)):
            if int(obs.object_mask[i]) == 0:
                continue
            owner = int(obs.object_owner_ids[i])
            zone = int(obs.object_zone_ids[i])
            # Human-vs-AI must keep the AI hand private. Manual test mode is an
            # explicit debugging surface, so both hands are exposed there.
            if self.mode == self.MODE_HUMAN_VS_AI and owner == self.AI and zone == 1:
                continue
            card_id = int(obs.object_card_ids[i])
            power = int(round(object_feature(obs, i, 8)))
            armor = int(round(object_feature(obs, i, 10)))
            status: list[str] = []
            for name, fi in [("bleed", 11), ("vitality", 12), ("poison", 13), ("order", 19), ("cd", 20), ("count", 21)]:
                v = int(round(object_feature(obs, i, fi)))
                if v:
                    status.append(f"{name}={v}")
            for name, fi in [("shield", 14), ("locked", 15), ("veil", 16), ("defender", 17), ("doomed", 18)]:
                if bool(round(object_feature(obs, i, fi))):
                    status.append(name)
            out.append(
                {
                    "object_index": i,
                    "entity_id": int(obs.object_entity_ids[i]),
                    "card_id": card_id,
                    "name": self.card_name(card_id),
                    "type": self.card_types.get(card_id, "-"),
                    "owner": owner,
                    "controller": int(obs.object_controller_ids[i]),
                    "zone": zone,
                    "zone_name": ZONE_NAMES.get(zone, str(zone)),
                    "row": int(obs.object_row_ids[i]),
                    "row_name": ROW_NAMES.get(int(obs.object_row_ids[i]), str(int(obs.object_row_ids[i]))),
                    "slot": int(obs.object_slot_indices[i]),
                    "power": power,
                    "armor": armor,
                    "status": status,
                }
            )
        return out

    def _public_row_effects(self) -> list[dict[str, Any]]:
        if not self.handle:
            return []
        effects: list[dict[str, Any]] = []
        for side in (0, 1):
            for row_id in (0, 1):
                for effect_id in ROW_EFFECT_IDS:
                    duration = int(
                        self.lib.gwent_rl_env_row_effect_duration(
                            self.handle, side, row_id, effect_id.encode("utf-8")
                        )
                    )
                    if duration > 0:
                        effects.append(
                            {
                                "side": side,
                                "row": row_id,
                                "id": effect_id,
                                "name": ROW_EFFECT_NAMES.get(effect_id, effect_id),
                                "duration": duration,
                            }
                        )
        return effects

    def public_state(self) -> dict[str, Any]:
        obs = self.observation()
        actions: list[dict[str, Any]] = []
        can_submit = (
            int(obs.done) == 0
            and (self.mode == self.MODE_MANUAL_TEST or int(obs.actor_id) == self.HUMAN)
        )
        if can_submit:
            for i in range(int(obs.option_count)):
                if int(obs.option_mask[i]) != 0:
                    actions.append(self.describe_option(obs, i))
        return {
            "api_version": CORE_API_VERSION,
            "summary": self._summary(obs),
            "objects": self._public_objects(obs),
            "row_effects": self._public_row_effects(),
            "actions": actions,
            "last_human_action": self.last_human_action,
            "last_ai_actions": self.last_ai_actions,
            "checkpoint": str(self.checkpoint) if self.checkpoint is not None else "",
            "checkpoint_update": int(self.metadata.get("update", -1)),
            "device": str(self.device) if self.device is not None else "not-loaded",
            "decks": {"p0": self.player0_deck_id, "p1": self.player1_deck_id},
            "mode": self.mode,
        }


class RequestModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class NewGameRequest(RequestModel):
    seed: int = Field(default=123, ge=0)
    starting_player_id: int = Field(default=-1, ge=-1, le=1)
    player0_deck_id: int = Field(default=0, ge=0)
    player1_deck_id: int = Field(default=0, ge=0)
    mode: str = Field(default=SERVER_MODE, pattern=r"^(human_vs_ai|manual_test)$")


class StepRequest(RequestModel):
    option_index: int = Field(ge=0)


class ReloadRequest(RequestModel):
    checkpoint: Optional[str] = None


app = FastAPI(title="Gwent Local Core", version="1.0")
ENGINE: Optional[HumanVsAiGame] = None
LOCK = threading.RLock()


def engine() -> HumanVsAiGame:
    if ENGINE is None:
        raise HTTPException(status_code=503, detail="server engine not initialized")
    return ENGINE


@app.get("/health")
def health():
    with LOCK:
        e = engine()
        obs = e.observation()
        return {
            "ok": True,
            "api_version": CORE_API_VERSION,
            "schema_version": int(obs.schema_version),
            "checkpoint": str(e.checkpoint) if e.checkpoint is not None else "",
            "checkpoint_update": int(e.metadata.get("update", -1)),
            "device": str(e.device) if e.device is not None else "not-loaded",
            "actor": int(obs.actor_id),
        }


@app.get("/state")
def state():
    with LOCK:
        return engine().public_state()


@app.post("/new")
def new_game(req: NewGameRequest):
    with LOCK:
        try:
            return engine().new_game(
                req.seed,
                req.starting_player_id,
                req.player0_deck_id,
                req.player1_deck_id,
                req.mode,
            )
        except Exception as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.post("/step")
def step(req: StepRequest):
    with LOCK:
        try:
            return engine().player_step(req.option_index)
        except ValueError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc
        except Exception as exc:
            raise HTTPException(status_code=500, detail=str(exc)) from exc


@app.post("/reload")
def reload_checkpoint(req: ReloadRequest):
    with LOCK:
        try:
            e = engine()
            target = req.checkpoint or (str(e.checkpoint) if e.checkpoint is not None else DEFAULT_CHECKPOINT)
            return e.reload_checkpoint(target)
        except Exception as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc


def _resolve_runtime_path(value: str) -> Path:
    path = Path(value).expanduser()
    if not path.is_absolute() and PROJECT_ROOT is not None:
        path = PROJECT_ROOT / path
    return path.resolve()


def _resolve_core_library() -> Path:
    if SERVER_LIBRARY.strip():
        return _resolve_runtime_path(SERVER_LIBRARY)

    if os.name == "nt":
        candidates = [
            r"build-vs\Release\gwent_core.dll",
            r"build-web\Release\gwent_core.dll",
            r"build\Release\gwent_core.dll",
        ]
    else:
        candidates = [
            "build-release/libgwent_core.so",
            ".build/test/libgwent_core.so",
            "build/libgwent_core.so",
        ]

    resolved = [_resolve_runtime_path(item) for item in candidates]
    for path in resolved:
        if path.is_file():
            return path
    # Return the preferred location so the error message is actionable.
    return resolved[0]


def main() -> int:
    mode = SERVER_MODE
    if mode not in HumanVsAiGame.MODES:
        print(f"[ERROR] unsupported SERVER_MODE: {mode}")
        print(f"[HINT] choose one of: {sorted(HumanVsAiGame.MODES)}")
        return 1

    library = _resolve_core_library()
    if not library.is_file():
        print(f"[ERROR] core library does not exist: {library}")
        if os.name == "nt":
            print("[HINT] build it with tools\\windows\\build_core_vs2022.bat")
        else:
            print("[HINT] build gwent_core before starting the local HTTP adapter")
        return 1

    checkpoint: Path | None = None
    if mode == HumanVsAiGame.MODE_HUMAN_VS_AI:
        checkpoint = _resolve_runtime_path(SERVER_CHECKPOINT or DEFAULT_CHECKPOINT)
        if not checkpoint.is_file():
            print(f"[ERROR] human_vs_ai checkpoint is missing: {checkpoint}")
            print('[HINT] For card testing, set SERVER_MODE = "manual_test" and SERVER_CHECKPOINT = None.')
            return 1
        if checkpoint.stat().st_size <= 0:
            print(f"[ERROR] checkpoint is empty: {checkpoint}")
            return 1
    elif SERVER_CHECKPOINT:
        # Explicitly loading a checkpoint in manual mode remains possible, but
        # the normal local card-test path leaves this as None.
        checkpoint = _resolve_runtime_path(SERVER_CHECKPOINT)
        if not checkpoint.is_file():
            print(f"[ERROR] configured checkpoint is missing: {checkpoint}")
            return 1

    global ENGINE
    ENGINE = HumanVsAiGame(
        library=library,
        checkpoint=checkpoint,
        device=SERVER_DEVICE,
        seed=SERVER_SEED,
        mode=mode,
    )

    checkpoint_label = str(ENGINE.checkpoint) if ENGINE.checkpoint is not None else "not-loaded"
    device_label = str(ENGINE.device) if ENGINE.device is not None else "not-loaded"
    print("=" * 72)
    print("[READY] Gwent local Core HTTP")
    print(f"  mode       : {ENGINE.mode}")
    print(f"  library    : {library}")
    print(f"  checkpoint : {checkpoint_label}")
    print(f"  device     : {device_label}")
    print(f"  address    : http://{SERVER_HOST}:{SERVER_PORT}")
    print("  remote     : disabled / loopback only")
    print("=" * 72, flush=True)

    import uvicorn

    uvicorn.run(app, host=SERVER_HOST, port=SERVER_PORT, log_level="info")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
