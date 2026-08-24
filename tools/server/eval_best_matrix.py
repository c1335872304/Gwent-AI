from __future__ import annotations

import argparse
import csv
import json
import math
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
PYTHON_SRC = ROOT / "python" / "src"
if str(PYTHON_SRC) not in sys.path:
    sys.path.insert(0, str(PYTHON_SRC))

from gwent_rl.collector import RlCollector, random_legal_actions
from gwent_rl.device import device_name, resolve_torch_device
from gwent_rl.experiment import policy_from_checkpoint
from gwent_rl.policy import CandidatePolicyValueNet, greedy_actions


DEFAULT_UPDATES = [4, 8, 12, 16, 20, 24, 28, 36, 40, 48, 52, 88]


@dataclass
class SideStats:
    games: int = 0
    a_wins: int = 0
    b_wins: int = 0
    draws: int = 0
    illegal: int = 0
    decisions: int = 0

    @property
    def a_win_rate(self) -> float:
        return self.a_wins / self.games if self.games else 0.0

    @property
    def b_win_rate(self) -> float:
        return self.b_wins / self.games if self.games else 0.0

    @property
    def draw_rate(self) -> float:
        return self.draws / self.games if self.games else 0.0

    @property
    def a_score_rate(self) -> float:
        return (self.a_wins + 0.5 * self.draws) / self.games if self.games else 0.0


@dataclass
class PairStats:
    label_a: str
    label_b: str
    side_a0: SideStats
    side_a1: SideStats
    elapsed_s: float

    @property
    def games(self) -> int:
        return self.side_a0.games + self.side_a1.games

    @property
    def a_wins(self) -> int:
        return self.side_a0.a_wins + self.side_a1.a_wins

    @property
    def b_wins(self) -> int:
        return self.side_a0.b_wins + self.side_a1.b_wins

    @property
    def draws(self) -> int:
        return self.side_a0.draws + self.side_a1.draws

    @property
    def illegal(self) -> int:
        return self.side_a0.illegal + self.side_a1.illegal

    @property
    def a_win_rate(self) -> float:
        return self.a_wins / self.games if self.games else 0.0

    @property
    def b_win_rate(self) -> float:
        return self.b_wins / self.games if self.games else 0.0

    @property
    def draw_rate(self) -> float:
        return self.draws / self.games if self.games else 0.0

    @property
    def a_score_rate(self) -> float:
        return (self.a_wins + 0.5 * self.draws) / self.games if self.games else 0.0

    def as_row(self) -> dict[str, object]:
        return {
            "a": self.label_a,
            "b": self.label_b,
            "games": self.games,
            "a_wins": self.a_wins,
            "b_wins": self.b_wins,
            "draws": self.draws,
            "a_win_rate": self.a_win_rate,
            "b_win_rate": self.b_win_rate,
            "draw_rate": self.draw_rate,
            "a_score_rate": self.a_score_rate,
            "illegal_result_count": self.illegal,
            "a_as_p0_games": self.side_a0.games,
            "a_as_p0_win_rate": self.side_a0.a_win_rate,
            "a_as_p1_games": self.side_a1.games,
            "a_as_p1_win_rate": self.side_a1.a_win_rate,
            "elapsed_s": self.elapsed_s,
        }


def parse_updates(raw: str) -> list[int]:
    if not raw.strip():
        return list(DEFAULT_UPDATES)
    out: list[int] = []
    for item in raw.split(","):
        item = item.strip().lower().removeprefix("u")
        if item:
            out.append(int(item))
    if len(set(out)) != len(out):
        raise ValueError("duplicate updates in --updates")
    return out


def checkpoint_path(checkpoint_dir: Path, update: int) -> Path:
    if update == 0:
        return checkpoint_dir / "initial.pt"
    return checkpoint_dir / f"update_{update:06d}.pt"


def label(update: int) -> str:
    return f"u{update}"


