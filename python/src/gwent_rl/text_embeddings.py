from __future__ import annotations

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .card_vocab import load_deck_a_card_records

DEFAULT_QWEN1024_EMBEDDING_PATH = "data/embeddings/supported_cards_text_embeddings_qwen1024.npz"


def _repo_root() -> Path:
    # python/src/gwent_rl/text_embeddings.py -> repo root
    return Path(__file__).resolve().parents[3]


def _resolve_project_path(path: str | Path) -> Path:
    p = Path(path)
    if p.exists() or p.is_absolute():
        return p
    candidate = _repo_root() / p
    if candidate.exists():
        return candidate
    return p


def hash_text_embedding(text: str, dim: int = 128) -> np.ndarray:
    vec = np.zeros((dim,), dtype=np.float32)
    tokens = text.replace(",", " ").replace(".", " ").replace(":", " ").replace(";", " ").split()
    if not tokens:
        tokens = [text]
    for token in tokens:
        digest = hashlib.blake2b(token.encode("utf-8"), digest_size=16).digest()
        idx = int.from_bytes(digest[:4], "little") % dim
        sign = 1.0 if (digest[4] & 1) else -1.0
        vec[idx] += sign
    norm = np.linalg.norm(vec)
    if norm > 0:
        vec /= norm
    return vec


@dataclass(frozen=True)
class TextEmbeddingTable:
    """Offline card text embeddings indexed by raw card id.

    Row 0 is reserved for unknown/padding.  `card_ids[i]` is the raw game card
    id represented by `embeddings[i]`.
    """

    card_ids: np.ndarray
    names: np.ndarray
    embeddings: np.ndarray
    source: str = "unknown"

    @property
    def dim(self) -> int:
        return int(self.embeddings.shape[1]) if self.embeddings.ndim == 2 else 0

    @property
    def row_count(self) -> int:
        return int(self.embeddings.shape[0])

    def validate(self) -> "TextEmbeddingTable":
        card_ids = np.asarray(self.card_ids, dtype=np.int64)
        embeddings = np.asarray(self.embeddings, dtype=np.float32)
        if embeddings.ndim != 2:
            raise ValueError("text embeddings must be a 2D array")
        if card_ids.ndim != 1:
            raise ValueError("card_ids must be a 1D array")
        if card_ids.shape[0] != embeddings.shape[0]:
            raise ValueError("card_ids and embeddings must have the same row count")
        if embeddings.shape[0] == 0 or card_ids[0] != 0:
            card_ids = np.concatenate([np.array([0], dtype=np.int64), card_ids])
            embeddings = np.concatenate([np.zeros((1, embeddings.shape[1]), dtype=np.float32), embeddings], axis=0)
        names = np.asarray(self.names, dtype=object)
        if names.shape[0] != embeddings.shape[0]:
            names = np.array(["<unk>"] + [str(cid) for cid in card_ids[1:]], dtype=object)
        return TextEmbeddingTable(card_ids=card_ids, names=names, embeddings=embeddings, source=self.source)

    def lookup_array(self) -> np.ndarray:
        table = self.validate()
        max_id = int(np.max(table.card_ids)) if table.card_ids.size else 0
        lookup = np.zeros((max_id + 1,), dtype=np.int64)
        for row, card_id in enumerate(table.card_ids):
            if int(card_id) >= 0:
                lookup[int(card_id)] = int(row)
        return lookup


def make_hash_embedding_table(dim: int = 128, deck_json: str | Path | None = None) -> TextEmbeddingTable:
    records = load_deck_a_card_records(deck_json)
    card_ids = np.array([0] + [rec.card_id for rec in records], dtype=np.int64)
    names = np.array(["<unk>"] + [rec.name for rec in records], dtype=object)
    embeddings = np.zeros((len(records) + 1, dim), dtype=np.float32)
    for i, rec in enumerate(records, start=1):
        embeddings[i] = hash_text_embedding(rec.text, dim)
    return TextEmbeddingTable(card_ids=card_ids, names=names, embeddings=embeddings, source="hash").validate()


def load_text_embedding_table(path: str | Path) -> TextEmbeddingTable:
    p = _resolve_project_path(path)
    data = np.load(p, allow_pickle=True)
    if "embeddings" not in data or "card_ids" not in data:
        raise ValueError(f"{p} must contain card_ids and embeddings arrays")
    names = data["names"] if "names" in data else np.array([], dtype=object)
    return TextEmbeddingTable(
        card_ids=np.asarray(data["card_ids"], dtype=np.int64),
        names=np.asarray(names, dtype=object),
        embeddings=np.asarray(data["embeddings"], dtype=np.float32),
        source=str(p),
    ).validate()


def resolve_text_embedding_table(
    mode: str = "none",
    path: str | Path | None = None,
    dim: int = 128,
    deck_json: str | Path | None = None,
) -> TextEmbeddingTable | None:
    key = str(mode or "none").strip().lower()
    if key in {"none", "off", "false", "0"}:
        return None
    if key == "hash":
        if path:
            p = _resolve_project_path(path)
            if p.exists():
                return load_text_embedding_table(p)
        return make_hash_embedding_table(dim=dim, deck_json=deck_json)
    if key == "npz":
        if not path:
            path = DEFAULT_QWEN1024_EMBEDDING_PATH
        return load_text_embedding_table(path)
    raise ValueError(f"unknown text embedding mode: {mode!r}")


def make_hash_embeddings(output: str | Path, dim: int = 128, deck_json: str | Path | None = None) -> Path:
    table = make_hash_embedding_table(dim=dim, deck_json=deck_json)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    np.savez(output, card_ids=table.card_ids, names=table.names, embeddings=table.embeddings)
    return output


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Create deterministic hash text embeddings for Deck A")
    parser.add_argument("--output", required=True)
    parser.add_argument("--dim", type=int, default=128)
    parser.add_argument("--deck-json", default=None)
    args = parser.parse_args(argv)
    path = make_hash_embeddings(args.output, args.dim, args.deck_json)
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
