from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import torch

from .collector import RlCollector
from .policy import CandidatePolicyValueNet
from .rollout_buffer import RolloutBuffer


@dataclass(frozen=True)
class RunnerStats:
    decisions: int
    collector_steps: int
    completed_episodes: int


class RolloutRunner:
    """Connects RlCollector collect/apply to a policy and RolloutBuffer."""

    def __init__(self, collector: RlCollector, policy: CandidatePolicyValueNet, device=None):
        self.collector = collector
        self.policy = policy
        self.device = device

    @torch.no_grad()
    def collect_steps(self, num_steps: int, deterministic: bool = False) -> tuple[RolloutBuffer, RunnerStats]:
        buffer = RolloutBuffer()
        decisions = 0
        completed = 0
        while decisions < int(num_steps):
            batch = self.collector.collect()
            if batch.count == 0:
                continue
            tensors = batch.to_torch(device=self.device)
            actions, log_probs, _entropy, values = self.policy.act(tensors, deterministic=deterministic)
            actions_np = actions.detach().cpu().numpy().astype(np.uint64)
            results = self.collector.apply_actions(batch, actions_np)
            buffer.append(
                batch=batch,
                actions=actions_np,
                log_probs=log_probs.detach().cpu().numpy(),
                values=values.detach().cpu().numpy(),
                results=results,
            )
            decisions += batch.count
            completed += int(np.count_nonzero(results.dones))
        return buffer, RunnerStats(
            decisions=buffer.size,
            collector_steps=self.collector.total_steps,
            completed_episodes=completed,
        )

    @torch.no_grad()
    def collect_games(self, num_games: int, deterministic: bool = False) -> tuple[RolloutBuffer, RunnerStats]:
        """Collect exactly N complete games without discarding trajectories.

        Exactly N games are allowed to start. Once the start budget is exhausted,
        finished environments retire and unfinished games drain to terminal under
        the same frozen latest policy. No partial trajectory is used by PPO.
        """
        target = int(num_games)
        if target <= 0:
            raise ValueError("num_games must be positive")
        if target < self.collector.env_count:
            raise ValueError(
                f"num_games={target} must be >= num_envs={self.collector.env_count} "
                "for exact no-discard collection"
            )

        self.collector.set_game_limit(target)
        buffer = RolloutBuffer()
        completed = 0

        while True:
            batch = self.collector.collect()
            if batch.count == 0:
                break

            tensors = batch.to_torch(device=self.device)
            actions, log_probs, _entropy, values = self.policy.act(tensors, deterministic=deterministic)
            actions_np = actions.detach().cpu().numpy().astype(np.uint64)
            results = self.collector.apply_actions(batch, actions_np)
            buffer.append(
                batch=batch,
                actions=actions_np,
                log_probs=log_probs.detach().cpu().numpy(),
                values=values.detach().cpu().numpy(),
                results=results,
            )
            completed += int(np.count_nonzero(results.dones))

        kept = buffer.retain_complete_episodes()
        if kept != target or completed != target:
            raise RuntimeError(
                f"exact game collection failed: completed={completed}, retained={kept}, target={target}"
            )
        return buffer, RunnerStats(
            decisions=buffer.size,
            collector_steps=self.collector.total_steps,
            completed_episodes=kept,
        )

class LearnerVsFrozenRunner:
    """Collect complete games where only one actor is owned by the learner.

    The frozen opponent chooses actions for the other actor, while the learner's
    value head evaluates *every* state in the complete trajectory.  This keeps
    signed actor-perspective GAE internally consistent.  Callers must compute
    GAE on the complete rollout before filtering to the learner actor.

    This runner is intentionally deck-agnostic.  Deck assignment is owned by
    the collector; ``learner_actor`` only states which player seat is trainable.
    """

    def __init__(
        self,
        collector: RlCollector,
        learner_policy: CandidatePolicyValueNet,
        frozen_policy: CandidatePolicyValueNet,
        *,
        learner_actor: int,
        device=None,
    ):
        if int(learner_actor) not in (0, 1):
            raise ValueError("learner_actor must be 0 or 1")
        self.collector = collector
        self.learner_policy = learner_policy
        self.frozen_policy = frozen_policy
        self.learner_actor = int(learner_actor)
        self.device = device
        self.frozen_policy.eval()
        for parameter in self.frozen_policy.parameters():
            parameter.requires_grad_(False)

    @torch.no_grad()
    def collect_games(self, num_games: int, deterministic: bool = False) -> tuple[RolloutBuffer, RunnerStats]:
        target = int(num_games)
        if target <= 0:
            raise ValueError("num_games must be positive")
        if target < self.collector.env_count:
            raise ValueError(
                f"num_games={target} must be >= num_envs={self.collector.env_count} "
                "for exact no-discard collection"
            )

        self.collector.set_game_limit(target)
        buffer = RolloutBuffer()
        completed = 0

        while True:
            batch = self.collector.collect()
            if batch.count == 0:
                break

            tensors = batch.to_torch(device=self.device)

            # Learner values are used for the entire trajectory, including
            # frozen-opponent turns.  This avoids mixing two critics in one GAE
            # chain when actor perspective flips between consecutive decisions.
            learner_actions, learner_log_probs, _learner_entropy, learner_values = self.learner_policy.act(
                tensors, deterministic=deterministic
            )

            actor_ids = torch.as_tensor(batch.actor_ids, device=learner_actions.device, dtype=torch.long)
            learner_mask = actor_ids == self.learner_actor
            opponent_rows = torch.nonzero(~learner_mask, as_tuple=False).flatten()
            actions = learner_actions.clone()
            log_probs = learner_log_probs.clone()
            if opponent_rows.numel() > 0:
                frozen_tensors = {}
                for key, value in tensors.items():
                    source = value.to(dtype=torch.int64) if value.dtype == torch.uint64 else value
                    frozen_tensors[key] = source.index_select(0, opponent_rows)
                frozen_actions, frozen_log_probs, _frozen_entropy, _frozen_values = self.frozen_policy.act(
                    frozen_tensors, deterministic=deterministic
                )
                actions.index_copy_(0, opponent_rows, frozen_actions)
                log_probs.index_copy_(0, opponent_rows, frozen_log_probs)

            actions_np = actions.detach().cpu().numpy().astype(np.uint64)
            results = self.collector.apply_actions(batch, actions_np)
            buffer.append(
                batch=batch,
                actions=actions_np,
                log_probs=log_probs.detach().cpu().numpy(),
                values=learner_values.detach().cpu().numpy(),
                results=results,
            )
            completed += int(np.count_nonzero(results.dones))

        kept = buffer.retain_complete_episodes()
        if kept != target or completed != target:
            raise RuntimeError(
                f"exact focused-game collection failed: completed={completed}, retained={kept}, target={target}"
            )
        return buffer, RunnerStats(
            decisions=buffer.size,
            collector_steps=self.collector.total_steps,
            completed_episodes=kept,
        )

