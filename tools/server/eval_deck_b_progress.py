from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any

import torch

ROOT = Path(__file__).resolve().parents[2]
PYTHON_SRC = ROOT / "python" / "src"
if str(PYTHON_SRC) not in sys.path:
    sys.path.insert(0, str(PYTHON_SRC))

from gwent_rl.device import device_name, resolve_torch_device
from gwent_rl.eval_matchup import MatchupStats, evaluate_matchup_once
from gwent_rl.experiment import policy_from_checkpoint


def _pct(x: float) -> str:
    return f"{100.0 * x:.2f}%"


def _tracked_side_dict(stats: MatchupStats, *, tracked_player: int) -> dict[str, Any]:
    games = int(stats.completed_episodes)
    wins = int(stats.a_wins)
    losses = int(stats.b_wins)
    draws = int(stats.draws)
    return {
        "games": games,
        "tracked_player": tracked_player,
        "wins": wins,
        "losses": losses,
        "draws": draws,
        "win_rate": wins / max(games, 1),
        "loss_rate": losses / max(games, 1),
        "draw_rate": draws / max(games, 1),
        "score_rate": (wins + 0.5 * draws) / max(games, 1),
        "illegal_result_count": int(stats.illegal_result_count),
        "decisions": int(stats.decisions),
    }


def _combine(s0: dict[str, Any], s1: dict[str, Any]) -> dict[str, Any]:
    games = int(s0["games"]) + int(s1["games"])
    wins = int(s0["wins"]) + int(s1["wins"])
    losses = int(s0["losses"]) + int(s1["losses"])
    draws = int(s0["draws"]) + int(s1["draws"])
    illegal = int(s0["illegal_result_count"]) + int(s1["illegal_result_count"])
    decisions = int(s0["decisions"]) + int(s1["decisions"])
    return {
        "games": games,
        "wins": wins,
        "losses": losses,
        "draws": draws,
        "win_rate": wins / max(games, 1),
        "loss_rate": losses / max(games, 1),
        "draw_rate": draws / max(games, 1),
        "score_rate": (wins + 0.5 * draws) / max(games, 1),
        "illegal_result_count": illegal,
        "decisions": decisions,
    }


@torch.no_grad()
def _eval_tracked_vs_opponent(
    tracked_policy,
    opponent_policy,
    *,
    tracked_deck: str,
    opponent_deck: str,
    games_per_side: int,
    num_envs: int,
    seed: int,
    deterministic: bool,
    reward_mode: str,
    library: str,
    device,
) -> dict[str, Any]:
    # Tracked policy as P0.
    p0_stats = evaluate_matchup_once(
        tracked_policy,
        opponent_policy,
        num_envs=min(num_envs, games_per_side),
        games=games_per_side,
        seed=seed,
        a_player=0,
        deterministic=deterministic,
        reward_mode=reward_mode,
        player0_deck=tracked_deck,
        player1_deck=opponent_deck,
        library_path=library,
        device=device,
    )

    # Tracked policy as P1.
    p1_stats = evaluate_matchup_once(
        tracked_policy,
        opponent_policy,
        num_envs=min(num_envs, games_per_side),
        games=games_per_side,
        seed=seed,
        a_player=1,
        deterministic=deterministic,
        reward_mode=reward_mode,
        player0_deck=opponent_deck,
        player1_deck=tracked_deck,
        library_path=library,
        device=device,
    )

    s0 = _tracked_side_dict(p0_stats, tracked_player=0)
    s1 = _tracked_side_dict(p1_stats, tracked_player=1)
    return {
        "as_p0": s0,
        "as_p1": s1,
        "overall": _combine(s0, s1),
    }


def _print_block(title: str, result: dict[str, Any], tracked_name: str) -> None:
    p0 = result["as_p0"]
    p1 = result["as_p1"]
    total = result["overall"]

    print(f"\n[{title}]")
    print(
        f"{tracked_name} as P0: games={p0['games']} "
        f"W={p0['wins']} ({_pct(p0['win_rate'])}) "
        f"L={p0['losses']} ({_pct(p0['loss_rate'])}) "
        f"D={p0['draws']} ({_pct(p0['draw_rate'])}) "
        f"score={_pct(p0['score_rate'])}"
    )
    print(
        f"{tracked_name} as P1: games={p1['games']} "
        f"W={p1['wins']} ({_pct(p1['win_rate'])}) "
        f"L={p1['losses']} ({_pct(p1['loss_rate'])}) "
        f"D={p1['draws']} ({_pct(p1['draw_rate'])}) "
        f"score={_pct(p1['score_rate'])}"
    )
    print(
        f"TOTAL: games={total['games']} "
        f"W={total['wins']} ({_pct(total['win_rate'])}) "
        f"L={total['losses']} ({_pct(total['loss_rate'])}) "
        f"D={total['draws']} ({_pct(total['draw_rate'])}) "
        f"score={_pct(total['score_rate'])} "
        f"illegal={total['illegal_result_count']}"
    )