@torch.no_grad()
def evaluate_side_exact(
    policy_a: CandidatePolicyValueNet,
    policy_b: CandidatePolicyValueNet,
    *,
    target_games: int,
    a_player: int,
    num_envs: int,
    seed: int,
    reward_mode: str,
    player0_deck: str,
    player1_deck: str,
    library_path: str | None,
    device: torch.device,
    stochastic: bool,
) -> SideStats:
    """Count exactly target_games completed matches for one A-side assignment.

    The collector may finish several games in the last batch. Extra completions in
    that final batch are deliberately ignored so every pair contributes exactly
    the same number of scored games.
    """
    if target_games <= 0:
        return SideStats()
    if a_player not in (0, 1):
        raise ValueError("a_player must be 0 or 1")

    b_player = 1 - a_player
    rng = np.random.default_rng(seed)
    stats = SideStats()

    policy_a = policy_a.to(device).eval()
    policy_b = policy_b.to(device).eval()

    with RlCollector(
        num_envs=num_envs,
        max_batch_size=num_envs,
        base_seed=seed,
        reward_mode=reward_mode,
        player0_deck=player0_deck,
        player1_deck=player1_deck,
        library_path=library_path,
    ) as collector:
        while stats.games < target_games:
            batch = collector.collect()
            if batch.count == 0:
                continue

            tensors = batch.to_torch(device=device)
            actions = random_legal_actions(batch, rng)
            actor_ids = batch.actor_ids
            rows_a = np.flatnonzero(actor_ids == a_player)
            rows_b = np.flatnonzero(actor_ids == b_player)

            if stochastic:
                if rows_a.size:
                    sampled_a, *_ = policy_a.act(tensors, deterministic=False)
                    sampled_a_np = sampled_a.detach().cpu().numpy().astype(np.uint64)
                    actions[rows_a] = sampled_a_np[rows_a]
                if rows_b.size:
                    sampled_b, *_ = policy_b.act(tensors, deterministic=False)
                    sampled_b_np = sampled_b.detach().cpu().numpy().astype(np.uint64)
                    actions[rows_b] = sampled_b_np[rows_b]
            else:
                if rows_a.size:
                    out_a = policy_a(tensors)
                    greedy_a = greedy_actions(out_a.logits, tensors["option_mask"]).cpu().numpy()
                    actions[rows_a] = greedy_a[rows_a]
                if rows_b.size:
                    out_b = policy_b(tensors)
                    greedy_b = greedy_actions(out_b.logits, tensors["option_mask"]).cpu().numpy()
                    actions[rows_b] = greedy_b[rows_b]

            results = collector.apply_actions(batch, actions)
            stats.decisions += int(batch.count)
            stats.illegal += int(np.count_nonzero(results.result_codes != 0))

            for i in range(results.count):
                if stats.games >= target_games:
                    break
                if int(results.dones[i]) == 0:
                    continue
                winner = int(results.winner_ids[i])
                stats.games += 1
                if winner == a_player:
                    stats.a_wins += 1
                elif winner == b_player:
                    stats.b_wins += 1
                else:
                    stats.draws += 1

    return stats


def evaluate_pair_exact(
    policy_a: CandidatePolicyValueNet,
    policy_b: CandidatePolicyValueNet,
    *,
    label_a: str,
    label_b: str,
    games: int,
    num_envs: int,
    seed: int,
    reward_mode: str,
    player0_deck: str,
    player1_deck: str,
    library_path: str | None,
    device: torch.device,
    stochastic: bool,
) -> PairStats:
    # If odd, A-as-P1 gets the extra game. Normally use an even number such as 500.
    games_a0 = games // 2
    games_a1 = games - games_a0
    t0 = time.perf_counter()

    # Intentionally use the same base seed for both halves. This gives the two
    # side assignments closely matched shuffle/start conditions and reduces noise.
    s0 = evaluate_side_exact(
        policy_a,
        policy_b,
        target_games=games_a0,
        a_player=0,
        num_envs=num_envs,
        seed=seed,
        reward_mode=reward_mode,
        player0_deck=player0_deck,
        player1_deck=player1_deck,
        library_path=library_path,
        device=device,
        stochastic=stochastic,
    )
    s1 = evaluate_side_exact(
        policy_a,
        policy_b,
        target_games=games_a1,
        a_player=1,
        num_envs=num_envs,
        seed=seed,
        reward_mode=reward_mode,
        player0_deck=player0_deck,
        player1_deck=player1_deck,
        library_path=library_path,
        device=device,
        stochastic=stochastic,
    )
    return PairStats(label_a, label_b, s0, s1, time.perf_counter() - t0)


def load_existing_details(path: Path) -> dict[tuple[str, str], dict[str, str]]:
    if not path.exists():
        return {}
    with path.open("r", newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    return {(row["a"], row["b"]): row for row in rows}


def write_details(path: Path, rows: list[dict[str, object]]) -> None:
    fieldnames = [
        "a", "b", "games", "a_wins", "b_wins", "draws",
        "a_win_rate", "b_win_rate", "draw_rate", "a_score_rate",
        "illegal_result_count", "a_as_p0_games", "a_as_p0_win_rate",
        "a_as_p1_games", "a_as_p1_win_rate", "elapsed_s",
    ]
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)


def float_from(row: dict[str, object] | dict[str, str], key: str) -> float:
    return float(row[key])


def int_from(row: dict[str, object] | dict[str, str], key: str) -> int:
    return int(float(row[key]))


