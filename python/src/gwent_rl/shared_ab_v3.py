from __future__ import annotations

import argparse
import copy
import hashlib
import time
from pathlib import Path
from typing import Any, Mapping

import torch

from .experiment import _validate_checkpoint_schema
from .policy import (
    CandidatePolicyValueNet,
    SharedDeckHeadsPolicyValueNet,
    SharedDeckPrivatePolicyValueNet,
)


def _sha256(path: str | Path) -> str:
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _payload(path: str | Path) -> tuple[dict[str, torch.Tensor], dict[str, Any]]:
    raw = torch.load(path, map_location="cpu", weights_only=False)
    if not isinstance(raw, Mapping):
        raise ValueError(f"checkpoint {path} is not a mapping payload")
    metadata = dict(raw.get("metadata", {}))
    _validate_checkpoint_schema(metadata, path)
    state = raw.get("model", raw)
    if not isinstance(state, Mapping):
        raise ValueError(f"checkpoint {path} does not contain model state")
    return dict(state), metadata


def _architecture(metadata: Mapping[str, Any]) -> str:
    manifest = metadata.get("model_manifest", {}) if isinstance(metadata, Mapping) else {}
    if isinstance(manifest, Mapping) and manifest.get("architecture"):
        return str(manifest["architecture"]).strip().lower()
    training = metadata.get("training_config", {}) if isinstance(metadata, Mapping) else {}
    model = training.get("model", {}) if isinstance(training, Mapping) else {}
    if isinstance(model, Mapping) and model.get("architecture"):
        return str(model["architecture"]).strip().lower()
    return "single_head"


def _model_kwargs(metadata: Mapping[str, Any], state: Mapping[str, torch.Tensor]) -> dict[str, Any]:
    training_config = metadata.get("training_config", {}) if isinstance(metadata, Mapping) else {}
    model_cfg = training_config.get("model", {}) if isinstance(training_config, Mapping) else {}
    manifest = metadata.get("model_manifest", {}) if isinstance(metadata, Mapping) else {}
    cfg = {
        **(model_cfg if isinstance(model_cfg, Mapping) else {}),
        **(manifest if isinstance(manifest, Mapping) else {}),
    }
    text_weight = state.get("text_embedding.weight")
    text_dim = int(cfg.get("text_embedding_dim", 0))
    if text_weight is not None and getattr(text_weight, "ndim", 0) == 2:
        text_dim = int(text_weight.shape[1])
    return {
        "hidden_dim": int(cfg.get("hidden_dim", 128)),
        "num_attention_heads": int(cfg.get("num_attention_heads", 4)),
        "attention_layers": int(cfg.get("attention_layers", 1)),
        "dropout": float(cfg.get("dropout", 0.0)),
        "card_vocab_size": int(cfg.get("card_vocab_size", 4096)),
        "text_embedding_dim": text_dim,
        "freeze_text_embeddings": bool(cfg.get("freeze_text_embeddings", True)),
        "use_candidate_object_attention": bool(cfg.get("use_candidate_object_attention", True)),
        "use_candidate_self_attention": bool(cfg.get("use_candidate_self_attention", True)),
    }


def _check_contract(a_meta: Mapping[str, Any], b_meta: Mapping[str, Any]) -> None:
    for key in ("schema_version", "action_grammar_version", "prefix_semantics"):
        if a_meta.get(key) != b_meta.get(key):
            raise ValueError(f"A/B checkpoints disagree on {key}: {a_meta.get(key)!r} != {b_meta.get(key)!r}")


def _check_model_shape_compatibility(
    a_meta: Mapping[str, Any],
    a_state: Mapping[str, torch.Tensor],
    b_meta: Mapping[str, Any],
    b_state: Mapping[str, torch.Tensor],
) -> dict[str, Any]:
    a_kwargs = _model_kwargs(a_meta, a_state)
    b_kwargs = _model_kwargs(b_meta, b_state)
    keys = (
        "hidden_dim",
        "num_attention_heads",
        "attention_layers",
        "card_vocab_size",
        "text_embedding_dim",
        "use_candidate_object_attention",
        "use_candidate_self_attention",
    )
    mismatches = {k: (a_kwargs[k], b_kwargs[k]) for k in keys if a_kwargs[k] != b_kwargs[k]}
    if mismatches:
        raise ValueError(f"A/B model shapes are incompatible: {mismatches}")
    # Preserve the shared source's dropout/freeze behavior; shape-critical values match.
    return b_kwargs


_PRIVATE_TO_BASE = {
    "global_encoders": "global_encoder",
    "decision_embeddings": "decision_embedding",
    "prefix_grus": "prefix_gru",
    "source_missing_by_deck": "source_missing",
    "state_norms": "state_norm",
    "prefix_norms": "prefix_norm",
    "relation_norms": "relation_norm",
    "option_norms": "option_norm",
    "source_target_gates": "source_target_gate",
    "target_gates": "target_gate",
    "candidate_object_attentions": "candidate_object_attention",
    "candidate_self_attentions": "candidate_self_attention",
    "candidate_ffns": "candidate_ffn",
    "logit_heads": "logit_head",
    "value_heads": "value_head",
}


