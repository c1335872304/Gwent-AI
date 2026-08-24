#!/usr/bin/env python3
"""Record a few real policy-vs-policy games as structured decision traces.

Designed for two consumers:
1. human diagnostics (why did the policy choose this legal option?);
2. the future Teacher Agent (same JSON contract, no rule/reward duplication).
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python" / "src"))

from gwent_rl.collector import RlCollector
from gwent_rl.decision_trace import attach_apply_result, decision_from_batch_row, load_card_catalog, write_trace
from gwent_rl.device import device_name, resolve_torch_device
from gwent_rl.experiment import policy_from_checkpoint
from gwent_rl.policy import greedy_actions


def _short_option(option: dict) -> str:
    name = option.get("card_name") or str(option.get("card_id"))
    return (
        f"#{option['option_index']} {option['option_kind']} {name} "
        f"target_obj={option['target_object_index']} row={option['target_row_id']} "
        f"pos={option['insert_position']} p={option['probability']:.3f}"
    )


@torch.no_grad()
def record(args: argparse.Namespace) -> Path:
    if args.games <= 0:
        raise ValueError("--games must be > 0")
    # One environment is deliberate: it gives one ordered, readable game stream.
    device = resolve_torch_device(args.device)
    policy0, meta0 = policy_from_checkpoint(args.checkpoint_p0, device=device)
    policy1, meta1 = policy_from_checkpoint(args.checkpoint_p1 or args.checkpoint_p0, device=device)
    policy0.eval(); policy1.eval()
    catalog = load_card_catalog(ROOT)

    decisions: list[dict] = []
    game_summaries: list[dict] = []
    current_game_no = 1
    decision_in_game = 0

    with RlCollector(
        num_envs=1,
        max_batch_size=1,
        base_seed=args.seed,
        starting_player_id=args.starting_player,
        shuffle_decks=not args.no_shuffle,
        auto_reset_done_envs=True,
        reward_mode=args.reward_mode,
        player0_deck=args.player0_deck,
        player1_deck=args.player1_deck,
        library_path=args.library,
    ) as collector:
        collector.set_game_limit(args.games)
        safety = 0
        while len(game_summaries) < args.games:
            safety += 1
            if safety > args.games * 4096:
                raise RuntimeError("trace recorder exceeded safety decision bound")
            batch = collector.collect()
            if batch.count == 0:
                continue
            tensors = batch.to_torch(device=device)
            actor = int(batch.actor_ids[0])
            policy = policy0 if actor == 0 else policy1
            out = policy(tensors)
            action = int(greedy_actions(out.logits, tensors["option_mask"])[0].item())
            actor_deck = args.player0_deck if actor == 0 else args.player1_deck
            opp_deck = args.player1_deck if actor == 0 else args.player0_deck

            snap = decision_from_batch_row(
                batch,
                0,
                logits=out.logits,
                values=out.values,
                chosen_option_index=action,
                actor_deck=actor_deck,
                opponent_deck=opp_deck,
                card_catalog=catalog,
            )
            results = collector.apply_actions(batch, np.asarray([action], dtype=np.uint64))
            row = attach_apply_result(snap, results, 0)
            row["game_no"] = current_game_no
            row["decision_no"] = decision_in_game + 1
            decisions.append(row)
            decision_in_game += 1

            if args.print_steps:
                chosen = next((o for o in row["legal_options"] if o["option_index"] == action), None)
                g = row["globals"]
                print(
                    f"G{current_game_no:02d} D{decision_in_game:03d} P{actor}({actor_deck}) "
                    f"R{int(g['round_no'])} T{int(g['turn_no'])} "
                    f"score={int(g['actor_score'])}:{int(g['opponent_score'])} "
                    f"frost_self={int(g['actor_melee_frost_duration'])}/{int(g['actor_ranged_frost_duration'])} "
                    f"frost_opp={int(g['opponent_melee_frost_duration'])}/{int(g['opponent_ranged_frost_duration'])} "
                    f"-> {_short_option(chosen) if chosen else action}"
                )

            if int(results.dones[0]) != 0:
                winner = int(results.winner_ids[0])
                game_summaries.append({
                    "game_no": current_game_no,
                    "winner_id": winner,
                    "winner_deck": args.player0_deck if winner == 0 else args.player1_deck if winner == 1 else "draw",
                    "decision_count": decision_in_game,
                })
                print(f"[GAME {current_game_no}] winner={winner} ({game_summaries[-1]['winner_deck']}) decisions={decision_in_game}")
                current_game_no += 1
                decision_in_game = 0

    metadata = {
        "purpose": "policy diagnostics and future Teacher Agent evidence",
        "checkpoint_p0": str(Path(args.checkpoint_p0).resolve()),
        "checkpoint_p1": str(Path(args.checkpoint_p1 or args.checkpoint_p0).resolve()),
        "player0_deck": args.player0_deck,
        "player1_deck": args.player1_deck,
        "games": args.games,
        "seed": args.seed,
        "starting_player": args.starting_player,
        "shuffle_decks": not args.no_shuffle,
        "reward_mode": args.reward_mode,
        "device": device_name(device),
        "checkpoint0_metadata": {k: meta0.get(k) for k in ("update", "total_games", "observation_schema_version", "action_grammar_version") if k in meta0},
        "checkpoint1_metadata": {k: meta1.get(k) for k in ("update", "total_games", "observation_schema_version", "action_grammar_version") if k in meta1},
    }
    out = write_trace(args.output, metadata=metadata, decisions=decisions, games=game_summaries)
    print(f"[DONE] games={len(game_summaries)} decisions={len(decisions)}")
    print(f"[TRACE] {out}")
    return out


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Record structured checkpoint-vs-checkpoint decision traces")
    p.add_argument("--checkpoint-p0", required=True)
    p.add_argument("--checkpoint-p1", default=None, help="defaults to --checkpoint-p0")
    p.add_argument("--player0-deck", default="b")
    p.add_argument("--player1-deck", default="a")
    p.add_argument("--games", type=int, default=5)
    p.add_argument("--seed", type=int, default=20260820)
    p.add_argument("--starting-player", type=int, choices=[-1, 0, 1], default=-1)
    p.add_argument("--no-shuffle", action="store_true")
    p.add_argument("--reward-mode", default="terminal")
    p.add_argument("--library", default="build-release/libgwent_core.so")
    p.add_argument("--device", default="auto")
    p.add_argument("--output", default="runs/analysis/policy_trace.json")
    p.add_argument("--print-steps", action="store_true")
    return p


def main(argv: list[str] | None = None) -> None:
    record(build_parser().parse_args(argv))


if __name__ == "__main__":
    main()
