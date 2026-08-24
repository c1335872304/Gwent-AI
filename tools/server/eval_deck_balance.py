from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

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


def _side_dict(stats: MatchupStats, *, b_player: int) -> dict:
    games = stats.completed_episodes
    b_wins = stats.a_wins
    a_wins = stats.b_wins
    draws = stats.draws
    return {
        "games": games,
        "b_player": b_player,
        "b_wins": b_wins,
        "a_wins": a_wins,
        "draws": draws,
        "b_win_rate": b_wins / max(games, 1),
        "a_win_rate": a_wins / max(games, 1),
        "draw_rate": draws / max(games, 1),
        "b_score_rate": (b_wins + 0.5 * draws) / max(games, 1),
        "illegal_result_count": stats.illegal_result_count,
        "decisions": stats.decisions,
    }


@torch.no_grad()
def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Evaluate Deck B vs Deck A with separate per-deck checkpoints, "
            "with B tested as both P0 and P1."
        )
    )
    parser.add_argument(
        "--checkpoint-a",
        required=True,
        help="Checkpoint used by Deck A",
    )
    parser.add_argument(
        "--checkpoint-b",
        required=True,
        help="Checkpoint used by Deck B",
    )
    parser.add_argument(
        "--games-per-side",
        type=int,
        default=2500,
        help="Complete games for B(P0)-vs-A(P1) and A(P0)-vs-B(P1); default 2500 each",
    )
    parser.add_argument("--num-envs", type=int, default=128)
    parser.add_argument("--seed", type=int, default=20260820)
    parser.add_argument("--reward-mode", default="terminal")
    parser.add_argument("--library", default="build-release/libgwent_core.so")
    parser.add_argument("--device", default="auto")
    parser.add_argument(
        "--stochastic",
        action="store_true",
        help="Sample from policy probabilities. Default is deterministic/greedy.",
    )
    parser.add_argument("--json-out", default=None, help="Optional output JSON path")
    args = parser.parse_args()

    if args.games_per_side <= 0:
        raise ValueError("--games-per-side must be > 0")

    device = resolve_torch_device(args.device)

    policy_a_deck, meta_a = policy_from_checkpoint(
        args.checkpoint_a,
        device=device,
    )
    policy_b_deck, meta_b = policy_from_checkpoint(
        args.checkpoint_b,
        device=device,
    )

    deterministic = not args.stochastic
    t0 = time.perf_counter()

    # B is P0, A is P1. In evaluate_matchup_once, "policy_a" means tracked policy,
    # not Deck A; therefore a_player=0 makes a_wins correspond to Deck B wins.
    b_as_p0 = evaluate_matchup_once(
        policy_b_deck,
        policy_a_deck,
        num_envs=min(args.num_envs, args.games_per_side),
        games=args.games_per_side,
        seed=args.seed,
        a_player=0,
        deterministic=deterministic,
        reward_mode=args.reward_mode,
        player0_deck="b",
        player1_deck="a",
        library_path=args.library,
        device=device,
    )

    # Swap decks/sides: A is P0, B is P1. Keep the same seed for a paired test.
    b_as_p1 = evaluate_matchup_once(
        policy_b_deck,
        policy_a_deck,
        num_envs=min(args.num_envs, args.games_per_side),
        games=args.games_per_side,
        seed=args.seed,
        a_player=1,
        deterministic=deterministic,
        reward_mode=args.reward_mode,
        player0_deck="a",
        player1_deck="b",
        library_path=args.library,
        device=device,
    )

    s0 = _side_dict(b_as_p0, b_player=0)
    s1 = _side_dict(b_as_p1, b_player=1)

    total_games = int(s0["games"]) + int(s1["games"])
    b_wins = int(s0["b_wins"]) + int(s1["b_wins"])
    a_wins = int(s0["a_wins"]) + int(s1["a_wins"])
    draws = int(s0["draws"]) + int(s1["draws"])
    illegal = int(s0["illegal_result_count"]) + int(s1["illegal_result_count"])
    decisions = int(s0["decisions"]) + int(s1["decisions"])

    overall = {
        "games": total_games,
        "b_wins": b_wins,
        "a_wins": a_wins,
        "draws": draws,
        "b_win_rate": b_wins / max(total_games, 1),
        "a_win_rate": a_wins / max(total_games, 1),
        "draw_rate": draws / max(total_games, 1),
        "b_score_rate": (b_wins + 0.5 * draws) / max(total_games, 1),
        "illegal_result_count": illegal,
        "decisions": decisions,
    }

    result = {
        "checkpoint_a": str(Path(args.checkpoint_a)),
        "checkpoint_b": str(Path(args.checkpoint_b)),
        "device": device_name(device),
        "deterministic": deterministic,
        "seed": args.seed,
        "games_per_side": args.games_per_side,
        "b_as_p0_vs_a_as_p1": s0,
        "b_as_p1_vs_a_as_p0": s1,
        "overall": overall,
        "elapsed_s": time.perf_counter() - t0,
        "checkpoint_meta": {
            "left_update": meta_b.get("update"),
            "right_update": meta_a.get("update"),
        },
    }

    print()
    print("=== Deck B vs Deck A | per-deck checkpoints ===")
    print(f"checkpoint_a : {args.checkpoint_a}")
    print(f"checkpoint_b : {args.checkpoint_b}")
    print(f"device     : {device_name(device)}")
    print(f"policy     : {'greedy/deterministic' if deterministic else 'stochastic sampling'}")
    print()
    print(
        f"B(P0) vs A(P1): games={s0['games']} "
        f"B={s0['b_wins']} ({_pct(float(s0['b_win_rate']))}) "
        f"A={s0['a_wins']} ({_pct(float(s0['a_win_rate']))}) "
        f"draw={s0['draws']} ({_pct(float(s0['draw_rate']))}) "
        f"B-score={_pct(float(s0['b_score_rate']))}"
    )
    print(
        f"A(P0) vs B(P1): games={s1['games']} "
        f"B={s1['b_wins']} ({_pct(float(s1['b_win_rate']))}) "
        f"A={s1['a_wins']} ({_pct(float(s1['a_win_rate']))}) "
        f"draw={s1['draws']} ({_pct(float(s1['draw_rate']))}) "
        f"B-score={_pct(float(s1['b_score_rate']))}"
    )
    print()
    print(
        f"TOTAL: games={overall['games']} "
        f"B={overall['b_wins']} ({_pct(float(overall['b_win_rate']))}) "
        f"A={overall['a_wins']} ({_pct(float(overall['a_win_rate']))}) "
        f"draw={overall['draws']} ({_pct(float(overall['draw_rate']))}) "
        f"B-score={_pct(float(overall['b_score_rate']))} "
        f"illegal={overall['illegal_result_count']}"
    )
    print(f"elapsed={result['elapsed_s']:.1f}s")

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"json_out={out}")


if __name__ == "__main__":
    main()
