from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import time
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Promote a Deck B candidate checkpoint to artifacts/policies/deck_b.pt"
    )
    parser.add_argument("--eval-json", required=True, help="Output JSON from eval_deck_b_progress.py")
    parser.add_argument("--candidate-b", required=True, help="Candidate Deck B checkpoint")
    parser.add_argument("--current-b", default="artifacts/policies/deck_b.pt")
    parser.add_argument("--backup-b", default="artifacts/policies/deck_b.previous.pt")
    parser.add_argument("--promotion-log", default="artifacts/policies/deck_b_promotion.json")
    parser.add_argument(
        "--min-internal-score",
        type=float,
        default=0.48,
        help="Minimum candidate-vs-baseline score_rate. Default 0.48",
    )
    parser.add_argument(
        "--min-external-score-delta",
        type=float,
        default=0.0,
        help="Minimum external score-rate improvement. Default > 0",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Promote even when automatic criteria are not met",
    )
    args = parser.parse_args()

    eval_path = Path(args.eval_json)
    candidate = Path(args.candidate_b)
    current = Path(args.current_b)
    backup = Path(args.backup_b)
    log_path = Path(args.promotion_log)

    if not eval_path.is_file():
        raise FileNotFoundError(f"eval json not found: {eval_path}")
    if not candidate.is_file():
        raise FileNotFoundError(f"candidate checkpoint not found: {candidate}")
    if not current.is_file():
        raise FileNotFoundError(f"current Deck B checkpoint not found: {current}")

    payload = json.loads(eval_path.read_text(encoding="utf-8"))
    summary = payload.get("summary", {})

    ext_delta = float(summary.get("external_score_rate_delta", 0.0))
    internal_score = float(summary.get("candidate_internal_score_rate", 0.0))

    automatic_ok = (
        ext_delta > args.min_external_score_delta
        and internal_score >= args.min_internal_score
    )

    print("=== Deck B Promotion Check ===")
    print(f"eval_json             : {eval_path}")
    print(f"candidate             : {candidate}")
    print(f"current               : {current}")
    print(f"external score delta  : {ext_delta * 100:+.2f} pp")
    print(f"internal score        : {internal_score * 100:.2f}%")
    print(f"threshold external    : > {args.min_external_score_delta * 100:.2f} pp")
    print(f"threshold internal    : >= {args.min_internal_score * 100:.2f}%")
    print(f"automatic eligible    : {automatic_ok}")

    if not automatic_ok and not args.force:
        print("[KEEP] current deck_b.pt kept; candidate not promoted")
        raise SystemExit(2)

    current_sha = sha256_file(current)
    candidate_sha = sha256_file(candidate)

    if current.resolve() == candidate.resolve() or current_sha == candidate_sha:
        print("[KEEP] candidate is identical to current deck_b.pt")
        raise SystemExit(0)

    backup.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    shutil.copy2(current, backup)
    shutil.copy2(candidate, current)

    promoted_sha = sha256_file(current)
    backup_sha = sha256_file(backup)

    record = {
        "schema_version": "gwent-deck-b-promotion-v1",
        "timestamp_unix": time.time(),
        "eval_json": str(eval_path),
        "candidate_source": str(candidate),
        "current_target": str(current),
        "backup_target": str(backup),
        "criteria": {
            "external_score_rate_delta": ext_delta,
            "candidate_internal_score_rate": internal_score,
            "min_external_score_delta": args.min_external_score_delta,
            "min_internal_score": args.min_internal_score,
            "forced": bool(args.force),
        },
        "sha256": {
            "previous": backup_sha,
            "promoted": promoted_sha,
        },
    }
    log_path.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")

    print("[PROMOTED] Deck B candidate is now the official deck_b.pt")
    print(f"backup   : {backup}")
    print(f"new best : {current}")
    print(f"sha256   : {promoted_sha}")
    print(f"log      : {log_path}")


if __name__ == "__main__":
    main()
