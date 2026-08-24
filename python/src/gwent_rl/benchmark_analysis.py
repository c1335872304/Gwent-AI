from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping

STAGE_KEYS = [
    "collect_ms",
    "to_torch_ms",
    "forward_ms",
    "action_select_ms",
    "apply_ms",
    "loop_overhead_ms",
]
CONFIG_KEYS = [
    "num_envs",
    "hidden_dim",
    "text_embedding_mode",
    "text_embedding_dim",
    "use_candidate_object_attention",
    "use_candidate_self_attention",
    "attention_layers",
    "num_attention_heads",
    "reward_mode",
    "torch_threads",
]


def _parse_value(value: Any) -> Any:
    if value is None:
        return None
    if isinstance(value, (int, float, bool)):
        return value
    text = str(value).strip()
    if text == "":
        return ""
    low = text.lower()
    if low in {"true", "yes", "on"}:
        return True
    if low in {"false", "no", "off"}:
        return False
    if low == "null":
        return None
    try:
        if any(ch in text for ch in (".", "e", "E")):
            return float(text)
        return int(text)
    except ValueError:
        return text


def _to_float(row: Mapping[str, Any], key: str, default: float = 0.0) -> float:
    value = row.get(key, default)
    value = _parse_value(value)
    if value in (None, ""):
        return default
    try:
        out = float(value)
        if not math.isfinite(out):
            return default
        return out
    except (TypeError, ValueError):
        return default


def _to_int(row: Mapping[str, Any], key: str, default: int = 0) -> int:
    value = row.get(key, default)
    value = _parse_value(value)
    if value in (None, ""):
        return default
    try:
        return int(value)
    except (TypeError, ValueError):
        try:
            return int(float(value))
        except (TypeError, ValueError):
            return default


def _to_bool(row: Mapping[str, Any], key: str, default: bool = False) -> bool:
    value = _parse_value(row.get(key, default))
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    return default


def _normalize_row(row: Mapping[str, Any]) -> dict[str, Any]:
    out = {key: _parse_value(value) for key, value in row.items()}
    for key in [
        "num_envs",
        "hidden_dim",
        "text_embedding_dim",
        "attention_layers",
        "num_attention_heads",
        "rounds",
        "warmup_rounds",
        "decisions",
        "completed_episodes",
        "model_param_count",
        "torch_threads",
    ]:
        if key in out and out[key] not in (None, ""):
            out[key] = _to_int(out, key)
    for key in STAGE_KEYS + [
        "decisions_per_second",
        "games_per_minute",
        "rss_delta_mb",
        "cuda_peak_mb",
        "measured_s",
        "total_s",
        "dropout",
    ]:
        if key in out and out[key] not in (None, ""):
            out[key] = _to_float(out, key)
    for key in [
        "torch_forward",
        "use_candidate_object_attention",
        "use_candidate_self_attention",
    ]:
        if key in out and out[key] not in (None, ""):
            out[key] = _to_bool(out, key)
    return out


def resolve_benchmark_input(path: str | Path) -> Path:
    p = Path(path)
    if p.is_dir():
        for name in ("benchmark_matrix.csv", "benchmark_matrix.jsonl", "benchmark_summary.json"):
            candidate = p / name
            if candidate.exists():
                return candidate
        raise FileNotFoundError(f"no benchmark_matrix.csv/jsonl found in {p}")
    if not p.exists():
        raise FileNotFoundError(str(p))
    return p