@torch.no_grad()
def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Evaluate Deck B progress for per-deck checkpoints. "
            "Runs: candidate-B vs fixed-A, baseline-B vs fixed-A, "
            "and candidate-B vs baseline-B."
        )
    )
    parser.add_argument("--candidate-b", required=True, help="New/candidate Deck B checkpoint")
    parser.add_argument("--baseline-b", required=True, help="Current/previous best Deck B checkpoint")
    parser.add_argument("--fixed-a", required=True, help="Fixed Deck A checkpoint")
    parser.add_argument(
        "--external-games-per-side",
        type=int,
        default=2500,
        help="Games per seat for B-vs-A tests; default 2500",
    )
    parser.add_argument(
        "--internal-games-per-side",
        type=int,
        default=1000,
        help="Games per seat for candidate-B vs baseline-B; default 1000",
    )
    parser.add_argument("--num-envs", type=int, default=128)
    parser.add_argument("--seed", type=int, default=20260820)
    parser.add_argument("--reward-mode", default="terminal")
    parser.add_argument("--library", default="build-release/libgwent_core.so")
    parser.add_argument("--device", default="auto")
    parser.add_argument(
        "--stochastic",
        action="store_true",
        help="Sample policy actions instead of greedy/deterministic evaluation",
    )
    parser.add_argument("--json-out", default=None, help="Optional output JSON path")
    args = parser.parse_args()

    if args.external_games_per_side <= 0:
        raise ValueError("--external-games-per-side must be > 0")
    if args.internal_games_per_side <= 0:
        raise ValueError("--internal-games-per-side must be > 0")

    device = resolve_torch_device(args.device)
    deterministic = not args.stochastic

    candidate_b, candidate_meta = policy_from_checkpoint(args.candidate_b, device=device)
    baseline_b, baseline_meta = policy_from_checkpoint(args.baseline_b, device=device)
    fixed_a, fixed_a_meta = policy_from_checkpoint(args.fixed_a, device=device)

    t0 = time.perf_counter()

    candidate_external = _eval_tracked_vs_opponent(
        candidate_b,
        fixed_a,
        tracked_deck="b",
        opponent_deck="a",
        games_per_side=args.external_games_per_side,
        num_envs=args.num_envs,
        seed=args.seed,
        deterministic=deterministic,
        reward_mode=args.reward_mode,
        library=args.library,
        device=device,
    )

    baseline_external = _eval_tracked_vs_opponent(
        baseline_b,
        fixed_a,
        tracked_deck="b",
        opponent_deck="a",
        games_per_side=args.external_games_per_side,
        num_envs=args.num_envs,
        seed=args.seed,
        deterministic=deterministic,
        reward_mode=args.reward_mode,
        library=args.library,
        device=device,
    )

    internal = _eval_tracked_vs_opponent(
        candidate_b,
        baseline_b,
        tracked_deck="b",
        opponent_deck="b",
        games_per_side=args.internal_games_per_side,
        num_envs=args.num_envs,
        seed=args.seed + 100000,
        deterministic=deterministic,
        reward_mode=args.reward_mode,
        library=args.library,
        device=device,
    )

    candidate_ext = candidate_external["overall"]
    baseline_ext = baseline_external["overall"]
    internal_total = internal["overall"]

    external_win_delta = candidate_ext["win_rate"] - baseline_ext["win_rate"]
    external_score_delta = candidate_ext["score_rate"] - baseline_ext["score_rate"]

    elapsed = time.perf_counter() - t0

    payload = {
        "schema_version": "gwent-deck-b-progress-v1",
        "candidate_b": str(Path(args.candidate_b)),
        "baseline_b": str(Path(args.baseline_b)),
        "fixed_a": str(Path(args.fixed_a)),
        "candidate_b_update": candidate_meta.get("update"),
        "baseline_b_update": baseline_meta.get("update"),
        "fixed_a_update": fixed_a_meta.get("update"),
        "device": device_name(device),
        "policy_mode": "stochastic" if args.stochastic else "greedy/deterministic",
        "external_games_per_side": args.external_games_per_side,
        "internal_games_per_side": args.internal_games_per_side,
        "candidate_external_vs_fixed_a": candidate_external,
        "baseline_external_vs_fixed_a": baseline_external,
        "candidate_internal_vs_baseline_b": internal,
        "summary": {
            "candidate_external_win_rate": candidate_ext["win_rate"],
            "baseline_external_win_rate": baseline_ext["win_rate"],
            "external_win_rate_delta": external_win_delta,
            "candidate_external_score_rate": candidate_ext["score_rate"],
            "baseline_external_score_rate": baseline_ext["score_rate"],
            "external_score_rate_delta": external_score_delta,
            "candidate_internal_win_rate": internal_total["win_rate"],
            "candidate_internal_score_rate": internal_total["score_rate"],
            "external_improved": external_score_delta > 0.0,
            "internal_non_losing": internal_total["score_rate"] >= 0.5,
        },
        "elapsed_s": elapsed,
    }

    print("\n=== Deck B Progress Evaluation | per-deck checkpoints ===")
    print(f"candidate_b : {args.candidate_b}")
    print(f"baseline_b  : {args.baseline_b}")
    print(f"fixed_a     : {args.fixed_a}")
    print(f"device      : {device_name(device)}")
    print(f"policy      : {'stochastic' if args.stochastic else 'greedy/deterministic'}")

    _print_block(
        "EXTERNAL candidate B vs fixed A",
        candidate_external,
        "B_candidate",
    )
    _print_block(
        "EXTERNAL baseline B vs fixed A",
        baseline_external,
        "B_baseline",
    )
    _print_block(
        "INTERNAL candidate B vs baseline B",
        internal,
        "B_candidate",
    )

    print("\n[SUMMARY]")
    print(
        f"external candidate win : {_pct(candidate_ext['win_rate'])} "
        f"(score {_pct(candidate_ext['score_rate'])})"
    )
    print(
        f"external baseline win  : {_pct(baseline_ext['win_rate'])} "
        f"(score {_pct(baseline_ext['score_rate'])})"
    )
    print(
        f"external delta         : win {external_win_delta * 100:+.2f} pp, "
        f"score {external_score_delta * 100:+.2f} pp"
    )
    print(
        f"internal candidate     : win {_pct(internal_total['win_rate'])}, "
        f"score {_pct(internal_total['score_rate'])}"
    )
    print(f"elapsed                : {elapsed:.1f}s")

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"json_out               : {out}")


if __name__ == "__main__":
    main()
