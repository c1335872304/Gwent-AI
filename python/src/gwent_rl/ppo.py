from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

import torch
from torch import nn

from .policy import CandidatePolicyValueNet
from .rollout_buffer import RolloutTensorBatch


@dataclass
class PPOConfig:
    gamma: float = 0.99
    gae_lambda: float = 0.95
    clip_ratio: float = 0.2
    value_coef: float = 0.5
    entropy_coef: float = 0.01
    learning_rate: float = 3e-4
    max_grad_norm: float = 0.5
    epochs: int = 2
    minibatch_size: int = 256
    target_kl: float = 0.03
    value_clip: float = 0.2


@dataclass(frozen=True)
class PPOUpdateStats:
    loss: float
    policy_loss: float
    value_loss: float
    entropy: float
    approx_kl: float
    clip_fraction: float
    grad_norm: float
    explained_variance: float
    updates_applied: int
    early_stopped: bool


def _minibatches(n: int, size: int, device) -> Iterable[torch.Tensor]:
    order = torch.randperm(n, device=device)
    for start in range(0, n, size):
        yield order[start : start + size]


def _slice_obs(obs: dict[str, torch.Tensor], idx: torch.Tensor) -> dict[str, torch.Tensor]:
    return {k: v.index_select(0, idx) for k, v in obs.items()}


def _explained_variance(values: torch.Tensor, returns: torch.Tensor) -> float:
    with torch.no_grad():
        var_y = torch.var(returns)
        if float(var_y.detach().cpu()) < 1e-8:
            return 0.0
        ev = 1.0 - torch.var(returns - values) / (var_y + 1e-8)
        return float(ev.detach().cpu())


def _assert_finite(name: str, tensor: torch.Tensor) -> None:
    if not torch.isfinite(tensor).all():
        raise FloatingPointError(f"non-finite tensor in {name}")


def ppo_update(
    policy: CandidatePolicyValueNet,
    optimizer: torch.optim.Optimizer,
    rollout: RolloutTensorBatch,
    config: PPOConfig,
) -> PPOUpdateStats:
    policy.train()

    n = int(rollout.actions.shape[0])
    if n == 0:
        raise ValueError("empty rollout")
    device = rollout.actions.device
    rows: list[tuple[float, float, float, float, float, float, float, float]] = []
    early_stopped = False

    for _epoch in range(int(config.epochs)):
        for idx in _minibatches(n, int(config.minibatch_size), device):
            obs = _slice_obs(rollout.observations, idx)
            actions = rollout.actions.index_select(0, idx)
            old_log_probs = rollout.old_log_probs.index_select(0, idx)
            returns = rollout.returns.index_select(0, idx)
            advantages = rollout.advantages.index_select(0, idx)
            old_values = rollout.values.index_select(0, idx)

            new_log_probs, entropy, values = policy.evaluate_actions(obs, actions)
            _assert_finite("new_log_probs", new_log_probs)
            _assert_finite("values", values)
            log_ratio = new_log_probs - old_log_probs
            ratio = torch.exp(log_ratio)
            unclipped = ratio * advantages
            clipped = torch.clamp(ratio, 1.0 - config.clip_ratio, 1.0 + config.clip_ratio) * advantages
            policy_loss = -torch.min(unclipped, clipped).mean()

            if config.value_clip > 0.0:
                clipped_values = old_values + (values - old_values).clamp(-config.value_clip, config.value_clip)
                value_loss_unclipped = (returns - values).pow(2)
                value_loss_clipped = (returns - clipped_values).pow(2)
                value_loss = 0.5 * torch.max(value_loss_unclipped, value_loss_clipped).mean()
            else:
                value_loss = 0.5 * (returns - values).pow(2).mean()

            entropy_loss = entropy.mean()
            loss = policy_loss + config.value_coef * value_loss - config.entropy_coef * entropy_loss
            _assert_finite("loss", loss)

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            grad_norm = nn.utils.clip_grad_norm_(policy.parameters(), config.max_grad_norm)
            optimizer.step()

            with torch.no_grad():
                approx_kl = (old_log_probs - new_log_probs).mean().abs()
                clip_fraction = ((ratio - 1.0).abs() > config.clip_ratio).float().mean()
                ev = _explained_variance(values.detach(), returns.detach())
                rows.append((
                    float(loss.detach().cpu()),
                    float(policy_loss.detach().cpu()),
                    float(value_loss.detach().cpu()),
                    float(entropy_loss.detach().cpu()),
                    float(approx_kl.detach().cpu()),
                    float(clip_fraction.detach().cpu()),
                    float(torch.as_tensor(grad_norm).detach().cpu()),
                    ev,
                ))
                if config.target_kl > 0.0 and float(approx_kl.detach().cpu()) > 1.5 * float(config.target_kl):
                    early_stopped = True
                    break
        if early_stopped:
            break

    if not rows:
        raise RuntimeError("PPO update produced no minibatch stats")
    arr = torch.tensor(rows, dtype=torch.float32)
    mean = arr.mean(dim=0)
    return PPOUpdateStats(
        loss=float(mean[0]),
        policy_loss=float(mean[1]),
        value_loss=float(mean[2]),
        entropy=float(mean[3]),
        approx_kl=float(mean[4]),
        clip_fraction=float(mean[5]),
        grad_norm=float(mean[6]),
        explained_variance=float(mean[7]),
        updates_applied=len(rows),
        early_stopped=early_stopped,
    )
