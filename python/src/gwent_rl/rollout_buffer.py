from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping

import numpy as np
import torch

from .collector import ApplyResults, InferenceBatch

_OBJECT_OBS_KEYS = {
    "object_features", "object_mask", "object_entity_ids", "object_card_ids",
    "object_owner_ids", "object_controller_ids", "object_zone_ids",
    "object_row_ids", "object_slot_indices",
}
_OPTION_OBS_KEYS = {
    "option_features", "option_mask", "option_kind_ids", "option_card_ids",
    "option_source_object_indices", "option_target_object_indices",
    "option_target_side_ids", "option_target_zone_ids", "option_target_row_ids", "option_insert_positions",
    "option_hand_slot_indices", "option_stable_hashes",
}
_PREFIX_OBS_KEYS = {
    "prefix_kind_ids", "prefix_source_object_indices",
    "prefix_target_object_indices", "prefix_card_ids", "prefix_row_ids", "prefix_insert_positions", "prefix_mask",
}
_TRAINING_OBS_KEYS = {
    # Python-side routing metadata required by shared_deck_heads_v1 during PPO
    # replay/minibatching.  These are not part of the C RL observation schema.
    "actor_deck_ids", "opponent_deck_ids",
    "global_features", "decision_kinds", "source_object_indices",
    "object_features", "object_mask", "object_card_ids", "object_zone_ids",
    "object_row_ids", "object_owner_ids", "object_controller_ids", "object_slot_indices",
    "option_features", "option_mask", "option_kind_ids", "option_card_ids",
    "option_source_object_indices", "option_target_object_indices",
    "option_target_row_ids", "option_insert_positions", "option_hand_slot_indices",
    "prefix_kind_ids", "prefix_source_object_indices", "prefix_target_object_indices",
    "prefix_card_ids", "prefix_row_ids", "prefix_insert_positions", "prefix_mask",
}


def _compact_observation_copy(batch: InferenceBatch) -> dict[str, np.ndarray]:
    """Copy only the active observation widths for this collector batch.

    The C ABI intentionally exposes fixed maximum object/option arrays, but PPO
    rollouts should not pay that memory cost for every decision.  The model is
    shape-dynamic on these axes, so batches can be stored at their observed
    maxima and padded only when rollout chunks are concatenated.
    """
    copied = batch.copy_numpy()
    object_width = max(1, int(np.max(copied["object_counts"])))
    option_width = max(1, int(np.max(copied["option_counts"])))
    prefix_width = max(1, int(np.max(copied["prefix_counts"])))
    for key in _OBJECT_OBS_KEYS:
        copied[key] = copied[key][:, :object_width].copy()
    for key in _OPTION_OBS_KEYS:
        copied[key] = copied[key][:, :option_width].copy()
    for key in _PREFIX_OBS_KEYS:
        copied[key] = copied[key][:, :prefix_width].copy()
    compact = {key: value for key, value in copied.items() if key in _TRAINING_OBS_KEYS}
    # Observation feature values are small game-state scalars. Store them as
    # fp16 in the host rollout buffer, then the policy converts them back to
    # fp32 on forward. This cuts another large slice of generation memory
    # without changing the C ABI or model arithmetic.
    for key in ("global_features", "object_features", "option_features"):
        compact[key] = compact[key].astype(np.float16, copy=False)
    return compact


def _pad_axis_one(array: np.ndarray, width: int) -> np.ndarray:
    if array.ndim < 2 or array.shape[1] == width:
        return array
    if array.shape[1] > width:
        raise ValueError("cannot pad observation to a smaller width")
    pad = [(0, 0)] * array.ndim
    pad[1] = (0, width - array.shape[1])
    return np.pad(array, pad, mode="constant", constant_values=0)


@dataclass
class RolloutTensorBatch:
    observations: dict[str, torch.Tensor]
    actions: torch.Tensor
    old_log_probs: torch.Tensor
    returns: torch.Tensor
    advantages: torch.Tensor
    values: torch.Tensor
    rewards: torch.Tensor
    dones: torch.Tensor
    env_indices: torch.Tensor
    actor_ids: torch.Tensor
    decision_serials: torch.Tensor


