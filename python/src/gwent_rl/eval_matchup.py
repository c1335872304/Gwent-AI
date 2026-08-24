from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import torch

from .collector import RlCollector, random_legal_actions
from ._ctypes import GWENT_RL_PASS_WIN, GWENT_RL_PASS_DRAW
from .device import device_name, resolve_torch_device
from .experiment import policy_from_checkpoint
from .policy import CandidatePolicyValueNet, greedy_actions
from .schema import GLOBAL_FEATURE_NAMES


def allocate_weighted_games(total_games: int, weighted_names: list[tuple[str, int]]) -> dict[str, int]:
    """Allocate an exact complete-game budget by integer weights.

    Uses the largest-remainder method so allocations always sum exactly to
    ``total_games`` while preserving deterministic input-order tie breaking.
    Every configured matchup with a positive weight receives at least one game
    when the total budget is large enough to cover all positive-weight entries.
    """
    total_games = int(total_games)
    if total_games <= 0:
        raise ValueError("total_games must be > 0")
    if not weighted_names:
        raise ValueError("weighted_names must not be empty")
    normalized = [(str(name), int(weight)) for name, weight in weighted_names]
    if any(weight <= 0 for _name, weight in normalized):
        raise ValueError("all evaluation matchup weights must be > 0")
    if len({name for name, _weight in normalized}) != len(normalized):
        raise ValueError("evaluation matchup names must be unique")
    if total_games < len(normalized):
        raise ValueError(
            f"total_games={total_games} is too small for {len(normalized)} configured matchups"
        )

    total_weight = sum(weight for _name, weight in normalized)
    exact = [(name, total_games * weight / total_weight) for name, weight in normalized]
    allocation = {name: int(math.floor(value)) for name, value in exact}
    remaining = total_games - sum(allocation.values())
    ranked = sorted(
        enumerate(exact),
        key=lambda item: (-(item[1][1] - math.floor(item[1][1])), item[0]),
    )
    for index, _entry in ranked[:remaining]:
        name = exact[index][0]
        allocation[name] += 1

    if sum(allocation.values()) != total_games:
        raise AssertionError("evaluation allocation does not sum to requested game budget")
    return allocation


