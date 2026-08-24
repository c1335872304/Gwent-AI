#!/usr/bin/env python3
"""Generate an offline Qwen/DashScope table for every supported card."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np


def load_records(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if data.get("card_set") != "supported_cards":
        raise ValueError(f"{path} is not the supported_cards catalog")
    records: list[dict[str, Any]] = []
    seen: set[int] = set()
    for card in data.get("cards", []):
        card_id = int(card["id"])
        if card_id in seen:
            raise ValueError(f"duplicate card id: {card_id}")
        seen.add(card_id)
        fields = [
            f"卡牌名称：{card.get('name', '')}",
            f"类型：{card.get('type', '')}",
            f"阵营：{card.get('faction', '')}",
            f"颜色：{card.get('color', '')}",
            f"基础战力：{card.get('power', 0)}",
            f"招募费用：{card.get('provision', 0)}",
            f"类别：{'、'.join(str(value) for value in card.get('categories', []))}",
            f"能力：{card.get('description', '')}",
        ]
        text = "\n".join(fields)
        records.append(
            {
                "card_id": card_id,
                "name": str(card.get("name", card_id)),
                "text": text,
                "text_sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
            }
        )
    if not records:
        raise ValueError(f"no cards found in {path}")
    records.sort(key=lambda record: record["card_id"])
    return records


def load_reusable_rows(path: Path, model: str, dimension: int) -> dict[int, tuple[str, np.ndarray]]:
    if not path.exists():
        return {}
    with np.load(path, allow_pickle=False) as data:
        required = {"card_ids", "embeddings", "text_sha256", "model", "dimension"}
        if not required.issubset(data.files):
            return {}
        stored_model = str(np.asarray(data["model"]).item())
        stored_dimension = int(np.asarray(data["dimension"]).item())
        embeddings = np.asarray(data["embeddings"], dtype=np.float32)
        if stored_model != model or stored_dimension != dimension or embeddings.ndim != 2:
            return {}
        if embeddings.shape[1] != dimension:
            return {}
        card_ids = np.asarray(data["card_ids"], dtype=np.int64)
        hashes = np.asarray(data["text_sha256"]).astype(str)
        if len(card_ids) != len(hashes) or len(card_ids) != len(embeddings):
            return {}
        return {
            int(card_id): (str(text_hash), embeddings[row].copy())
            for row, (card_id, text_hash) in enumerate(zip(card_ids, hashes))
            if int(card_id) != 0
        }


def request_batch(
    texts: list[str], *, api_key: str, model: str, dimension: int, retries: int
) -> list[np.ndarray]:
    try:
        import dashscope
        from dashscope import TextEmbedding
    except ImportError as exc:
        raise RuntimeError("DashScope SDK is missing; install with: pip install -e 'python[qwen]'") from exc

    base_url = os.getenv("DASHSCOPE_BASE_HTTP_API_URL")
    if base_url:
        dashscope.base_http_api_url = base_url

    for attempt in range(retries + 1):
        response = TextEmbedding.call(
            api_key=api_key,
            model=model,
            input=texts,
            dimension=dimension,
            text_type="document",
        )
        status_code = int(getattr(response, "status_code", 0))
        if status_code == 200:
            items = list(response.output["embeddings"])
            if len(items) != len(texts):
                raise RuntimeError(f"DashScope returned {len(items)} rows for {len(texts)} texts")
            ordered: list[np.ndarray | None] = [None] * len(texts)
            for fallback_index, item in enumerate(items):
                index = int(item.get("text_index", fallback_index))
                if index < 0 or index >= len(texts) or ordered[index] is not None:
                    raise RuntimeError("DashScope returned invalid text_index values")
                vector = np.asarray(item["embedding"], dtype=np.float32)
                if vector.shape != (dimension,) or not np.isfinite(vector).all():
                    raise RuntimeError(f"invalid embedding shape/content at batch index {index}")
                ordered[index] = vector
            if any(vector is None for vector in ordered):
                raise RuntimeError("DashScope response omitted an input row")
            return [vector for vector in ordered if vector is not None]

        message = str(getattr(response, "message", "unknown DashScope error"))
        request_id = str(getattr(response, "request_id", "unknown"))
        if attempt >= retries:
            raise RuntimeError(
                f"DashScope request failed after {attempt + 1} attempts: "
                f"status={status_code} request_id={request_id} message={message}"
            )
        delay = min(2**attempt, 30)
        print(f"request failed ({status_code}, {request_id}); retrying in {delay}s", file=sys.stderr)
        time.sleep(delay)
    raise AssertionError("unreachable")


def write_table(
    output: Path,
    records: list[dict[str, Any]],
    vectors: dict[int, np.ndarray],
    *,
    model: str,
    dimension: int,
) -> None:
    card_ids = np.array([0] + [record["card_id"] for record in records], dtype=np.int64)
    names = np.array(["<unk>"] + [record["name"] for record in records], dtype=np.str_)
    hashes = np.array([""] + [record["text_sha256"] for record in records], dtype=np.str_)
    embeddings = np.zeros((len(records) + 1, dimension), dtype=np.float32)
    for row, record in enumerate(records, start=1):
        embeddings[row] = vectors[record["card_id"]]

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.tmp")
    try:
        with temporary.open("wb") as handle:
            np.savez_compressed(
                handle,
                card_ids=card_ids,
                names=names,
                embeddings=embeddings,
                text_sha256=hashes,
                model=np.array(model),
                dimension=np.array(dimension, dtype=np.int64),
            )
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cards", type=Path, default=Path("data/cards/supported_cards.json"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/embeddings/supported_cards_text_embeddings_qwen1024.npz"),
    )
    parser.add_argument("--model", default="text-embedding-v4")
    parser.add_argument("--dimension", type=int, default=1024)
    parser.add_argument("--batch-size", type=int, default=10)
    parser.add_argument("--retries", type=int, default=4)
    parser.add_argument("--dry-run", action="store_true", help="validate input/cache without calling DashScope")
    args = parser.parse_args(argv)

    if args.dimension <= 0 or args.batch_size <= 0 or args.batch_size > 25 or args.retries < 0:
        parser.error("dimension and batch-size must be positive, batch-size <= 25, retries >= 0")

    records = load_records(args.cards)
    reusable = load_reusable_rows(args.output, args.model, args.dimension)
    vectors: dict[int, np.ndarray] = {}
    missing: list[dict[str, Any]] = []
    for record in records:
        cached = reusable.get(record["card_id"])
        if cached is not None and cached[0] == record["text_sha256"]:
            vectors[record["card_id"]] = cached[1]
        else:
            missing.append(record)

    print(f"supported cards={len(records)} reusable={len(vectors)} to_generate={len(missing)}")
    if args.dry_run:
        return 0

    api_key = os.getenv("DASHSCOPE_API_KEY")
    if not api_key:
        raise RuntimeError("DASHSCOPE_API_KEY is not set")
    for start in range(0, len(missing), args.batch_size):
        batch = missing[start : start + args.batch_size]
        generated = request_batch(
            [record["text"] for record in batch],
            api_key=api_key,
            model=args.model,
            dimension=args.dimension,
            retries=args.retries,
        )
        for record, vector in zip(batch, generated):
            vectors[record["card_id"]] = vector
        print(f"generated {min(start + len(batch), len(missing))}/{len(missing)}")

    write_table(args.output, records, vectors, model=args.model, dimension=args.dimension)
    print(f"wrote {args.output} ({len(records) + 1} rows x {args.dimension})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
