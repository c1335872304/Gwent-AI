from __future__ import annotations

import argparse
import gc
import json
import os
import resource
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np
import torch

from .collector import RlCollector, random_legal_actions
from .device import resolve_torch_device
from .policy import CandidatePolicyValueNet, greedy_actions
from .text_embeddings import resolve_text_embedding_table


@dataclass(frozen=True)
class BenchmarkModelConfig:
    hidden_dim: int = 64
    num_attention_heads: int = 4
    attention_layers: int = 1
    dropout: float = 0.0
    card_vocab_size: int = 4096
    text_embedding_mode: str = "npz"
    text_embedding_path: str | None = "data/embeddings/supported_cards_text_embeddings_qwen1024.npz"
    text_embedding_dim: int = 1024
    freeze_text_embeddings: bool = True
    use_candidate_object_attention: bool = True
    use_candidate_self_attention: bool = True
    device: str = "auto"


@dataclass(frozen=True)
class BenchmarkRunConfig:
    num_envs: int = 128
    rounds: int = 256
    warmup_rounds: int = 8
    seed: int = 0
    reward_mode: str = "terminal"
    torch_forward: bool = False
    torch_threads: int | None = None
    library_path: str | None = None
    model: BenchmarkModelConfig = BenchmarkModelConfig()