@dataclass
class MatchupStats:
    decisions: int = 0
    completed_episodes: int = 0
    a_wins: int = 0
    b_wins: int = 0
    draws: int = 0
    illegal_result_count: int = 0
    a_reward_sum: float = 0.0
    b_reward_sum: float = 0.0
    a_safe_win_opportunities: int = 0
    a_safe_win_passes: int = 0
    a_safe_draw_opportunities: int = 0
    a_safe_draw_passes: int = 0
    a_unsafe_opportunities: int = 0
    a_unsafe_passes: int = 0
    b_safe_win_opportunities: int = 0
    b_safe_win_passes: int = 0
    b_safe_draw_opportunities: int = 0
    b_safe_draw_passes: int = 0
    b_unsafe_opportunities: int = 0
    b_unsafe_passes: int = 0

    def merge(self, other: "MatchupStats") -> None:
        self.decisions += other.decisions
        self.completed_episodes += other.completed_episodes
        self.a_wins += other.a_wins
        self.b_wins += other.b_wins
        self.draws += other.draws
        self.illegal_result_count += other.illegal_result_count
        self.a_reward_sum += other.a_reward_sum
        self.b_reward_sum += other.b_reward_sum
        self.a_safe_win_opportunities += other.a_safe_win_opportunities
        self.a_safe_win_passes += other.a_safe_win_passes
        self.a_safe_draw_opportunities += other.a_safe_draw_opportunities
        self.a_safe_draw_passes += other.a_safe_draw_passes
        self.a_unsafe_opportunities += other.a_unsafe_opportunities
        self.a_unsafe_passes += other.a_unsafe_passes
        self.b_safe_win_opportunities += other.b_safe_win_opportunities
        self.b_safe_win_passes += other.b_safe_win_passes
        self.b_safe_draw_opportunities += other.b_safe_draw_opportunities
        self.b_safe_draw_passes += other.b_safe_draw_passes
        self.b_unsafe_opportunities += other.b_unsafe_opportunities
        self.b_unsafe_passes += other.b_unsafe_passes

    def as_dict(self) -> dict[str, float]:
        denom = max(self.completed_episodes, 1)
        return {
            "decisions": float(self.decisions),
            "completed_episodes": float(self.completed_episodes),
            "a_win_rate": self.a_wins / denom,
            "b_win_rate": self.b_wins / denom,
            "draw_rate": self.draws / denom,
            "a_wins": float(self.a_wins),
            "b_wins": float(self.b_wins),
            "draws": float(self.draws),
            "avg_a_reward": self.a_reward_sum / max(self.decisions, 1),
            "avg_b_reward": self.b_reward_sum / max(self.decisions, 1),
            "a_safe_win_opportunities": float(self.a_safe_win_opportunities),
            "a_safe_win_passes": float(self.a_safe_win_passes),
            "a_safe_draw_opportunities": float(self.a_safe_draw_opportunities),
            "a_safe_draw_passes": float(self.a_safe_draw_passes),
            "a_unsafe_pass_opportunities": float(self.a_unsafe_opportunities),
            "a_unsafe_passes": float(self.a_unsafe_passes),
            "b_safe_win_opportunities": float(self.b_safe_win_opportunities),
            "b_safe_win_passes": float(self.b_safe_win_passes),
            "b_safe_draw_opportunities": float(self.b_safe_draw_opportunities),
            "b_safe_draw_passes": float(self.b_safe_draw_passes),
            "b_unsafe_pass_opportunities": float(self.b_unsafe_opportunities),
            "b_unsafe_passes": float(self.b_unsafe_passes),
            "a_secured_round_opportunities": float(self.a_safe_win_opportunities + self.a_safe_draw_opportunities),
            "a_secured_round_passes": float(self.a_safe_win_passes + self.a_safe_draw_passes),
            "a_secured_round_pass_rate": (self.a_safe_win_passes + self.a_safe_draw_passes) / max(self.a_safe_win_opportunities + self.a_safe_draw_opportunities, 1),
            "a_unsafe_pass_rate": self.a_unsafe_passes / max(self.a_unsafe_opportunities, 1),
            "b_secured_round_opportunities": float(self.b_safe_win_opportunities + self.b_safe_draw_opportunities),
            "b_secured_round_passes": float(self.b_safe_win_passes + self.b_safe_draw_passes),
            "b_secured_round_pass_rate": (self.b_safe_win_passes + self.b_safe_draw_passes) / max(self.b_safe_win_opportunities + self.b_safe_draw_opportunities, 1),
            "b_unsafe_pass_rate": self.b_unsafe_passes / max(self.b_unsafe_opportunities, 1),
            "illegal_result_count": float(self.illegal_result_count),
        }


def _count_secured_passes(collector, batch, actions: np.ndarray, rows: np.ndarray) -> tuple[int, int, int, int, int, int]:
    """Classify PASS using a real counterfactual round resolution.

    safe win: simulated PASS makes the actor win the round.
    safe draw: simulated PASS draws the round AND both players have equal hand counts.
    everything else is tracked as unsafe/non-safe for PASS diagnostics.
    """
    opponent_passed_index = GLOBAL_FEATURE_NAMES.index("opponent_passed")
    safe_win_opp = safe_win_pass = 0
    safe_draw_opp = safe_draw_pass = 0
    unsafe_opp = unsafe_pass = 0
    env_indices = batch.env_indices

    for row in rows:
        if batch.global_features[row, opponent_passed_index] <= 0.5:
            continue
        n = int(batch.option_counts[row])
        pass_options = np.flatnonzero(batch.option_kind_ids[row, :n] == 1)
        if pass_options.size == 0:
            continue

        probe = collector.probe_pass(int(env_indices[row]))
        if not probe.applicable:
            continue
        chosen = int(actions[row])
        chose_pass = 0 <= chosen < n and int(batch.option_kind_ids[row, chosen]) == 1

        if probe.outcome == GWENT_RL_PASS_WIN:
            safe_win_opp += 1
            safe_win_pass += int(chose_pass)
        elif probe.outcome == GWENT_RL_PASS_DRAW and probe.hand_diff == 0:
            safe_draw_opp += 1
            safe_draw_pass += int(chose_pass)
        else:
            unsafe_opp += 1
            unsafe_pass += int(chose_pass)

    return safe_win_opp, safe_win_pass, safe_draw_opp, safe_draw_pass, unsafe_opp, unsafe_pass


