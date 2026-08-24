#!/usr/bin/env python3
"""Validate and install a trained checkpoint into the local final model slot.

Usage:
    python scripts/install_model.py /path/to/best.pt

The source is validated through the same loader used by inference. Installation is
atomic and records a small local provenance file for runtime provenance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
PYTHON_SRC = ROOT / "python" / "src"
DEFAULT_DEST = ROOT / "models" / "v3" / "policy.pt"

if str(PYTHON_SRC) not in sys.path:
    sys.path.insert(0, str(PYTHON_SRC))

from gwent_rl.experiment import policy_from_checkpoint  # noqa: E402


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def compact_metadata(metadata: dict[str, Any]) -> dict[str, Any]:
    keys = (
        "schema_version",
        "action_grammar_version",
        "reward_config_version",
        "prefix_semantics",
        "update",
        "total_decisions",
        "total_games",
        "created_unix",
    )
    return {key: metadata[key] for key in keys if key in metadata}


def install(source: Path, destination: Path) -> dict[str, Any]:
    source = source.expanduser().resolve()
    destination = destination.expanduser().resolve()

    if not source.is_file():
        raise FileNotFoundError(f"checkpoint does not exist: {source}")
    if source.stat().st_size <= 0:
        raise ValueError(f"checkpoint is empty: {source}")

    # Production loader performs current schema/action-grammar/model-shape validation.
    _policy, metadata = policy_from_checkpoint(source, device="cpu")
    digest = sha256_file(source)

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        dir=destination.parent,
        prefix=destination.name + ".",
        suffix=".tmp",
        delete=False,
    ) as tmp:
        tmp_path = Path(tmp.name)
    try:
        shutil.copyfile(source, tmp_path)
        os.replace(tmp_path, destination)
    finally:
        if tmp_path.exists():
            tmp_path.unlink()

    record = {
        "checkpoint": destination.name,
        "source_filename": source.name,
        "sha256": digest,
        "size_bytes": destination.stat().st_size,
        "metadata": compact_metadata(dict(metadata)),
    }
    record_path = destination.parent / "installed.json"
    record_path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="downloaded compatible .pt checkpoint")
    parser.add_argument(
        "--destination",
        type=Path,
        default=DEFAULT_DEST,
        help=f"install destination (default: {DEFAULT_DEST.relative_to(ROOT)})",
    )
    args = parser.parse_args()

    try:
        record = install(args.source, args.destination)
    except Exception as exc:
        print(f"[ERROR] model install failed: {exc}", file=sys.stderr)
        return 1

    print(f"[OK] installed: {args.destination.resolve()}")
    print(f"[OK] sha256: {record['sha256']}")
    meta = record.get("metadata", {})
    print(
        "[OK] contract: "
        f"schema={meta.get('schema_version', '?')} "
        f"grammar={meta.get('action_grammar_version', '?')} "
        f"update={meta.get('update', '?')}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