def load_benchmark_rows(path: str | Path) -> list[dict[str, Any]]:
    p = resolve_benchmark_input(path)
    suffix = p.suffix.lower()
    rows: list[dict[str, Any]] = []
    if suffix == ".csv":
        with p.open("r", newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                rows.append(_normalize_row(row))
    elif suffix == ".jsonl":
        with p.open("r", encoding="utf-8") as f:
            for line in f:
                if line.strip():
                    rows.append(_normalize_row(json.loads(line)))
    elif suffix == ".json":
        data = json.loads(p.read_text(encoding="utf-8"))
        if isinstance(data, dict) and isinstance(data.get("rows"), list):
            rows = [_normalize_row(x) for x in data["rows"]]
        elif isinstance(data, dict) and isinstance(data.get("best_case"), dict):
            rows = [_normalize_row(data["best_case"])]
        elif isinstance(data, list):
            rows = [_normalize_row(x) for x in data]
        else:
            raise ValueError(f"unsupported json benchmark shape in {p}")
    else:
        raise ValueError(f"unsupported benchmark input suffix: {p.suffix}")
    return [r for r in rows if _to_float(r, "decisions_per_second") > 0.0]


def stage_breakdown(row: Mapping[str, Any]) -> dict[str, Any]:
    values = {key: max(_to_float(row, key), 0.0) for key in STAGE_KEYS}
    total = sum(values.values())
    if total <= 0.0:
        return {"dominant_stage": "unknown", "dominant_stage_pct": 0.0, "total_stage_ms": 0.0, "stages": values}
    dominant = max(values, key=values.get)
    return {
        "dominant_stage": dominant,
        "dominant_stage_pct": 100.0 * values[dominant] / total,
        "total_stage_ms": total,
        "stages": values,
        "stage_pct": {key: 100.0 * value / total for key, value in values.items()},
    }


def score_row(row: Mapping[str, Any], *, memory_weight: float = 0.05, param_weight: float = 0.03) -> dict[str, float]:
    dps = max(_to_float(row, "decisions_per_second"), 0.0)
    rss = max(_to_float(row, "rss_delta_mb"), 0.0)
    params = max(_to_float(row, "model_param_count"), 0.0)
    forward_ms = max(_to_float(row, "forward_ms"), 0.0)
    # Throughput is raw dps. Balanced score penalizes memory, parameter count, and slow model forward.
    balanced = dps / (1.0 + memory_weight * (rss / 256.0) + param_weight * (params / 1_000_000.0) + 0.03 * forward_ms)
    light = dps / (1.0 + params / 500_000.0 + rss / 512.0)
    return {
        "throughput_score": dps,
        "balanced_score": balanced,
        "light_score": light,
    }


def _sorted_by(rows: list[dict[str, Any]], key: str) -> list[dict[str, Any]]:
    return sorted(rows, key=lambda row: score_row(row).get(key, 0.0), reverse=True)


def _dedupe_by_case(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    seen: set[tuple[Any, ...]] = set()
    out: list[dict[str, Any]] = []
    for row in rows:
        key = tuple(row.get(k) for k in CONFIG_KEYS)
        if key in seen:
            continue
        seen.add(key)
        out.append(row)
    return out


def _group_average(rows: list[dict[str, Any]], key: str) -> list[dict[str, Any]]:
    buckets: dict[Any, list[dict[str, Any]]] = {}
    for row in rows:
        buckets.setdefault(row.get(key), []).append(row)
    summary: list[dict[str, Any]] = []
    for value, items in buckets.items():
        dps = [_to_float(item, "decisions_per_second") for item in items]
        summary.append(
            {
                key: value,
                "case_count": len(items),
                "decisions_per_second_mean": statistics.fmean(dps),
                "decisions_per_second_max": max(dps),
            }
        )
    return sorted(summary, key=lambda row: float(row["decisions_per_second_mean"]), reverse=True)


def _compare_modes(rows: list[dict[str, Any]], varying_key: str) -> list[dict[str, Any]]:
    # Compare mean throughput by one switch/key while keeping this simple and robust.
    return _group_average(rows, varying_key)


def recommend_rows(rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    if not rows:
        raise ValueError("no benchmark rows")
    by_throughput = _sorted_by(rows, "throughput_score")
    by_balanced = _sorted_by(rows, "balanced_score")
    by_light = _sorted_by(rows, "light_score")
    return {
        "throughput": by_throughput[0],
        "balanced": by_balanced[0],
        "light": by_light[0],
    }


def _bool_text(value: Any) -> str:
    return "true" if bool(value) else "false"


def _yaml_scalar(value: Any) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value)
    if text == "" or any(ch in text for ch in ":#{}[],&*?|-<>=!%@\\\n\t") or text.strip() != text:
        return json.dumps(text)
    return text


def _write_yaml(path: Path, data: Mapping[str, Any], indent: int = 0) -> None:
    def lines(obj: Mapping[str, Any], level: int) -> list[str]:
        out: list[str] = []
        pad = " " * level
        for key, value in obj.items():
            if isinstance(value, Mapping):
                out.append(f"{pad}{key}:")
                out.extend(lines(value, level + 2))
            else:
                out.append(f"{pad}{key}: {_yaml_scalar(value)}")
        return out

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines(data, indent)) + "\n", encoding="utf-8")