@torch.no_grad()
def evaluate_matchup_once(
    policy_a: CandidatePolicyValueNet,
    policy_b: CandidatePolicyValueNet,
    *,
    num_envs: int = 128,
    games: int = 1024,
    seed: int = 0,
    a_player: int = 0,
    deterministic: bool = True,
    reward_mode: str = "terminal",
    player0_deck: str = "a",
    player1_deck: str = "a",
    library_path: str | None = None,
    device: str | torch.device | None = "auto",
) -> MatchupStats:
    device_obj = resolve_torch_device(device)
    was_training_a = policy_a.training
    was_training_b = policy_b.training
    policy_a = policy_a.to(device_obj).eval()
    policy_b = policy_b.to(device_obj).eval()
    a_player = int(a_player)
    if a_player not in (0, 1):
        raise ValueError("a_player must be 0 or 1")
    b_player = 1 - a_player
    games = int(games)
    num_envs = int(num_envs)
    if games <= 0:
        raise ValueError("games must be > 0")
    if num_envs <= 0:
        raise ValueError("num_envs must be > 0")
    num_envs = min(num_envs, games)
    rng = np.random.default_rng(seed)
    stats = MatchupStats()
    # Safety bound only. Evaluation stops on completed games, not collector steps.
    # 1024 decision rounds per logical wave is intentionally generous and turns
    # a broken/non-terminating environment into an explicit error rather than a hang.
    max_collector_rounds = max(1024, math.ceil(games / num_envs) * 1024)
    collector_rounds = 0

    try:
        with RlCollector(
            num_envs=num_envs,
            max_batch_size=num_envs,
            base_seed=seed,
            reward_mode=reward_mode,
            player0_deck=player0_deck,
            player1_deck=player1_deck,
            library_path=library_path,
        ) as collector:
            while stats.completed_episodes < games:
                collector_rounds += 1
                if collector_rounds > max_collector_rounds:
                    raise RuntimeError(
                        "evaluation failed to reach complete-game target: "
                        f"completed={stats.completed_episodes}/{games}, "
                        f"collector_rounds={collector_rounds - 1}, num_envs={num_envs}"
                    )
                batch = collector.collect()
                if batch.count == 0:
                    continue
                tensors = batch.to_torch(device=device_obj)

                actions = random_legal_actions(batch, rng)
                actor_ids = batch.actor_ids
                rows_a = np.flatnonzero(actor_ids == a_player)
                rows_b = np.flatnonzero(actor_ids == b_player)

                if deterministic:
                    if rows_a.size:
                        out_a = policy_a(tensors)
                        greedy_a = greedy_actions(out_a.logits, tensors["option_mask"]).cpu().numpy()
                        actions[rows_a] = greedy_a[rows_a]
                    if rows_b.size:
                        out_b = policy_b(tensors)
                        greedy_b = greedy_actions(out_b.logits, tensors["option_mask"]).cpu().numpy()
                        actions[rows_b] = greedy_b[rows_b]
                else:
                    if rows_a.size:
                        sampled_a, _logp_a, _ent_a, _value_a = policy_a.act(tensors, deterministic=False)
                        sampled_a_np = sampled_a.detach().cpu().numpy().astype(np.uint64)
                        actions[rows_a] = sampled_a_np[rows_a]
                    if rows_b.size:
                        sampled_b, _logp_b, _ent_b, _value_b = policy_b.act(tensors, deterministic=False)
                        sampled_b_np = sampled_b.detach().cpu().numpy().astype(np.uint64)
                        actions[rows_b] = sampled_b_np[rows_b]

                a_wopp, a_wpass, a_dopp, a_dpass, a_uopp, a_upass = _count_secured_passes(collector, batch, actions, rows_a)
                b_wopp, b_wpass, b_dopp, b_dpass, b_uopp, b_upass = _count_secured_passes(collector, batch, actions, rows_b)
                stats.a_safe_win_opportunities += a_wopp
                stats.a_safe_win_passes += a_wpass
                stats.a_safe_draw_opportunities += a_dopp
                stats.a_safe_draw_passes += a_dpass
                stats.a_unsafe_opportunities += a_uopp
                stats.a_unsafe_passes += a_upass
                stats.b_safe_win_opportunities += b_wopp
                stats.b_safe_win_passes += b_wpass
                stats.b_safe_draw_opportunities += b_dopp
                stats.b_safe_draw_passes += b_dpass
                stats.b_unsafe_opportunities += b_uopp
                stats.b_unsafe_passes += b_upass

                results = collector.apply_actions(batch, actions)
                stats.decisions += batch.count
                stats.illegal_result_count += int(np.count_nonzero(results.result_codes != 0))
                for i in range(results.count):
                    if int(results.actor_ids[i]) in (0, 1):
                        stats.a_reward_sum += float(results.rewards[i, a_player])
                        stats.b_reward_sum += float(results.rewards[i, b_player])
                    if int(results.dones[i]) != 0 and stats.completed_episodes < games:
                        stats.completed_episodes += 1
                        winner = int(results.winner_ids[i])
                        if winner == a_player:
                            stats.a_wins += 1
                        elif winner == b_player:
                            stats.b_wins += 1
                        else:
                            stats.draws += 1
    finally:
        if was_training_a:
            policy_a.train()
        if was_training_b:
            policy_b.train()

    return stats


