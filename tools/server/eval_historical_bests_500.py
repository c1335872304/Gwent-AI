from __future__ import annotations

import csv
import json
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


# ============================================================
# 只改这里
# ============================================================

RUN_NAME = "rulefix_v7_250k"

GAMES_PER_PAIR = 500
NUM_ENVS = 128
COLLECTOR_THREADS = 12
SEED = 20260818

REWARD_MODE = "terminal"
DEVICE = "auto"
STOCHASTIC = False

LIBRARY = "build-release/libgwent_core.so"


# ============================================================


@dataclass(frozen=True)
class HistoricalBest:
    label: str
    update: int
    checkpoint: Path
    promoted_from: int | None = None


def find_project_root() -> Path:
    here = Path(__file__).resolve()

    for root in [Path.cwd(), here.parent, *here.parents]:
        if (
            (root / "CMakeLists.txt").is_file()
            and (root / "python/src/gwent_rl").is_dir()
            and (root / "tools/server/eval_best_matrix.py").is_file()
        ):
            return root.resolve()

    raise RuntimeError("Cannot find Gwent project root")


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows = []

    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))

    return rows


def as_int(value: Any, default: int = 0) -> int:
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def discover_bests(run_dir: Path) -> list[HistoricalBest]:
    ckpt_dir = run_dir / "checkpoints"
    eval_path = run_dir / "eval.jsonl"

    initial = ckpt_dir / "initial.pt"

    if not initial.is_file():
        raise FileNotFoundError(initial)

    if not eval_path.is_file():
        raise FileNotFoundError(eval_path)

    history = [
        HistoricalBest(
            label="u000_init",
            update=0,
            checkpoint=initial,
        )
    ]

    seen = {0}

    for row in read_jsonl(eval_path):
        if as_int(row.get("promoted")) != 1:
            continue

        update = as_int(row.get("update"), -1)

        if update < 0 or update in seen:
            continue

        checkpoint = ckpt_dir / f"update_{update:06d}.pt"

        if not checkpoint.is_file():
            raise FileNotFoundError(
                f"Promoted checkpoint missing: {checkpoint}"
            )

        history.append(
            HistoricalBest(
                label=f"u{update:03d}",
                update=update,
                checkpoint=checkpoint,
                promoted_from=as_int(
                    row.get("best_update_before"),
                    -1,
                ),
            )
        )

        seen.add(update)

    return history


def write_history(path: Path, history: list[HistoricalBest]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)

        writer.writerow([
            "label",
            "update",
            "promoted_from",
            "checkpoint",
        ])

        for x in history:
            writer.writerow([
                x.label,
                x.update,
                x.promoted_from,
                x.checkpoint,
            ])


def main() -> int:
    os.environ["GWENT_COLLECTOR_THREADS"] = str(COLLECTOR_THREADS)

    root = find_project_root()
    os.chdir(root)

    python_src = str(root / "python/src")

    if python_src not in sys.path:
        sys.path.insert(0, python_src)

    import torch

    from eval_best_matrix import (
        build_outputs,
        evaluate_pair_exact,
        load_existing_details,
        write_details,
    )
    from gwent_rl.device import device_name, resolve_torch_device
    from gwent_rl.experiment import policy_from_checkpoint

    run_dir = root / "runs/tasks" / RUN_NAME
    library = root / LIBRARY

    if not run_dir.is_dir():
        raise SystemExit(f"Run not found: {run_dir}")

    if not library.is_file():
        raise SystemExit(f"Core library not found: {library}")

    history = discover_bests(run_dir)

    print(f"[RUN] {RUN_NAME}")
    print("[HISTORICAL BESTS]")

    for i, x in enumerate(history, 1):
        suffix = (
            ""
            if x.promoted_from is None
            else f" <- best u{x.promoted_from}"
        )

        print(
            f"  {i:02d}. {x.label:<10} "
            f"update={x.update:<4} "
            f"{x.checkpoint.name}{suffix}"
        )

    if len(history) < 2:
        print("[DONE] Nothing to compare")
        return 0

    pair_count = len(history) * (len(history) - 1) // 2

    print(
        f"[PLAN] models={len(history)} "
        f"pairs={pair_count} "
        f"games_per_pair={GAMES_PER_PAIR} "
        f"total={pair_count * GAMES_PER_PAIR}"
    )

    out_dir = run_dir / f"historical_best_matrix_{GAMES_PER_PAIR}"
    out_dir.mkdir(parents=True, exist_ok=True)

    write_history(
        out_dir / "best_history.csv",
        history,
    )

    details_path = out_dir / "details.csv"
    existing = load_existing_details(details_path)

    device = resolve_torch_device(DEVICE)

    print(
        f"[START] device={device_name(device)} "
        f"envs={NUM_ENVS} "
        f"threads={COLLECTOR_THREADS} "
        f"mode={'stochastic' if STOCHASTIC else 'greedy'}"
    )

    policies: dict[str, Any] = {}

    for x in history:
        print(f"[LOAD] {x.label} {x.checkpoint.name}")

        policy, meta = policy_from_checkpoint(
            x.checkpoint,
            device="cpu",
        )

        policy.eval()
        policies[x.label] = policy

        print(
            f"[OK] {x.label} "
            f"meta_update={meta.get('update', '?')}"
        )

    labels = [x.label for x in history]
    pair_no = 0

    for i in range(len(history)):
        for j in range(i + 1, len(history)):
            pair_no += 1

            a = history[i]
            b = history[j]
            key = (a.label, b.label)

            old = existing.get(key)

            if (
                old is not None
                and as_int(old.get("games")) == GAMES_PER_PAIR
            ):
                print(
                    f"[SKIP] {pair_no:03d}/{pair_count} "
                    f"{a.label} vs {b.label}"
                )
                continue

            print(
                f"[MATCH] {pair_no:03d}/{pair_count} "
                f"{a.label} vs {b.label}"
            )

            t0 = time.perf_counter()

            policy_a = policies[a.label].to(device)
            policy_b = policies[b.label].to(device)

            result = evaluate_pair_exact(
                policy_a,
                policy_b,
                label_a=a.label,
                label_b=b.label,
                games=GAMES_PER_PAIR,
                num_envs=NUM_ENVS,
                seed=SEED,
                reward_mode=REWARD_MODE,
                library_path=str(library),
                device=device,
                stochastic=STOCHASTIC,
            )

            policy_a.to("cpu")
            policy_b.to("cpu")

            if torch.cuda.is_available():
                torch.cuda.empty_cache()

            existing[key] = {
                k: str(v)
                for k, v in result.as_row().items()
            }

            rows = list(existing.values())

            write_details(details_path, rows)
            build_outputs(out_dir, labels, rows)

            print(
                f"[RESULT] "
                f"{a.label} vs {b.label} "
                f"W/L/D="
                f"{result.a_wins}/"
                f"{result.b_wins}/"
                f"{result.draws} "
                f"A_win={result.a_win_rate * 100:.2f}% "
                f"score={result.a_score_rate * 100:.2f}% "
                f"illegal={result.illegal} "
                f"time={time.perf_counter() - t0:.1f}s"
            )

    build_outputs(
        out_dir,
        labels,
        list(existing.values()),
    )

    print("\n[DONE]")
    print(f"ranking : {out_dir / 'ranking.csv'}")
    print(f"matrix  : {out_dir / 'matrix_winrate_pct.csv'}")
    print(f"pretty  : {out_dir / 'matrix_pretty.txt'}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
