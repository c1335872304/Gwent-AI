from __future__ import annotations

import argparse
import numpy as np
import torch

from .collector import RlCollector, random_legal_actions
from ._ctypes import GWENT_RL_PASS_WIN, GWENT_RL_PASS_DRAW
from .device import device_name, resolve_torch_device
from .experiment import policy_from_checkpoint
from .policy import CandidatePolicyValueNet, greedy_actions
from .schema import GLOBAL_FEATURE_NAMES


@torch.no_grad()
def evaluate(
    policy: CandidatePolicyValueNet,
    num_envs: int = 64,
    rounds: int = 128,
    seed: int = 0,
    checkpoint: str | None = None,
    opponent: str = "random",
    controlled_player: int = 0,
    deterministic: bool = True,
    reward_mode: str = "terminal",
    library_path: str | None = None,
    device: str | torch.device | None = "auto",
) -> dict[str, float]:
    device_obj = resolve_torch_device(device)
    if checkpoint:
        policy, _metadata = policy_from_checkpoint(checkpoint, device=device_obj)
    else:
        policy = policy.to(device_obj)
    was_training = policy.training
    policy.eval()
    rng = np.random.default_rng(seed)
    decisions = 0
    completed = 0
    wins = 0
    losses = 0
    draws = 0
    reward_sum = 0.0
    illegal_results = 0
    safe_win_opportunities = 0
    safe_win_passes = 0
    safe_draw_opportunities = 0
    safe_draw_passes = 0
    unsafe_pass_opportunities = 0
    unsafe_passes = 0
    controlled_player = int(controlled_player)
    opponent_passed_index = GLOBAL_FEATURE_NAMES.index("opponent_passed")

    with RlCollector(num_envs=num_envs, max_batch_size=num_envs, base_seed=seed, reward_mode=reward_mode, library_path=library_path) as collector:
        for _ in range(rounds):
            batch = collector.collect()
            tensors = batch.to_torch(device=device_obj)
            if opponent == "self":
                out = policy(tensors)
                actions = greedy_actions(out.logits, tensors["option_mask"]).cpu().numpy()
            elif opponent == "random":
                actions = random_legal_actions(batch, rng)
                rows = np.flatnonzero(batch.actor_ids == controlled_player)
                if rows.size:
                    out = policy(tensors)
                    greedy = greedy_actions(out.logits, tensors["option_mask"]).cpu().numpy()
                    actions[rows] = greedy[rows]
            else:
                raise ValueError(f"unknown opponent: {opponent}")

            controlled_rows = np.flatnonzero(batch.actor_ids == controlled_player)
            env_indices = batch.env_indices
            for row in controlled_rows:
                if batch.global_features[row, opponent_passed_index] <= 0.5:
                    continue
                n = int(batch.option_counts[row])
                if not np.any(batch.option_kind_ids[row, :n] == 1):
                    continue
                probe = collector.probe_pass(int(env_indices[row]))
                if not probe.applicable:
                    continue
                option_index = int(actions[row])
                chose_pass = 0 <= option_index < n and int(batch.option_kind_ids[row, option_index]) == 1
                if probe.outcome == GWENT_RL_PASS_WIN:
                    safe_win_opportunities += 1
                    safe_win_passes += int(chose_pass)
                elif probe.outcome == GWENT_RL_PASS_DRAW and probe.hand_diff == 0:
                    safe_draw_opportunities += 1
                    safe_draw_passes += int(chose_pass)
                else:
                    unsafe_pass_opportunities += 1
                    unsafe_passes += int(chose_pass)

            results = collector.apply_actions(batch, actions)
            decisions += batch.count
            illegal_results += int(np.count_nonzero(results.result_codes != 0))
            for i in range(results.count):
                actor = int(batch.actor_ids[i])
                if actor in (0, 1):
                    reward_sum += float(results.rewards[i, actor])
                if int(results.dones[i]) != 0:
                    completed += 1
                    winner = int(results.winner_ids[i])
                    if winner == controlled_player:
                        wins += 1
                    elif winner in (0, 1):
                        losses += 1
                    else:
                        draws += 1
    if was_training:
        policy.train()

    denom = max(completed, 1)
    return {
        "decisions": float(decisions),
        "completed_episodes": float(completed),
        "win_rate": wins / denom,
        "loss_rate": losses / denom,
        "draw_rate": draws / denom,
        "wins": float(wins),
        "losses": float(losses),
        "draws": float(draws),
        "avg_actor_reward": reward_sum / max(decisions, 1),
        "safe_win_opportunities": float(safe_win_opportunities),
        "safe_win_passes": float(safe_win_passes),
        "safe_draw_opportunities": float(safe_draw_opportunities),
        "safe_draw_passes": float(safe_draw_passes),
        "unsafe_pass_opportunities": float(unsafe_pass_opportunities),
        "unsafe_passes": float(unsafe_passes),
        "secured_round_opportunities": float(safe_win_opportunities + safe_draw_opportunities),
        "secured_round_pass_rate": (safe_win_passes + safe_draw_passes) / max(safe_win_opportunities + safe_draw_opportunities, 1),
        "unsafe_pass_rate": unsafe_passes / max(unsafe_pass_opportunities, 1),
        "illegal_result_count": float(illegal_results),
    }


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Evaluate a checkpoint with deterministic actions")
    parser.add_argument("--checkpoint", default=None)
    parser.add_argument("--num-envs", type=int, default=64)
    parser.add_argument("--rounds", type=int, default=128)
    parser.add_argument("--seed", type=int, default=123)
    parser.add_argument("--opponent", choices=["random", "self"], default="random")
    parser.add_argument("--controlled-player", type=int, default=0)
    parser.add_argument("--reward-mode", default="terminal")
    parser.add_argument("--library", default=None)
    parser.add_argument("--device", default="auto")
    args = parser.parse_args(argv)
    stats = evaluate(
        CandidatePolicyValueNet(),
        args.num_envs,
        args.rounds,
        args.seed,
        args.checkpoint,
        opponent=args.opponent,
        controlled_player=args.controlled_player,
        reward_mode=args.reward_mode,
        library_path=args.library,
        device=args.device,
    )
    print(f"device={device_name(resolve_torch_device(args.device))}")
    print(" ".join(f"{k}={v:.6g}" for k, v in stats.items()))


if __name__ == "__main__":
    main()