@torch.no_grad()
def evaluate_policies(
    policy_a: CandidatePolicyValueNet,
    policy_b: CandidatePolicyValueNet,
    *,
    num_envs: int = 128,
    games: int = 1024,
    seed: int = 0,
    a_player: int = 0,
    swap_sides: bool = False,
    deterministic: bool = True,
    reward_mode: str = "terminal",
    player0_deck: str = "a",
    player1_deck: str = "a",
    library_path: str | None = None,
    device: str | torch.device | None = "auto",
) -> dict[str, float]:
    device_obj = resolve_torch_device(device)
    if not swap_sides:
        stats = evaluate_matchup_once(
            policy_a,
            policy_b,
            num_envs=num_envs,
            games=games,
            seed=seed,
            a_player=a_player,
            deterministic=deterministic,
            reward_mode=reward_mode,
            player0_deck=player0_deck,
            player1_deck=player1_deck,
            library_path=library_path,
            device=device_obj,
        )
        out = stats.as_dict()
        out["a_player"] = float(a_player)
        out["b_player"] = float(1 - int(a_player))
        return out

    games_a0 = int(games) // 2
    games_a1 = int(games) - games_a0
    total = MatchupStats()

    def run_side(game_budget: int, side: int, side_seed: int) -> MatchupStats:
        if game_budget <= 0:
            return MatchupStats()
        return evaluate_matchup_once(
            policy_a,
            policy_b,
            num_envs=min(num_envs, game_budget),
            games=game_budget,
            seed=side_seed,
            a_player=side,
            deterministic=deterministic,
            reward_mode=reward_mode,
            player0_deck=player0_deck,
            player1_deck=player1_deck,
            library_path=library_path,
            device=device_obj,
        )

    first = run_side(games_a0, 0, seed)
    second = run_side(games_a1, 1, seed + 1_000_003)
    total.merge(first)
    total.merge(second)
    out = total.as_dict()
    out.update(
        {
            "swap_sides": 1.0,
            "a_as_player0_win_rate": first.a_wins / max(first.completed_episodes, 1),
            "a_as_player1_win_rate": second.a_wins / max(second.completed_episodes, 1),
            "episodes_a_as_player0": float(first.completed_episodes),
            "episodes_a_as_player1": float(second.completed_episodes),
        }
    )
    return out


