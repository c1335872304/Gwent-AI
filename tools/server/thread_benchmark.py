import csv
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

CONFIG = ROOT / "configs/training/ppo_128env_strategic.yaml"
LIBRARY = ROOT / "build-release/libgwent_core.so"
CHECKPOINT = ROOT / "artifacts/checkpoints/pre_mulligan_v2/update_000088.pt"

THREAD_COUNTS = [8, 12, 16, 24]

GAMES = 2500


def read_result(run_dir):
    metrics = run_dir / "metrics.csv"

    if not metrics.exists():
        return None

    with metrics.open("r", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    if not rows:
        return None

    row = rows[-1]

    return {
        "collect_s": float(row["collect_s"]),
        "dps": float(row["decisions_per_second"]),
        "decisions": int(row["rollout_decisions"]),
    }


def main():
    print("=" * 70)
    print("Gwent Collector Thread Benchmark")
    print("=" * 70)

    print("Python :", sys.executable)
    print("Project:", ROOT)
    print("CPU available:", len(os.sched_getaffinity(0)))
    print()

    results = []

    for threads in THREAD_COUNTS:
        run_dir = ROOT / "runs" / "benchmarks" / f"threads_{threads}"

        # 每次重新开始，保证公平
        if run_dir.exists():
            shutil.rmtree(run_dir)

        env = os.environ.copy()

        env["PYTHONPATH"] = (
            str(ROOT / "python/src")
            + ":"
            + env.get("PYTHONPATH", "")
        )

        env["GWENT_CORE_LIBRARY"] = str(LIBRARY)
        env["GWENT_COLLECTOR_THREADS"] = str(threads)

        command = [
            sys.executable,
            "-m",
            "gwent_rl.train_ppo",

            "--config",
            str(CONFIG),

            "--total-games",
            str(GAMES),

            "--games-per-update",
            str(GAMES),

            "--num-envs",
            "512",

            "--max-batch-size",
            "512",

            "--minibatch-size",
            "2048",

            "--device",
            "auto",

            "--checkpoint-interval",
            "1000",

            "--eval-interval",
            "1000",

            "--run-dir",
            str(run_dir),

            "--library",
            str(LIBRARY),

            "--initialize-from",
            str(CHECKPOINT),

            "--initialize-source-schema",
            "6",

            "--initialize-source-action-grammar",
            "2",

            "--initialize-checkpoint-sha256",
            "921dacf91fdbb11f924b6f2bda931b5ad46712d04706eb95c404cfc8175b35e1",

            "--reset-decision-embedding",
            "mulligan",

            "--reset-option-embedding",
            "mulligan",

            "--reset-option-embedding",
            "keep_hand",
        ]

        print()
        print("=" * 70)
        print(f"Testing {threads} collector threads")
        print("=" * 70)

        result = subprocess.run(
            command,
            cwd=ROOT,
            env=env,
        )

        if result.returncode != 0:
            print(f"[ERROR] {threads} threads 测试失败")
            continue

        metrics = read_result(run_dir)

        if metrics:
            results.append((threads, metrics))

            print()
            print(
                f"RESULT: threads={threads} | "
                f"DPS={metrics['dps']:.1f} | "
                f"collect={metrics['collect_s']:.2f}s"
            )

    print()
    print("=" * 70)
    print("Benchmark Results")
    print("=" * 70)

    results.sort(
        key=lambda item: item[1]["dps"],
        reverse=True,
    )

    for threads, result in results:
        print(
            f"{threads:2d} threads | "
            f"{result['dps']:8.1f} DPS | "
            f"{result['collect_s']:6.2f} s"
        )

    if results:
        best_threads, best = results[0]

        print()
        print("=" * 70)
        print(f"BEST: {best_threads} threads")
        print(f"DPS : {best['dps']:.1f}")
        print("=" * 70)


if __name__ == "__main__":
    main()