def _source_key_for_private(
    target_key: str,
    *,
    source_architecture: str,
    source_branch: str,
) -> str | None:
    parts = target_key.split(".")
    root = parts[0]
    if root in {"state_adapters", "candidate_adapters"}:
        if source_architecture == SharedDeckPrivatePolicyValueNet.ARCHITECTURE:
            parts[1] = source_branch
            return ".".join(parts)
        return None
    if root not in _PRIVATE_TO_BASE or len(parts) < 2:
        raise ValueError(f"unsupported V3 private target key: {target_key}")
    remainder = parts[2:]
    if source_architecture == SharedDeckPrivatePolicyValueNet.ARCHITECTURE:
        return ".".join([root, source_branch, *remainder])
    if source_architecture == SharedDeckHeadsPolicyValueNet.ARCHITECTURE:
        if root in {"logit_heads", "value_heads"}:
            return ".".join([root, source_branch, *remainder])
        base = _PRIVATE_TO_BASE[root]
        return ".".join([base, *remainder]) if remainder else base
    if source_architecture in {"", "single_head"}:
        base = _PRIVATE_TO_BASE[root]
        return ".".join([base, *remainder]) if remainder else base
    raise ValueError(f"unsupported source architecture {source_architecture!r}")


def _copy_private_partition(
    target_state: dict[str, torch.Tensor],
    source_state: Mapping[str, torch.Tensor],
    *,
    target_deck: str,
    source_architecture: str,
    source_branch: str,
) -> tuple[int, list[str]]:
    copied = 0
    initialized: list[str] = []
    wanted = f"{target_deck}_private"
    for key, current in list(target_state.items()):
        if SharedDeckPrivatePolicyValueNet.state_key_partition(key) != wanted:
            continue
        source_key = _source_key_for_private(
            key,
            source_architecture=source_architecture,
            source_branch=source_branch,
        )
        if source_key is None:
            initialized.append(key)
            continue
        source_value = source_state.get(source_key)
        if source_value is None:
            raise ValueError(f"source is missing {source_key!r} needed for {key!r}")
        if source_value.shape != current.shape:
            raise ValueError(
                f"shape mismatch {source_key} -> {key}: source={tuple(source_value.shape)} "
                f"target={tuple(current.shape)}"
            )
        target_state[key] = source_value.detach().clone()
        copied += 1
    return copied, initialized


def _shared_value(
    key: str,
    a_state: Mapping[str, torch.Tensor],
    b_state: Mapping[str, torch.Tensor],
    source: str,
) -> torch.Tensor:
    a = a_state.get(key)
    b = b_state.get(key)
    if a is None or b is None:
        missing = "A" if a is None else "B"
        raise ValueError(f"{missing} source is missing shared key {key!r}")
    if a.shape != b.shape:
        raise ValueError(f"shared key {key} has incompatible A/B shapes: {tuple(a.shape)} vs {tuple(b.shape)}")
    if source == "a":
        return a.detach().clone()
    if source == "b":
        return b.detach().clone()
    if source != "mean":
        raise ValueError("shared_source must be 'mean', 'a', or 'b'")
    if not (torch.is_floating_point(a) or torch.is_complex(a)):
        if not torch.equal(a, b):
            raise ValueError(f"non-floating shared buffer differs between A/B for {key!r}")
        return a.detach().clone()
    return ((a.detach().to(torch.float32) + b.detach().to(torch.float32)) * 0.5).to(dtype=a.dtype)