@torch.no_grad()
def evaluate_matchup(
    checkpoint_a: str | Path,
    checkpoint_b: str | Path,
    *,
    num_envs: int = 128,
    games: int = 1024,
    seed: int = 0,
    a_player: int = 0,
    swap_sides: bool = False,
    deterministic: bool = True,
    reward_mode: str = "terminal",
    player0_deck: str = "a",
    player1_deck: str = "a",
    library_path: str | None = None,
    device: str | torch.device | None = "auto",
) -> dict[str, float]:
    device_obj = resolve_torch_device(device)
    policy_a, _meta_a = policy_from_checkpoint(checkpoint_a, device=device_obj)
    policy_b, _meta_b = policy_from_checkpoint(checkpoint_b, device=device_obj)
    return evaluate_policies(
        policy_a,
        policy_b,
        num_envs=num_envs,
        games=games,
        seed=seed,
        a_player=a_player,
        swap_sides=swap_sides,
        deterministic=deterministic,
        reward_mode=reward_mode,
        player0_deck=player0_deck,
        player1_deck=player1_deck,
        library_path=library_path,
        device=device_obj,
    )


def _checkpoint_label(path: str | Path) -> str:
    p = Path(path)
    parts = p.parts
    if "runs" in parts:
        i = parts.index("runs")
        if i + 1 < len(parts):
            return parts[i + 1]
    return str(path)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Evaluate checkpoint A against checkpoint B")
    parser.add_argument("--checkpoint-a", required=True, help="Checkpoint for model A, usually the newer model")
    parser.add_argument("--checkpoint-b", required=True, help="Checkpoint for model B, usually the older baseline")
    parser.add_argument("--num-envs", type=int, default=128)
    parser.add_argument("--games", type=int, default=1024, help="Exact number of complete games to evaluate")
    parser.add_argument("--seed", type=int, default=123)
    parser.add_argument("--a-player", type=int, default=0, choices=[0, 1])
    parser.add_argument("--swap-sides", action="store_true")
    parser.add_argument("--stochastic", action="store_true")
    parser.add_argument("--reward-mode", default="terminal")
    parser.add_argument("--player0-deck", default="a", choices=["a", "b", "deck_a", "deck_b"])
    parser.add_argument("--player1-deck", default="a", choices=["a", "b", "deck_a", "deck_b"])
    parser.add_argument("--library", default=None)
    parser.add_argument("--device", default="auto")
    args = parser.parse_args(argv)

    device_obj = resolve_torch_device(args.device)
    stats = evaluate_matchup(
        args.checkpoint_a,
        args.checkpoint_b,
        num_envs=args.num_envs,
        games=args.games,
        seed=args.seed,
        a_player=args.a_player,
        swap_sides=args.swap_sides,
        deterministic=not args.stochastic,
        reward_mode=args.reward_mode,
        player0_deck=args.player0_deck,
        player1_deck=args.player1_deck,
        library_path=args.library,
        device=device_obj,
    )
    print(f"device={device_name(device_obj)}")
    print(f"checkpoint_a={_checkpoint_label(args.checkpoint_a)}")
    print(f"checkpoint_b={_checkpoint_label(args.checkpoint_b)}")
    print(" ".join(f"{k}={v:.6g}" for k, v in stats.items()))


if __name__ == "__main__":
    main()