def _rss_mb() -> float:
    # ru_maxrss is KiB on Linux, bytes on macOS. This environment is Linux, but
    # the branch keeps the helper portable for developer laptops.
    rss = float(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
    if os.uname().sysname == "Darwin":
        return rss / (1024.0 * 1024.0)
    return rss / 1024.0


def _model_param_count(policy: CandidatePolicyValueNet | None) -> int:
    if policy is None:
        return 0
    return int(sum(p.numel() for p in policy.parameters()))


def _build_policy(config: BenchmarkModelConfig) -> CandidatePolicyValueNet:
    text_table = resolve_text_embedding_table(
        config.text_embedding_mode,
        config.text_embedding_path,
        config.text_embedding_dim,
    )
    text_dim = int(text_table.dim) if text_table is not None else 0
    policy = CandidatePolicyValueNet(
        hidden_dim=config.hidden_dim,
        num_attention_heads=config.num_attention_heads,
        attention_layers=config.attention_layers,
        dropout=config.dropout,
        card_vocab_size=config.card_vocab_size,
        text_embedding_dim=text_dim,
        freeze_text_embeddings=config.freeze_text_embeddings,
        use_candidate_object_attention=config.use_candidate_object_attention,
        use_candidate_self_attention=config.use_candidate_self_attention,
    )
    if text_table is not None:
        policy.set_text_embedding_table(text_table, freeze=config.freeze_text_embeddings)
    return policy.to(resolve_torch_device(config.device)).eval()


def _sync_if_cuda(device: str) -> None:
    if str(device).startswith("cuda") and torch.cuda.is_available():
        torch.cuda.synchronize()


def run_benchmark(
    num_envs: int = 128,
    rounds: int = 256,
    seed: int = 0,
    reward_mode: str = "terminal",
    torch_forward: bool = False,
    *,
    warmup_rounds: int = 0,
    hidden_dim: int = 64,
    num_attention_heads: int = 4,
    attention_layers: int = 1,
    dropout: float = 0.0,
    text_embedding_mode: str = "none",
    text_embedding_path: str | None = None,
    text_embedding_dim: int = 128,
    freeze_text_embeddings: bool = True,
    use_candidate_object_attention: bool = True,
    use_candidate_self_attention: bool = True,
    device: str = "auto",
    torch_threads: int | None = None,
    library_path: str | None = None,
) -> dict[str, float | int | str | bool]:
    """Benchmark collector + optional model forward/apply loop.

    This remains backward compatible with the older m56/m59 `run_benchmark`
    signature while exposing the knobs needed for m60 model ablations.
    """
    if torch_threads is not None and int(torch_threads) > 0:
        torch.set_num_threads(int(torch_threads))
    rng = np.random.default_rng(seed)
    device_obj = resolve_torch_device(device)
    device = str(device_obj)
    collect_s = 0.0
    to_torch_s = 0.0
    forward_s = 0.0
    action_select_s = 0.0
    apply_s = 0.0
    decisions = 0
    warmup_decisions = 0
    policy: CandidatePolicyValueNet | None = None
    if torch_forward:
        policy = _build_policy(
            BenchmarkModelConfig(
                hidden_dim=hidden_dim,
                num_attention_heads=num_attention_heads,
                attention_layers=attention_layers,
                dropout=dropout,
                text_embedding_mode=text_embedding_mode,
                text_embedding_path=text_embedding_path,
                text_embedding_dim=text_embedding_dim,
                freeze_text_embeddings=freeze_text_embeddings,
                use_candidate_object_attention=use_candidate_object_attention,
                use_candidate_self_attention=use_candidate_self_attention,
                device=device,
            )
        )
    param_count = _model_param_count(policy)
    rss_before = _rss_mb()
    cuda_before = torch.cuda.max_memory_allocated(device_obj) if device_obj.type == "cuda" and torch.cuda.is_available() else 0
    if device_obj.type == "cuda" and torch.cuda.is_available():
        torch.cuda.reset_peak_memory_stats(device_obj)

    with RlCollector(num_envs=num_envs, max_batch_size=num_envs, base_seed=seed, reward_mode=reward_mode, library_path=library_path) as collector:
        total_rounds = int(warmup_rounds) + int(rounds)
        t0 = time.perf_counter()
        for step in range(total_rounds):
            measuring = step >= int(warmup_rounds)
            a = time.perf_counter()
            batch = collector.collect()
            b = time.perf_counter()
            if policy is not None:
                tensors = batch.to_torch(device=device_obj)
                c = time.perf_counter()
                _sync_if_cuda(device)
                with torch.no_grad():
                    out = policy(tensors)
                    _sync_if_cuda(device)
                    d0 = time.perf_counter()
                    actions = greedy_actions(out.logits, tensors["option_mask"]).detach().cpu().numpy()
                    d = time.perf_counter()
            else:
                c = b
                d0 = c
                actions = random_legal_actions(batch, rng)
                d = d0
            collector.apply_actions(batch, actions)
            e = time.perf_counter()
            if measuring:
                collect_s += b - a
                to_torch_s += c - b
                forward_s += d0 - c
                action_select_s += d - d0
                apply_s += e - d
                decisions += batch.count
            else:
                warmup_decisions += batch.count
        total_s = time.perf_counter() - t0
        measured_s = collect_s + to_torch_s + forward_s + action_select_s + apply_s
        completed = collector.completed_episodes
    rss_after = _rss_mb()
    cuda_peak = torch.cuda.max_memory_allocated(device_obj) if device_obj.type == "cuda" and torch.cuda.is_available() else 0
    if policy is not None:
        del policy
    gc.collect()

    measured_rounds = max(int(rounds), 1)
    return {
        "num_envs": int(num_envs),
        "rounds": int(rounds),
        "warmup_rounds": int(warmup_rounds),
        "seed": int(seed),
        "reward_mode": reward_mode,
        "torch_forward": bool(torch_forward),
        "device": str(device),
        "torch_threads": int(torch.get_num_threads()),
        "hidden_dim": int(hidden_dim),
        "num_attention_heads": int(num_attention_heads),
        "attention_layers": int(attention_layers),
        "dropout": float(dropout),
        "text_embedding_mode": str(text_embedding_mode),
        "text_embedding_path": str(text_embedding_path or ""),
        "text_embedding_dim": int(text_embedding_dim),
        "use_candidate_object_attention": bool(use_candidate_object_attention),
        "use_candidate_self_attention": bool(use_candidate_self_attention),
        "model_param_count": int(param_count),
        "decisions": int(decisions),
        "warmup_decisions": int(warmup_decisions),
        "completed_episodes": int(completed),
        "total_s": float(total_s),
        "measured_s": float(measured_s),
        "collect_ms": 1000.0 * collect_s / measured_rounds,
        "to_torch_ms": 1000.0 * to_torch_s / measured_rounds,
        "forward_ms": 1000.0 * forward_s / measured_rounds,
        "action_select_ms": 1000.0 * action_select_s / measured_rounds,
        "apply_ms": 1000.0 * apply_s / measured_rounds,
        "loop_overhead_ms": 1000.0 * max(total_s - measured_s, 0.0) / max(total_rounds, 1),
        "decisions_per_second": decisions / max(measured_s, 1e-9),
        "games_per_minute": 60.0 * completed / max(total_s, 1e-9),
        "rss_before_mb": float(rss_before),
        "rss_after_mb": float(rss_after),
        "rss_delta_mb": float(max(rss_after - rss_before, 0.0)),
        "cuda_peak_mb": float(max(cuda_peak - cuda_before, 0) / (1024.0 * 1024.0)),
    }


def _write_stats(path: str | Path | None, stats: dict[str, Any]) -> None:
    if not path:
        return
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.suffix.lower() == ".json":
        p.write_text(json.dumps(stats, indent=2, sort_keys=True), encoding="utf-8")
    else:
        import csv

        with p.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(stats.keys()))
            writer.writeheader()
            writer.writerow(stats)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Benchmark C RL collector and optional model throughput")
    parser.add_argument("--num-envs", type=int, default=128)
    parser.add_argument("--rounds", type=int, default=256)
    parser.add_argument("--warmup-rounds", type=int, default=8)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--reward-mode", default="terminal")
    parser.add_argument("--torch-forward", action="store_true")
    parser.add_argument("--device", default="auto")
    parser.add_argument("--torch-threads", type=int, default=None)
    parser.add_argument("--hidden-dim", type=int, default=64)
    parser.add_argument("--num-attention-heads", type=int, default=4)
    parser.add_argument("--attention-layers", type=int, default=1)
    parser.add_argument("--dropout", type=float, default=0.0)
    parser.add_argument("--text-embedding-mode", choices=["none", "hash", "npz"], default="npz")
    parser.add_argument("--text-embedding-path", default=None)
    parser.add_argument("--text-embedding-dim", type=int, default=1024)
    parser.add_argument("--disable-candidate-object-attention", action="store_true")
    parser.add_argument("--disable-candidate-self-attention", action="store_true")
    parser.add_argument("--library", default=None)
    parser.add_argument("--output", default=None, help="Optional .json or .csv output path")
    args = parser.parse_args(argv)
    stats = run_benchmark(
        num_envs=args.num_envs,
        rounds=args.rounds,
        seed=args.seed,
        reward_mode=args.reward_mode,
        torch_forward=args.torch_forward,
        warmup_rounds=args.warmup_rounds,
        hidden_dim=args.hidden_dim,
        num_attention_heads=args.num_attention_heads,
        attention_layers=args.attention_layers,
        dropout=args.dropout,
        text_embedding_mode=args.text_embedding_mode,
        text_embedding_path=args.text_embedding_path,
        text_embedding_dim=args.text_embedding_dim,
        use_candidate_object_attention=not args.disable_candidate_object_attention,
        use_candidate_self_attention=not args.disable_candidate_self_attention,
        device=args.device,
        torch_threads=args.torch_threads,
        library_path=args.library,
    )
    _write_stats(args.output, stats)
    for key, value in stats.items():
        if isinstance(value, float):
            print(f"{key}: {value:.6g}")
        else:
            print(f"{key}: {value}")


if __name__ == "__main__":
    main()