def config_from_row(row: Mapping[str, Any], *, profile: str, run_dir: str | None = None) -> dict[str, Any]:
    num_envs = _to_int(row, "num_envs", 128)
    hidden_dim = _to_int(row, "hidden_dim", 128)
    reward_mode = row.get("reward_mode") or "terminal"
    text_mode = row.get("text_embedding_mode") or "npz"
    text_dim = _to_int(row, "text_embedding_dim", 1024)
    attention_layers = _to_int(row, "attention_layers", 1)
    heads = _to_int(row, "num_attention_heads", 4)
    if hidden_dim % max(heads, 1) != 0:
        heads = 1
    steps_per_update = max(256, num_envs * 4)
    minibatch_size = min(max(128, num_envs), steps_per_update)
    return {
        "seed": 0,
        "device": row.get("device") or "auto",
        "run_dir": run_dir or f"runs/{profile}",
        "checkpoint_interval": 1,
        "collector": {
            "num_envs": num_envs,
            "max_batch_size": num_envs,
            "base_seed": 0,
            "reward_mode": reward_mode,
            "enable_invariants": False,
            "reward_overrides": {},
        },
        "model": {
            "hidden_dim": hidden_dim,
            "num_attention_heads": heads,
            "attention_layers": attention_layers,
            "dropout": _to_float(row, "dropout", 0.0),
            "card_vocab_size": _to_int(row, "card_vocab_size", 4096),
            "text_embedding_mode": text_mode,
            "text_embedding_path": row.get("text_embedding_path") or ("data/embeddings/supported_cards_text_embeddings_qwen1024.npz" if str(text_mode).lower() == "npz" else None),
            "text_embedding_dim": text_dim,
            "freeze_text_embeddings": True,
            "use_candidate_object_attention": _to_bool(row, "use_candidate_object_attention", True),
            "use_candidate_self_attention": _to_bool(row, "use_candidate_self_attention", True),
        },
        "ppo": {
            "updates": 100,
            "steps_per_update": steps_per_update,
            "learning_rate": 0.0003,
            "epochs": 2,
            "minibatch_size": minibatch_size,
            "gamma": 0.99,
            "gae_lambda": 0.95,
            "clip_ratio": 0.2,
            "value_coef": 0.5,
            "entropy_coef": 0.01,
            "max_grad_norm": 0.5,
            "target_kl": 0.03,
            "value_clip": 0.2,
        },
        "eval": {
            "interval": 5,
            "games": max(64, min(512, num_envs * 2)),
            "num_envs": min(max(32, num_envs // 2), 128),
            "opponent": "random",
            "controlled_player": 0,
            "deterministic": True,
        },
    }


def write_recommended_configs(rows: list[dict[str, Any]], output_dir: str | Path) -> dict[str, str]:
    recs = recommend_rows(rows)
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)
    paths: dict[str, str] = {}
    for profile, row in recs.items():
        path = out / f"ppo_best_{profile}.yaml"
        _write_yaml(path, config_from_row(row, profile=f"ppo_best_{profile}"))
        paths[profile] = str(path)
    return paths


def _row_label(row: Mapping[str, Any]) -> str:
    return (
        f"envs={row.get('num_envs')} hidden={row.get('hidden_dim')} "
        f"text={row.get('text_embedding_mode')} obj_attn={row.get('use_candidate_object_attention')} "
        f"self_attn={row.get('use_candidate_self_attention')}"
    )


def _markdown_table(rows: Iterable[Mapping[str, Any]], columns: list[tuple[str, str]], limit: int | None = None) -> str:
    rows_list = list(rows)
    if limit is not None:
        rows_list = rows_list[:limit]
    header = "| " + " | ".join(title for title, _ in columns) + " |"
    sep = "| " + " | ".join("---" for _ in columns) + " |"
    body = []
    for row in rows_list:
        cells = []
        for _, key in columns:
            value = row.get(key, "")
            if isinstance(value, float):
                cells.append(f"{value:.4g}")
            else:
                cells.append(str(value))
        body.append("| " + " | ".join(cells) + " |")
    return "\n".join([header, sep, *body])


def analyze_rows(rows: list[dict[str, Any]], *, top_k: int = 10) -> dict[str, Any]:
    if not rows:
        raise ValueError("no benchmark rows")
    enriched: list[dict[str, Any]] = []
    for row in rows:
        item = dict(row)
        item.update(score_row(row))
        breakdown = stage_breakdown(row)
        item["dominant_stage"] = breakdown["dominant_stage"]
        item["dominant_stage_pct"] = breakdown["dominant_stage_pct"]
        enriched.append(item)
    recs = recommend_rows(enriched)
    dps = [_to_float(r, "decisions_per_second") for r in enriched]
    return {
        "case_count": len(enriched),
        "generated_unix": time.time(),
        "throughput_mean": statistics.fmean(dps),
        "throughput_min": min(dps),
        "throughput_max": max(dps),
        "recommendations": recs,
        "top_throughput": _dedupe_by_case(_sorted_by(enriched, "throughput_score"))[:top_k],
        "top_balanced": _dedupe_by_case(_sorted_by(enriched, "balanced_score"))[:top_k],
        "top_light": _dedupe_by_case(_sorted_by(enriched, "light_score"))[:top_k],
        "by_num_envs": _group_average(enriched, "num_envs"),
        "by_hidden_dim": _group_average(enriched, "hidden_dim"),
        "by_text_embedding_mode": _compare_modes(enriched, "text_embedding_mode"),
        "by_object_attention": _compare_modes(enriched, "use_candidate_object_attention"),
        "by_self_attention": _compare_modes(enriched, "use_candidate_self_attention"),
    }


def _recommendation_notes(analysis: Mapping[str, Any]) -> list[str]:
    notes: list[str] = []
    recs = analysis["recommendations"]
    throughput = recs["throughput"]
    breakdown = stage_breakdown(throughput)
    dominant = breakdown["dominant_stage"]
    pct = float(breakdown["dominant_stage_pct"])
    notes.append(f"Best raw throughput is `{_row_label(throughput)}` at {_to_float(throughput, 'decisions_per_second'):.0f} decisions/sec.")
    if dominant == "forward_ms" and pct >= 40.0:
        notes.append("Model forward is the dominant stage; try smaller hidden_dim, disabling candidate self-attention, or moving forward pass to GPU.")
    elif dominant in {"collect_ms", "apply_ms"} and pct >= 40.0:
        notes.append("Environment collect/apply is the dominant stage; try larger batch sizes first, then profile C collector hot paths.")
    elif dominant == "to_torch_ms" and pct >= 25.0:
        notes.append("Tensor conversion is material; reduce host copies or keep recurrent tensors on the target device.")
    else:
        notes.append(f"No single severe bottleneck dominates; largest stage is `{dominant}` at {pct:.1f}% of measured loop time.")
    by_envs = analysis.get("by_num_envs", [])
    if by_envs:
        best_env = by_envs[0]
        notes.append(f"Best average env count in this matrix is `{best_env.get('num_envs')}` envs.")
    by_text = analysis.get("by_text_embedding_mode", [])
    if len(by_text) >= 2:
        best_text = by_text[0]
        notes.append(f"Best average text mode in this matrix is `{best_text.get('text_embedding_mode')}`.")
    return notes


def write_markdown_report(analysis: Mapping[str, Any], path: str | Path, *, config_paths: Mapping[str, str] | None = None) -> Path:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    notes = _recommendation_notes(analysis)
    lines: list[str] = []
    lines.append("# m61 Benchmark Analysis Report")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- Cases analyzed: **{analysis['case_count']}**")
    lines.append(f"- Mean decisions/sec: **{analysis['throughput_mean']:.0f}**")
    lines.append(f"- Best decisions/sec: **{analysis['throughput_max']:.0f}**")
    lines.append(f"- Worst decisions/sec: **{analysis['throughput_min']:.0f}**")
    lines.append("")
    lines.append("## Recommendations")
    lines.append("")
    for note in notes:
        lines.append(f"- {note}")
    if config_paths:
        lines.append("")
        lines.append("Generated config files:")
        for key, value in config_paths.items():
            lines.append(f"- `{key}`: `{value}`")
    lines.append("")
    cols = [
        ("case", "case_id"),
        ("envs", "num_envs"),
        ("hidden", "hidden_dim"),
        ("text", "text_embedding_mode"),
        ("obj_attn", "use_candidate_object_attention"),
        ("self_attn", "use_candidate_self_attention"),
        ("dps", "decisions_per_second"),
        ("forward_ms", "forward_ms"),
        ("apply_ms", "apply_ms"),
        ("dominant", "dominant_stage"),
    ]
    lines.append("## Top Throughput Cases")
    lines.append("")
    lines.append(_markdown_table(analysis["top_throughput"], cols, limit=10))
    lines.append("")
    lines.append("## Top Balanced Cases")
    lines.append("")
    lines.append(_markdown_table(analysis["top_balanced"], cols + [("balanced", "balanced_score")], limit=10))
    lines.append("")
    lines.append("## Average Throughput by Env Count")
    lines.append("")
    lines.append(_markdown_table(analysis["by_num_envs"], [("envs", "num_envs"), ("cases", "case_count"), ("mean dps", "decisions_per_second_mean"), ("max dps", "decisions_per_second_max")]))
    lines.append("")
    lines.append("## Average Throughput by Hidden Dim")
    lines.append("")
    lines.append(_markdown_table(analysis["by_hidden_dim"], [("hidden", "hidden_dim"), ("cases", "case_count"), ("mean dps", "decisions_per_second_mean"), ("max dps", "decisions_per_second_max")]))
    lines.append("")
    lines.append("## Average Throughput by Text Mode")
    lines.append("")
    lines.append(_markdown_table(analysis["by_text_embedding_mode"], [("text", "text_embedding_mode"), ("cases", "case_count"), ("mean dps", "decisions_per_second_mean"), ("max dps", "decisions_per_second_max")]))
    lines.append("")
    lines.append("## Suggested Next Commands")
    lines.append("")
    lines.append("```bash")
    lines.append("python -m gwent_rl.train_ppo --config configs/training/ppo_best_balanced.yaml --library build/libgwent_core.so")
    lines.append("python -m gwent_rl.benchmark_suite --envs 128,256,512 --hidden-dims 64,128 --text-modes none,hash --output-dir runs/benchmarks/refine")
    lines.append("python -m gwent_rl.benchmark_analysis --input runs/benchmarks/refine --output-dir runs/benchmarks/refine_analysis --write-configs")
    lines.append("```")
    lines.append("")
    p.write_text("\n".join(lines), encoding="utf-8")
    return p


def run_analysis(
    input_path: str | Path,
    *,
    output_dir: str | Path,
    top_k: int = 10,
    write_configs: bool = True,
    project_config_dir: str | Path | None = None,
) -> dict[str, Any]:
    rows = load_benchmark_rows(input_path)
    analysis = analyze_rows(rows, top_k=top_k)
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)
    report_config_paths: dict[str, str] = {}
    if write_configs:
        # Always write local configs next to the analysis report so benchmark
        # artifacts are self-contained. CLI --write-configs also passes a
        # project training-config directory to update configs/training/ppo_best_*.yaml.
        local_config_paths = write_recommended_configs(rows, out / "configs")
        generated: dict[str, Any] = {"local": local_config_paths}
        report_config_paths = local_config_paths
        if project_config_dir is not None:
            project_paths = write_recommended_configs(rows, project_config_dir)
            generated["project"] = project_paths
            report_config_paths = project_paths
        analysis["generated_configs"] = generated
    (out / "benchmark_analysis.json").write_text(json.dumps(analysis, indent=2, sort_keys=True, default=str), encoding="utf-8")
    write_markdown_report(analysis, out / "benchmark_report.md", config_paths=report_config_paths)
    return analysis


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Analyze m60 benchmark matrix and generate recommended PPO configs")
    parser.add_argument("--input", required=True, help="benchmark_matrix.csv/jsonl or directory containing benchmark_matrix.csv")
    parser.add_argument("--output-dir", default="runs/benchmarks/m61_analysis")
    parser.add_argument("--top-k", type=int, default=10)
    parser.add_argument("--write-configs", action="store_true", help="Write configs/training/ppo_best_*.yaml and local copies")
    args = parser.parse_args(argv)
    analysis = run_analysis(args.input, output_dir=args.output_dir, top_k=args.top_k, write_configs=args.write_configs, project_config_dir=Path("configs/training") if args.write_configs else None)
    summary = {
        "case_count": analysis["case_count"],
        "throughput_best": analysis["throughput_max"],
        "recommended_configs": analysis.get("generated_configs", {}),
        "report": str(Path(args.output_dir) / "benchmark_report.md"),
    }
    print(json.dumps(summary, indent=2, sort_keys=True, default=str))


if __name__ == "__main__":
    main()