class RolloutBuffer:
    """Stores copied collector batches for PPO-style updates."""

    def __init__(self):
        self.reset()

    def reset(self) -> None:
        self._obs: list[dict[str, np.ndarray]] = []
        self._actions: list[np.ndarray] = []
        self._log_probs: list[np.ndarray] = []
        self._values: list[np.ndarray] = []
        self._rewards: list[np.ndarray] = []
        self._dones: list[np.ndarray] = []
        self._env_indices: list[np.ndarray] = []
        self._actor_ids: list[np.ndarray] = []
        self._decision_serials: list[np.ndarray] = []
        self._returns: np.ndarray | None = None
        self._advantages: np.ndarray | None = None

    @property
    def size(self) -> int:
        return int(sum(arr.shape[0] for arr in self._actions))

    def append(
        self,
        batch: InferenceBatch,
        actions: np.ndarray,
        log_probs: np.ndarray,
        values: np.ndarray,
        results: ApplyResults,
    ) -> None:
        copied = _compact_observation_copy(batch)
        count = int(batch.count)
        if count == 0:
            return
        actions = np.asarray(actions, dtype=np.int64)
        log_probs = np.asarray(log_probs, dtype=np.float32)
        values = np.asarray(values, dtype=np.float32)
        if actions.shape[0] != count:
            raise ValueError(f"actions size {actions.shape[0]} != batch count {count}")
        if results.count != count:
            raise ValueError(f"apply result size {results.count} != batch count {count}")

        # Reward is stored from the actor's perspective for each decision.
        actor_ids = np.asarray(batch.actor_ids, dtype=np.int32).copy()
        rewards = np.zeros((count,), dtype=np.float32)
        for i, actor in enumerate(actor_ids):
            if actor in (0, 1):
                rewards[i] = float(results.rewards[i, int(actor)])
            else:
                rewards[i] = 0.0

        self._obs.append(copied)
        self._actions.append(actions.astype(np.int64).copy())
        self._log_probs.append(log_probs.astype(np.float32).copy())
        self._values.append(values.astype(np.float32).copy())
        self._rewards.append(rewards)
        self._dones.append(results.dones.astype(np.float32).copy())
        self._env_indices.append(np.asarray(batch.env_indices, dtype=np.uint64).copy())
        self._actor_ids.append(actor_ids.copy())
        self._decision_serials.append(np.asarray(batch.decision_serials, dtype=np.uint64).copy())
        self._returns = None
        self._advantages = None


    def retain_complete_episodes(self, max_episodes: int | None = None) -> int:
        """Drop partial trajectories and optionally keep exactly N completed games.

        A game-based PPO generation may cross the target game count in the final
        collector batch and may also contain newly-started partial games from
        auto-reset environments. Only complete terminal-delimited trajectories
        are retained for training.
        """
        if self.size == 0:
            return 0

        obs = self._concat_obs()
        actions = self._concat(self._actions)
        log_probs = self._concat(self._log_probs)
        values = self._concat(self._values)
        rewards = self._concat(self._rewards)
        dones = self._concat(self._dones)
        env_indices = self._concat(self._env_indices)
        actor_ids = self._concat(self._actor_ids)
        decision_serials = self._concat(self._decision_serials)

        pending: dict[int, list[int]] = {}
        complete: list[tuple[int, list[int]]] = []
        for i, env in enumerate(env_indices):
            key = int(env)
            segment = pending.setdefault(key, [])
            segment.append(i)
            if dones[i] != 0:
                complete.append((i, segment.copy()))
                pending[key] = []

        complete.sort(key=lambda item: item[0])
        if max_episodes is not None:
            target = int(max_episodes)
            if target < 0:
                raise ValueError("max_episodes must be non-negative")
            if len(complete) < target:
                raise RuntimeError(f"only {len(complete)} complete episodes available, need {target}")
            complete = complete[:target]

        selected = sorted(index for _end, segment in complete for index in segment)
        indices = np.asarray(selected, dtype=np.int64)
        self._obs = [{key: value[indices].copy() for key, value in obs.items()}]
        self._actions = [actions[indices].copy()]
        self._log_probs = [log_probs[indices].copy()]
        self._values = [values[indices].copy()]
        self._rewards = [rewards[indices].copy()]
        self._dones = [dones[indices].copy()]
        self._env_indices = [env_indices[indices].copy()]
        self._actor_ids = [actor_ids[indices].copy()]
        self._decision_serials = [decision_serials[indices].copy()]
        self._returns = None
        self._advantages = None
        return len(complete)

    def retain_rows(self, mask: np.ndarray) -> int:
        """Keep only selected decision rows while preserving computed GAE.

        This is intentionally a generic post-processing primitive.  For
        learner-vs-frozen training the full trajectories first compute signed
        GAE, then opponent-owned rows are removed before PPO normalization and
        optimization.  Filtering before GAE would break long-horizon credit
        assignment across alternating actors.
        """
        if self.size == 0:
            return 0
        mask = np.asarray(mask, dtype=bool).reshape(-1)
        if mask.shape[0] != self.size:
            raise ValueError(f"row mask size {mask.shape[0]} != rollout size {self.size}")
        indices = np.flatnonzero(mask).astype(np.int64)

        obs = self._concat_obs()
        self._obs = [{key: value[indices].copy() for key, value in obs.items()}]
        for name in (
            "_actions", "_log_probs", "_values", "_rewards", "_dones",
            "_env_indices", "_actor_ids", "_decision_serials",
        ):
            values = self._concat(getattr(self, name))
            setattr(self, name, [values[indices].copy()])
        if self._returns is not None:
            self._returns = self._returns[indices].copy()
        if self._advantages is not None:
            self._advantages = self._advantages[indices].copy()
        return int(indices.shape[0])

    def retain_actor(self, actor_id: int) -> int:
        """Keep only rows owned by one actor after full-trajectory GAE."""
        actor = int(actor_id)
        if actor not in (0, 1):
            raise ValueError("actor_id must be 0 or 1")
        actor_ids = self._concat(self._actor_ids)
        return self.retain_rows(actor_ids == actor)

    def _concat_obs(self) -> dict[str, np.ndarray]:
        if not self._obs:
            raise RuntimeError("empty rollout buffer")
        keys = self._obs[0].keys()
        out: dict[str, np.ndarray] = {}
        for key in keys:
            arrays = [obs[key] for obs in self._obs]
            if arrays[0].ndim >= 2 and key in (_OBJECT_OBS_KEYS | _OPTION_OBS_KEYS | _PREFIX_OBS_KEYS):
                width = max(array.shape[1] for array in arrays)
                arrays = [_pad_axis_one(array, width) for array in arrays]
            out[key] = np.concatenate(arrays, axis=0)
        return out

    def _concat(self, arrays: list[np.ndarray]) -> np.ndarray:
        if not arrays:
            raise RuntimeError("empty rollout buffer")
        return np.concatenate(arrays, axis=0)

    def compute_signed_gae(
        self,
        gamma: float = 0.99,
        gae_lambda: float = 0.95,
        require_complete_episodes: bool = False,
    ) -> tuple[np.ndarray, np.ndarray]:
        """Compute actor-perspective GAE, flipping bootstrap sign on actor switch.

        Game-based generations should pass require_complete_episodes=True; this
        fails fast if any retained environment trajectory ends before terminal.
        """
        rewards = self._concat(self._rewards).astype(np.float32)
        dones = self._concat(self._dones).astype(np.float32)
        values = self._concat(self._values).astype(np.float32)
        env_indices = self._concat(self._env_indices)
        actor_ids = self._concat(self._actor_ids)

        n = rewards.shape[0]
        advantages = np.zeros((n,), dtype=np.float32)
        returns = np.zeros((n,), dtype=np.float32)

        by_env: dict[int, list[int]] = {}
        for i, env in enumerate(env_indices):
            by_env.setdefault(int(env), []).append(i)

        for indices in by_env.values():
            if require_complete_episodes and indices and dones[indices[-1]] == 0.0:
                raise RuntimeError("game-based rollout contains a truncated non-terminal trajectory")
            last_adv = 0.0
            for pos in range(len(indices) - 1, -1, -1):
                i = indices[pos]
                done = bool(dones[i] != 0.0)
                if done or pos == len(indices) - 1:
                    next_value = 0.0
                    next_adv = 0.0
                    sign = 1.0
                    nonterminal = 0.0 if done else 1.0
                else:
                    j = indices[pos + 1]
                    sign = 1.0 if int(actor_ids[i]) == int(actor_ids[j]) else -1.0
                    next_value = float(values[j])
                    next_adv = float(last_adv)
                    nonterminal = 1.0
                delta = float(rewards[i]) + gamma * nonterminal * sign * next_value - float(values[i])
                last_adv = delta + gamma * gae_lambda * nonterminal * sign * next_adv
                advantages[i] = last_adv
                returns[i] = advantages[i] + values[i]

        self._advantages = advantages
        self._returns = returns
        return advantages, returns

    def as_torch(self, device=None, normalize_advantages: bool = True) -> RolloutTensorBatch:
        obs_np = self._concat_obs()
        actions = self._concat(self._actions).astype(np.int64)
        log_probs = self._concat(self._log_probs).astype(np.float32)
        values = self._concat(self._values).astype(np.float32)
        rewards = self._concat(self._rewards).astype(np.float32)
        dones = self._concat(self._dones).astype(np.float32)
        env_indices = self._concat(self._env_indices).astype(np.int64)
        actor_ids = self._concat(self._actor_ids).astype(np.int64)
        decision_serials = self._concat(self._decision_serials).astype(np.int64)
        if self._advantages is None or self._returns is None:
            self.compute_signed_gae()
        advantages = self._advantages.astype(np.float32).copy()
        returns = self._returns.astype(np.float32).copy()
        if normalize_advantages and advantages.size > 1:
            advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)

        observations = {}
        for key, value in obs_np.items():
            copied = value.copy()
            # PyTorch does not implement all indexing ops for uint64.
            # These arrays are identifiers, not arithmetic feature tensors, so
            # int64 is the safest training-side representation.
            if copied.dtype == np.uint64:
                copied = copied.astype(np.int64)
            observations[key] = torch.from_numpy(copied)
        out = RolloutTensorBatch(
            observations=observations,
            actions=torch.from_numpy(actions),
            old_log_probs=torch.from_numpy(log_probs),
            returns=torch.from_numpy(returns),
            advantages=torch.from_numpy(advantages),
            values=torch.from_numpy(values),
            rewards=torch.from_numpy(rewards),
            dones=torch.from_numpy(dones),
            env_indices=torch.from_numpy(env_indices),
            actor_ids=torch.from_numpy(actor_ids),
            decision_serials=torch.from_numpy(decision_serials),
        )
        if device is not None:
            out.observations = {k: v.to(device) for k, v in out.observations.items()}
            for name in ("actions", "old_log_probs", "returns", "advantages", "values", "rewards", "dones", "env_indices", "actor_ids", "decision_serials"):
                setattr(out, name, getattr(out, name).to(device))
        return out
