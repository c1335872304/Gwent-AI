from __future__ import annotations

import argparse
import csv
import itertools
import json
import statistics
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from .benchmark_collector import run_benchmark


def _parse_ints(text: str) -> list[int]:
    return [int(x.strip()) for x in str(text).split(",") if x.strip()]


def _parse_strings(text: str) -> list[str]:
    return [x.strip() for x in str(text).split(",") if x.strip()]


def _parse_bools(text: str) -> list[bool]:
    values: list[bool] = []
    for item in str(text).split(","):
        key = item.strip().lower()
        if not key:
            continue
        if key in {"1", "true", "yes", "on"}:
            values.append(True)
        elif key in {"0", "false", "no", "off"}:
            values.append(False)
        else:
            raise ValueError(f"invalid bool value: {item!r}")
    return values


@dataclass(frozen=True)
class SuiteCase:
    case_id: str
    num_envs: int
    hidden_dim: int
    text_embedding_mode: str
    use_candidate_object_attention: bool
    use_candidate_self_attention: bool
    attention_layers: int
    num_attention_heads: int

    def tags(self) -> dict[str, Any]:
        return {
            "case_id": self.case_id,
            "num_envs": self.num_envs,
            "hidden_dim": self.hidden_dim,
            "text_embedding_mode": self.text_embedding_mode,
            "use_candidate_object_attention": self.use_candidate_object_attention,
            "use_candidate_self_attention": self.use_candidate_self_attention,
            "attention_layers": self.attention_layers,
            "num_attention_heads": self.num_attention_heads,
        }


def build_cases(
    *,
    envs: Iterable[int],
    hidden_dims: Iterable[int],
    text_modes: Iterable[str],
    object_attention: Iterable[bool],
    self_attention: Iterable[bool],
    attention_layers: Iterable[int],
    num_attention_heads: Iterable[int],
) -> list[SuiteCase]:
    cases: list[SuiteCase] = []
    for idx, combo in enumerate(
        itertools.product(envs, hidden_dims, text_modes, object_attention, self_attention, attention_layers, num_attention_heads),
        start=1,
    ):
        env, hidden, text, obj_attn, self_attn, layers, heads = combo
        cases.append(
            SuiteCase(
                case_id=f"case_{idx:04d}",
                num_envs=int(env),
                hidden_dim=int(hidden),
                text_embedding_mode=str(text),
                use_candidate_object_attention=bool(obj_attn),
                use_candidate_self_attention=bool(self_attn),
                attention_layers=int(layers),
                num_attention_heads=int(heads),
            )
        )
    return cases


def _write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, sort_keys=True) + "\n")


def _write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames: list[str] = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    if not rows:
        return {"case_count": 0}
    ranked = sorted(rows, key=lambda r: float(r.get("decisions_per_second", 0.0)), reverse=True)
    dps = [float(r.get("decisions_per_second", 0.0)) for r in rows]
    fwd = [float(r.get("forward_ms", 0.0)) for r in rows]
    return {
        "case_count": len(rows),
        "best_case": ranked[0],
        "worst_case": ranked[-1],
        "decisions_per_second_mean": statistics.fmean(dps),
        "decisions_per_second_min": min(dps),
        "decisions_per_second_max": max(dps),
        "forward_ms_mean": statistics.fmean(fwd),
        "generated_unix": time.time(),
    }


def run_suite(
    cases: list[SuiteCase],
    *,
    rounds: int,
    warmup_rounds: int,
    seed: int,
    reward_mode: str,
    text_embedding_dim: int,
    text_embedding_path: str | None = None,
    device: str = "auto",
    torch_threads: int | None = None,
    library_path: str | None = None,
    output_dir: str | Path = "runs/benchmarks/m60",
) -> dict[str, Any]:
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    for i, case in enumerate(cases, start=1):
        print(f"[{i}/{len(cases)}] {case.case_id} envs={case.num_envs} hidden={case.hidden_dim} text={case.text_embedding_mode} obj_attn={case.use_candidate_object_attention} self_attn={case.use_candidate_self_attention}", flush=True)
        stats = run_benchmark(
            num_envs=case.num_envs,
            rounds=rounds,
            warmup_rounds=warmup_rounds,
            seed=seed + i - 1,
            reward_mode=reward_mode,
            torch_forward=True,
            hidden_dim=case.hidden_dim,
            num_attention_heads=case.num_attention_heads,
            attention_layers=case.attention_layers,
            text_embedding_mode=case.text_embedding_mode,
            text_embedding_dim=text_embedding_dim,
            text_embedding_path=text_embedding_path,
            use_candidate_object_attention=case.use_candidate_object_attention,
            use_candidate_self_attention=case.use_candidate_self_attention,
            device=device,
            torch_threads=torch_threads,
            library_path=library_path,
        )
        rows.append({**case.tags(), **stats})
        _write_jsonl(out / "benchmark_matrix.jsonl", rows)
        _write_csv(out / "benchmark_matrix.csv", rows)
    summary = summarize(rows)
    (out / "benchmark_summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    return summary


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Run m60 large-scale model/environment ablation benchmarks")
    parser.add_argument("--envs", default="128,256,512,1024", help="Comma-separated env counts")
    parser.add_argument("--hidden-dims", default="64,128,256")
    parser.add_argument("--text-modes", default="none,npz")
    parser.add_argument("--object-attention", default="true,false")
    parser.add_argument("--self-attention", default="true,false")
    parser.add_argument("--attention-layers", default="1")
    parser.add_argument("--attention-heads", default="4")
    parser.add_argument("--rounds", type=int, default=128)
    parser.add_argument("--warmup-rounds", type=int, default=8)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--reward-mode", default="terminal")
    parser.add_argument("--text-embedding-dim", type=int, default=1024)
    parser.add_argument("--text-embedding-path", default="data/embeddings/supported_cards_text_embeddings_qwen1024.npz")
    parser.add_argument("--device", default="auto")
    parser.add_argument("--torch-threads", type=int, default=None)
    parser.add_argument("--library", default=None)
    parser.add_argument("--output-dir", default="runs/benchmarks/m60")
    parser.add_argument("--max-cases", type=int, default=None, help="Optional cap for smoke runs")
    args = parser.parse_args(argv)
    cases = build_cases(
        envs=_parse_ints(args.envs),
        hidden_dims=_parse_ints(args.hidden_dims),
        text_modes=_parse_strings(args.text_modes),
        object_attention=_parse_bools(args.object_attention),
        self_attention=_parse_bools(args.self_attention),
        attention_layers=_parse_ints(args.attention_layers),
        num_attention_heads=_parse_ints(args.attention_heads),
    )
    if args.max_cases is not None:
        cases = cases[: max(int(args.max_cases), 0)]
    summary = run_suite(
        cases,
        rounds=args.rounds,
        warmup_rounds=args.warmup_rounds,
        seed=args.seed,
        reward_mode=args.reward_mode,
        text_embedding_dim=args.text_embedding_dim,
        text_embedding_path=args.text_embedding_path,
        device=args.device,
        torch_threads=args.torch_threads,
        library_path=args.library,
        output_dir=args.output_dir,
    )
    print(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