def build_v3_checkpoint(
    deck_a_checkpoint: str | Path,
    deck_b_checkpoint: str | Path,
    output: str | Path,
    *,
    shared_source: str = "mean",
    deck_a_branch: str = "a",
    deck_b_branch: str = "b",
) -> Path:
    """Build a V3 checkpoint from A and B teachers.

    A/B teachers may be legacy single-head, V2, or V3 checkpoints.  Shared
    semantic weights are copied from A, B, or their arithmetic mean.  Each
    private strategy trunk is copied from its own teacher branch.  V3-only
    residual adapters are zero-initialized when importing older teachers.
    """
    a_state, a_meta = _payload(deck_a_checkpoint)
    b_state, b_meta = _payload(deck_b_checkpoint)
    _check_contract(a_meta, b_meta)
    kwargs = _check_model_shape_compatibility(a_meta, a_state, b_meta, b_state)
    a_arch = _architecture(a_meta)
    b_arch = _architecture(b_meta)
    if deck_a_branch not in {"a", "b"} or deck_b_branch not in {"a", "b"}:
        raise ValueError("deck_a_branch/deck_b_branch must be 'a' or 'b'")
    if a_arch == "single_head":
        deck_a_branch = "a"
    if b_arch == "single_head":
        deck_b_branch = "b"

    model = SharedDeckPrivatePolicyValueNet(**kwargs)
    # Match checkpoint text-table shape before loading state.
    text_weight = b_state.get("text_embedding.weight")
    if text_weight is None:
        text_weight = a_state.get("text_embedding.weight")
    text_lookup = b_state.get("text_card_id_lookup")
    if text_lookup is None:
        text_lookup = a_state.get("text_card_id_lookup")
    if text_weight is not None and getattr(text_weight, "ndim", 0) == 2:
        lookup_size = int(text_lookup.shape[0]) if text_lookup is not None else 1
        model._init_text_embedding_modules(text_weight.shape[0], text_weight.shape[1], lookup_size)

    target = {k: v.detach().clone() for k, v in model.state_dict().items()}
    shared_copied = 0
    for key, current in list(target.items()):
        if SharedDeckPrivatePolicyValueNet.state_key_partition(key) != "shared":
            continue
        value = _shared_value(key, a_state, b_state, shared_source)
        if value.shape != current.shape:
            raise ValueError(f"shared shape mismatch for {key}: {tuple(value.shape)} != {tuple(current.shape)}")
        target[key] = value
        shared_copied += 1

    a_copied, a_initialized = _copy_private_partition(
        target,
        a_state,
        target_deck="a",
        source_architecture=a_arch,
        source_branch=deck_a_branch,
    )
    b_copied, b_initialized = _copy_private_partition(
        target,
        b_state,
        target_deck="b",
        source_architecture=b_arch,
        source_branch=deck_b_branch,
    )
    model.load_state_dict(target, strict=True)

    metadata = copy.deepcopy(dict(b_meta))
    training_config = copy.deepcopy(metadata.get("training_config", {}))
    if not isinstance(training_config, dict):
        training_config = {}
    model_cfg = copy.deepcopy(training_config.get("model", {}))
    if not isinstance(model_cfg, dict):
        model_cfg = {}
    model_cfg["architecture"] = SharedDeckPrivatePolicyValueNet.ARCHITECTURE
    model_cfg.update({
        "hidden_dim": model.hidden_dim,
        "num_attention_heads": model.num_attention_heads,
        "attention_layers": model.attention_layers,
        "dropout": model.dropout,
        "card_vocab_size": model.card_vocab_size,
        "text_embedding_dim": model.text_embedding_dim,
        "freeze_text_embeddings": model.freeze_text_embeddings,
        "use_candidate_object_attention": model.use_candidate_object_attention,
        "use_candidate_self_attention": model.use_candidate_self_attention,
    })
    training_config["model"] = model_cfg
    metadata["training_config"] = training_config
    metadata["model_manifest"] = model.model_manifest()
    metadata["architecture_version"] = SharedDeckPrivatePolicyValueNet.ARCHITECTURE
    metadata["update"] = 0
    metadata["total_decisions"] = 0
    metadata["total_games"] = 0
    metadata["created_unix"] = time.time()
    metadata["v3_initialization"] = {
        "shared_source": shared_source,
        "deck_a_checkpoint": str(Path(deck_a_checkpoint)),
        "deck_b_checkpoint": str(Path(deck_b_checkpoint)),
        "deck_a_sha256": _sha256(deck_a_checkpoint),
        "deck_b_sha256": _sha256(deck_b_checkpoint),
        "deck_a_source_architecture": a_arch,
        "deck_b_source_architecture": b_arch,
        "deck_a_source_branch": deck_a_branch,
        "deck_b_source_branch": deck_b_branch,
        "deck_a_source_update": int(a_meta.get("update", 0)),
        "deck_b_source_update": int(b_meta.get("update", 0)),
        "copied_shared_keys": shared_copied,
        "copied_a_private_keys": a_copied,
        "copied_b_private_keys": b_copied,
        "a_target_initialized_keys": a_initialized,
        "b_target_initialized_keys": b_initialized,
        "parameter_partition_trainable": model.parameter_partition_report(trainable_only=True),
    }

    out = Path(output)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".tmp")
    torch.save({"model": model.state_dict(), "metadata": metadata}, tmp)
    tmp.replace(out)
    return out


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Build gwent V3 50/50 shared/private A/B checkpoint")
    parser.add_argument("--deck-a", required=True, help="A teacher checkpoint (normally old deck_a.pt)")
    parser.add_argument("--deck-b", required=True, help="B teacher checkpoint (V2 best is recommended)")
    parser.add_argument("--output", required=True)
    parser.add_argument("--shared-source", choices=["mean", "a", "b"], default="mean")
    parser.add_argument("--deck-a-branch", choices=["a", "b"], default="a")
    parser.add_argument("--deck-b-branch", choices=["a", "b"], default="b")
    args = parser.parse_args(argv)
    out = build_v3_checkpoint(
        args.deck_a,
        args.deck_b,
        args.output,
        shared_source=args.shared_source,
        deck_a_branch=args.deck_a_branch,
        deck_b_branch=args.deck_b_branch,
    )
    _, meta = _payload(out)
    report = meta["v3_initialization"]["parameter_partition_trainable"]
    print(f"[GWENT_V3] wrote {out}")
    print(
        "[GWENT_V3] active path trainable split "
        f"A shared/private={report['a_shared_fraction']:.3f}/{report['a_private_fraction']:.3f} "
        f"B shared/private={report['b_shared_fraction']:.3f}/{report['b_private_fraction']:.3f}"
    )


if __name__ == "__main__":
    main()
