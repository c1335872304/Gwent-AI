from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np

from .collector import RlCollector, random_legal_actions
from .explain import explain_batch_row


@dataclass
class ReplayAction:
    env_index: int
    decision_serial: int
    option_index: int
    actor_id: int
    option_count: int
    done: int = 0
    winner_id: int = -1
    result_code: int = 0
    action_status: int = 0
    reward0: float = 0.0
    reward1: float = 0.0


@dataclass
class ReplayTrace:
    base_seed: int
    num_envs: int
    reward_mode: str
    actions: list[ReplayAction]

    def to_json(self) -> dict[str, Any]:
        return {
            "schema": "gwent-rl-replay-v2",
            "base_seed": self.base_seed,
            "num_envs": self.num_envs,
            "reward_mode": self.reward_mode,
            "actions": [asdict(a) for a in self.actions],
        }


def record_random_trace(
    num_envs: int,
    rounds: int,
    seed: int,
    reward_mode: str = "terminal",
    library_path: str | None = None,
) -> ReplayTrace:
    rng = np.random.default_rng(seed)
    actions: list[ReplayAction] = []
    with RlCollector(
        num_envs=num_envs,
        max_batch_size=num_envs,
        base_seed=seed,
        reward_mode=reward_mode,
        library_path=library_path,
    ) as collector:
        for _ in range(rounds):
            batch = collector.collect()
            chosen = random_legal_actions(batch, rng)
            results = collector.apply_actions(batch, chosen)
            for row, option in enumerate(chosen):
                actions.append(ReplayAction(
                    env_index=int(batch.env_indices[row]),
                    decision_serial=int(batch.decision_serials[row]),
                    option_index=int(option),
                    actor_id=int(batch.actor_ids[row]),
                    option_count=int(batch.option_counts[row]),
                    done=int(results.dones[row]),
                    winner_id=int(results.winner_ids[row]),
                    result_code=int(results.result_codes[row]),
                    action_status=int(results.action_statuses[row]),
                    reward0=float(results.rewards[row, 0]),
                    reward1=float(results.rewards[row, 1]),
                ))
    return ReplayTrace(base_seed=seed, num_envs=num_envs, reward_mode=reward_mode, actions=actions)


def replay_trace(path: str | Path, max_mismatches: int = 1, library_path: str | None = None) -> dict[str, int]:
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    base_seed = int(payload["base_seed"])
    num_envs = int(payload["num_envs"])
    reward_mode = str(payload.get("reward_mode", "terminal"))
    expected = list(payload.get("actions", []))
    cursor = 0
    mismatches = 0
    applied = 0
    with RlCollector(
        num_envs=num_envs,
        max_batch_size=num_envs,
        base_seed=base_seed,
        reward_mode=reward_mode,
        library_path=library_path,
    ) as collector:
        while cursor < len(expected):
            batch = collector.collect()
            selected = np.zeros((batch.count,), dtype=np.uint64)
            expected_rows: list[dict[str, Any] | None] = []
            for row in range(batch.count):
                if cursor >= len(expected):
                    selected[row] = 0
                    expected_rows.append(None)
                    continue
                item = expected[cursor]
                env = int(batch.env_indices[row])
                serial = int(batch.decision_serials[row])
                if int(item["env_index"]) != env or int(item["decision_serial"]) != serial:
                    mismatches += 1
                    if mismatches <= max_mismatches:
                        print("mismatch", {"cursor": cursor, "expected": item, "actual_env": env, "actual_serial": serial})
                        print(explain_batch_row(batch, row))
                    if mismatches >= max_mismatches:
                        return {"applied": applied, "mismatches": mismatches}
                    selected[row] = 0
                    expected_rows.append(None)
                else:
                    selected[row] = int(item["option_index"])
                    expected_rows.append(item)
                    cursor += 1
            results = collector.apply_actions(batch, selected)
            matched_count = sum(1 for item in expected_rows if item is not None)
            for row, item in enumerate(expected_rows):
                if item is None:
                    continue
                checks = {
                    "result_code": int(results.result_codes[row]),
                    "action_status": int(results.action_statuses[row]),
                    "done": int(results.dones[row]),
                    "winner_id": int(results.winner_ids[row]),
                }
                for key, actual in checks.items():
                    if int(item.get(key, actual)) != actual:
                        mismatches += 1
                        if mismatches <= max_mismatches:
                            print("result mismatch", {"cursor": applied + row, "key": key, "expected": item.get(key), "actual": actual})
                        if mismatches >= max_mismatches:
                            return {"applied": applied, "mismatches": mismatches}
                for key, actual in (("reward0", float(results.rewards[row, 0])), ("reward1", float(results.rewards[row, 1]))):
                    if abs(float(item.get(key, actual)) - actual) > 1e-6:
                        mismatches += 1
                        if mismatches <= max_mismatches:
                            print("reward mismatch", {"cursor": applied + row, "key": key, "expected": item.get(key), "actual": actual})
                        if mismatches >= max_mismatches:
                            return {"applied": applied, "mismatches": mismatches}
            applied += int(matched_count)
    return {"applied": applied, "mismatches": mismatches}


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Record or replay RL collector traces for debugging")
    sub = parser.add_subparsers(dest="cmd")

    rec = sub.add_parser("record", help="record a random-policy replay trace")
    rec.add_argument("--num-envs", type=int, default=8)
    rec.add_argument("--rounds", type=int, default=8)
    rec.add_argument("--seed", type=int, default=0)
    rec.add_argument("--reward-mode", default="terminal")
    rec.add_argument("--output", required=True)
    rec.add_argument("--library", default=None)

    rep = sub.add_parser("replay", help="replay a recorded trace")
    rep.add_argument("--input", required=True)
    rep.add_argument("--max-mismatches", type=int, default=1)
    rep.add_argument("--library", default=None)

    # Backward-compatible one-shot record mode.
    parser.add_argument("--num-envs", type=int, default=None)
    parser.add_argument("--rounds", type=int, default=None)
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--reward-mode", default=None)
    parser.add_argument("--output", default=None)
    parser.add_argument("--library", default=None)

    args = parser.parse_args(argv)
    if args.cmd == "replay":
        stats = replay_trace(args.input, args.max_mismatches, library_path=args.library)
        print(json.dumps(stats, sort_keys=True))
        return
    if args.cmd == "record" or args.output:
        num_envs = args.num_envs if args.num_envs is not None else 8
        rounds = args.rounds if args.rounds is not None else 8
        seed = args.seed if args.seed is not None else 0
        reward_mode = args.reward_mode or "terminal"
        output = args.output
        if output is None:
            raise SystemExit("--output is required")
        trace = record_random_trace(num_envs, rounds, seed, reward_mode, library_path=args.library)
        path = Path(output)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(trace.to_json(), indent=2), encoding="utf-8")
        print(f"wrote {path} actions={len(trace.actions)}")
        return
    parser.print_help()


if __name__ == "__main__":
    main()