def build_outputs(out_dir: Path, labels: list[str], rows: list[dict[str, object] | dict[str, str]]) -> None:
    index = {name: i for i, name in enumerate(labels)}
    n = len(labels)
    win = [[math.nan] * n for _ in range(n)]
    score = [[math.nan] * n for _ in range(n)]
    draw = [[math.nan] * n for _ in range(n)]
    for i in range(n):
        win[i][i] = 0.5
        score[i][i] = 0.5
        draw[i][i] = 0.0

    totals = {
        name: {"games": 0, "wins": 0, "losses": 0, "draws": 0, "points": 0.0, "illegal": 0}
        for name in labels
    }

    for row in rows:
        a = str(row["a"])
        b = str(row["b"])
        if a not in index or b not in index:
            continue
        i, j = index[a], index[b]
        games = int_from(row, "games")
        a_wins = int_from(row, "a_wins")
        b_wins = int_from(row, "b_wins")
        draws = int_from(row, "draws")
        illegal = int_from(row, "illegal_result_count")
        if games <= 0:
            continue

        win[i][j] = a_wins / games
        win[j][i] = b_wins / games
        score[i][j] = (a_wins + 0.5 * draws) / games
        score[j][i] = (b_wins + 0.5 * draws) / games
        draw[i][j] = draw[j][i] = draws / games

        totals[a]["games"] += games
        totals[a]["wins"] += a_wins
        totals[a]["losses"] += b_wins
        totals[a]["draws"] += draws
        totals[a]["points"] += a_wins + 0.5 * draws
        totals[a]["illegal"] += illegal

        totals[b]["games"] += games
        totals[b]["wins"] += b_wins
        totals[b]["losses"] += a_wins
        totals[b]["draws"] += draws
        totals[b]["points"] += b_wins + 0.5 * draws
        totals[b]["illegal"] += illegal

    def write_matrix(path: Path, matrix: list[list[float]], percent: bool = True) -> None:
        with path.open("w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(["row\\col", *labels])
            for name, row in zip(labels, matrix):
                vals = []
                for v in row:
                    if math.isnan(v):
                        vals.append("")
                    elif percent:
                        vals.append(f"{100.0 * v:.2f}")
                    else:
                        vals.append(f"{v:.6f}")
                w.writerow([name, *vals])

    write_matrix(out_dir / "matrix_winrate_pct.csv", win, percent=True)
    write_matrix(out_dir / "matrix_score_pct.csv", score, percent=True)
    write_matrix(out_dir / "matrix_draw_pct.csv", draw, percent=True)

    ranking = []
    for name in labels:
        t = totals[name]
        games = int(t["games"])
        ranking.append({
            "model": name,
            "games": games,
            "wins": int(t["wins"]),
            "losses": int(t["losses"]),
            "draws": int(t["draws"]),
            "win_rate": (int(t["wins"]) / games) if games else 0.0,
            "score_rate": (float(t["points"]) / games) if games else 0.0,
            "illegal_result_count": int(t["illegal"]),
        })
    ranking.sort(key=lambda x: (x["score_rate"], x["win_rate"]), reverse=True)

    with (out_dir / "ranking.csv").open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=list(ranking[0].keys()))
        w.writeheader()
        w.writerows(ranking)

    # Compact terminal-friendly table.
    pretty = []
    pretty.append("Historical BEST round-robin win rate matrix (%)")
    pretty.append("cell[row,col] = row model's raw win rate vs column model")
    pretty.append("")
    width = max(7, max(len(x) for x in labels) + 1)
    pretty.append("".ljust(width) + "".join(x.rjust(width) for x in labels))
    for name, row in zip(labels, win):
        cells = []
        for v in row:
            cells.append(("-" if math.isnan(v) else f"{100*v:.1f}").rjust(width))
        pretty.append(name.ljust(width) + "".join(cells))
    pretty.append("")
    pretty.append("Ranking by score rate (win=1, draw=0.5, loss=0):")
    for k, r in enumerate(ranking, 1):
        pretty.append(
            f"{k:2d}. {r['model']:>5s} score={100*r['score_rate']:.2f}% "
            f"win={100*r['win_rate']:.2f}% W/L/D={r['wins']}/{r['losses']}/{r['draws']}"
        )
    (out_dir / "matrix_pretty.txt").write_text("\n".join(pretty) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Exact-game round-robin matrix for historical Gwent best checkpoints"
    )
    parser.add_argument(
        "--checkpoint-dir",
        default="runs/train_25w_v10/checkpoints",
        help="Directory containing update_XXXXXX.pt files",
    )
    parser.add_argument(
        "--updates",
        default=",".join(map(str, DEFAULT_UPDATES)),
        help="Comma-separated update numbers",
    )
    parser.add_argument("--games-per-pair", type=int, default=500)
    parser.add_argument("--num-envs", type=int, default=128)
    parser.add_argument("--seed", type=int, default=20260817)
    parser.add_argument("--reward-mode", default="terminal")
    parser.add_argument("--player0-deck", default="a", choices=("a", "b"))
    parser.add_argument("--player1-deck", default="a", choices=("a", "b"))
    parser.add_argument("--library", default="build-release/libgwent_core.so")
    parser.add_argument("--device", default="auto")
    parser.add_argument("--stochastic", action="store_true", help="Sample policy actions instead of greedy actions")
    parser.add_argument("--out-dir", default="runs/train_25w_v10/best_matrix_500")
    parser.add_argument("--fresh", action="store_true", help="Ignore existing details.csv and rerun every pair")
    args = parser.parse_args()

    if args.games_per_pair <= 0:
        raise SystemExit("--games-per-pair must be positive")
    if args.games_per_pair % 2 != 0:
        print("[WARN] odd --games-per-pair: one side assignment will receive one extra game", flush=True)

    checkpoint_dir = Path(args.checkpoint_dir)
    updates = parse_updates(args.updates)
    labels = [label(u) for u in updates]
    paths = {label(u): checkpoint_path(checkpoint_dir, u) for u in updates}
    missing = [str(p) for p in paths.values() if not p.is_file()]
    if missing:
        raise SystemExit("Missing checkpoints:\n  " + "\n  ".join(missing))

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    details_path = out_dir / "details.csv"

    device = resolve_torch_device(args.device)
    print(f"[START] models={len(labels)} pairs={len(labels)*(len(labels)-1)//2} "
          f"games_per_pair={args.games_per_pair} total_scored_games={len(labels)*(len(labels)-1)//2*args.games_per_pair} "
          f"decks={args.player0_deck.upper()}{args.player1_deck.upper()} "
          f"device={device_name(device)} mode={'stochastic' if args.stochastic else 'greedy'}", flush=True)

    existing = {} if args.fresh else load_existing_details(details_path)
    rows: list[dict[str, object] | dict[str, str]] = list(existing.values())

    # Load once on CPU; move only the active pair to GPU.
    policies: dict[str, CandidatePolicyValueNet] = {}
    for name in labels:
        policy, _meta = policy_from_checkpoint(paths[name], device="cpu")
        policy.eval()
        policies[name] = policy
        print(f"[LOAD] {name} {paths[name].name}", flush=True)

    total_pairs = len(labels) * (len(labels) - 1) // 2
    pair_no = 0
    for i in range(len(labels)):
        for j in range(i + 1, len(labels)):
            pair_no += 1
            a, b = labels[i], labels[j]
            key = (a, b)
            if key in existing and int(float(existing[key]["games"])) == args.games_per_pair:
                print(f"[SKIP] {pair_no:02d}/{total_pairs} {a} vs {b} already complete", flush=True)
                continue

            # Same fixed seed schedule for every pair gives a common-random-numbers
            # benchmark: all model pairs are tested on closely matched deal randomness.
            print(f"[MATCH] {pair_no:02d}/{total_pairs} {a} vs {b} ...", flush=True)
            pa = policies[a].to(device)
            pb = policies[b].to(device)
            result = evaluate_pair_exact(
                pa,
                pb,
                label_a=a,
                label_b=b,
                games=args.games_per_pair,
                num_envs=args.num_envs,
                seed=args.seed,
                reward_mode=args.reward_mode,
                player0_deck=args.player0_deck,
                player1_deck=args.player1_deck,
                library_path=args.library,
                device=device,
                stochastic=args.stochastic,
            )
            pa.to("cpu")
            pb.to("cpu")
            if torch.cuda.is_available():
                torch.cuda.empty_cache()

            row = result.as_row()
            existing[key] = {k: str(v) for k, v in row.items()}
            rows = list(existing.values())
            write_details(details_path, rows)
            build_outputs(out_dir, labels, rows)

            print(
                f"[RESULT] {a} vs {b} games={result.games} "
                f"W/L/D={result.a_win_rate:.3f}/{result.b_win_rate:.3f}/{result.draw_rate:.3f} "
                f"A(P0/P1)={result.side_a0.a_win_rate:.3f}/{result.side_a1.a_win_rate:.3f} "
                f"illegal={result.illegal} time={result.elapsed_s:.1f}s",
                flush=True,
            )

    rows = list(existing.values())
    build_outputs(out_dir, labels, rows)
    print(f"[DONE] {out_dir}", flush=True)
    print(f"       details: {details_path}", flush=True)
    print(f"       matrix : {out_dir / 'matrix_winrate_pct.csv'}", flush=True)
    print(f"       rank   : {out_dir / 'ranking.csv'}", flush=True)
    print(f"       pretty : {out_dir / 'matrix_pretty.txt'}", flush=True)


if __name__ == "__main__":
    main()
