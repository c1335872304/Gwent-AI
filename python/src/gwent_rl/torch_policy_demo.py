from __future__ import annotations

import argparse

import numpy as np

from .collector import RlCollector
from .device import device_name, resolve_torch_device


def masked_argmax(logits, mask):
    import torch

    masked = logits.masked_fill(mask == 0, torch.finfo(logits.dtype).min)
    return torch.argmax(masked, dim=-1)


def run(args: argparse.Namespace) -> None:
    import torch

    torch.manual_seed(args.seed)
    device = resolve_torch_device(args.device)
    rng = np.random.default_rng(args.seed)
    with RlCollector(
        num_envs=args.num_envs,
        max_batch_size=args.max_batch_size or args.num_envs,
        base_seed=args.seed,
        enable_invariants=args.enable_invariants,
        library_path=args.library,
    ) as collector:
        for _ in range(args.rounds):
            batch = collector.collect()
            tensors = batch.to_torch(device=device)
            # Minimal shape proof: a real policy would combine global/object/option features.
            option_features = tensors["option_features"].float()
            option_mask = tensors["option_mask"].to(torch.bool)
            logits = option_features[..., 0] + 0.01 * torch.randn(option_features.shape[:2], device=device)
            actions = masked_argmax(logits, option_mask).cpu().numpy().astype(np.uint64)
            collector.apply_actions(batch, actions)
        print(
            "gwent-rl torch demo: "
            f"device={device_name(device)} "
            f"envs={collector.env_count} rounds={args.rounds} "
            f"steps={collector.total_steps} completed={collector.completed_episodes}"
        )


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Run a tiny torch policy loop through C RL collector")
    parser.add_argument("--num-envs", type=int, default=128)
    parser.add_argument("--max-batch-size", type=int, default=0)
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--enable-invariants", action="store_true")
    parser.add_argument("--library", default=None, help="Path to libgwent_core shared library")
    parser.add_argument("--device", default="auto")
    args = parser.parse_args(argv)
    run(args)


if __name__ == "__main__":
    main()
