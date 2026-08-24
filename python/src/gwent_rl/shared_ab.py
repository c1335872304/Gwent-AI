from __future__ import annotations

import argparse
import copy
import hashlib
import time
from pathlib import Path
from typing import Any, Mapping

import torch

from .experiment import _validate_checkpoint_schema
from .policy import CandidatePolicyValueNet, SharedDeckHeadsPolicyValueNet


def _sha256(path: str | Path) -> str:
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _payload(path: str | Path) -> tuple[dict[str, torch.Tensor], dict[str, Any], Mapping[str, Any]]:
    raw = torch.load(path, map_location="cpu", weights_only=False)
    if not isinstance(raw, Mapping):
        raise ValueError(f"checkpoint {path} is not a mapping payload")
    meta = dict(raw.get("metadata", {}))
    _validate_checkpoint_schema(meta, path)
    state = raw.get("model", raw)
    if not isinstance(state, Mapping):
        raise ValueError(f"checkpoint {path} does not contain a model state")
    return dict(state), meta, raw


def _model_kwargs(metadata: Mapping[str, Any], state: Mapping[str, torch.Tensor]) -> dict[str, Any]:
    training_config = metadata.get("training_config", {}) if isinstance(metadata, Mapping) else {}
    model_cfg = training_config.get("model", {}) if isinstance(training_config, Mapping) else {}
    manifest = metadata.get("model_manifest", {}) if isinstance(metadata, Mapping) else {}
    cfg = {**(model_cfg if isinstance(model_cfg, Mapping) else {}), **(manifest if isinstance(manifest, Mapping) else {})}
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


def _assert_single_head(state: Mapping[str, torch.Tensor], label: str) -> None:
    if not any(k.startswith("logit_head.") for k in state):
        raise ValueError(f"{label} does not look like a legacy single-head checkpoint")
    if not any(k.startswith("value_head.") for k in state):
        raise ValueError(f"{label} is missing value_head parameters")


def build_shared_ab_checkpoint(
    deck_a_checkpoint: str | Path,
    deck_b_checkpoint: str | Path,
    output: str | Path,
    *,
    backbone: str = "b",
) -> Path:
    """Combine two schema-compatible single-head policies into gwent_v2.

    Shared representation weights come from ``backbone`` (default: B).  A's
    final policy/value heads are copied into A heads and B's into B heads.
    No parameter averaging is performed.
    """
    a_state, a_meta, _ = _payload(deck_a_checkpoint)
    b_state, b_meta, _ = _payload(deck_b_checkpoint)
    _assert_single_head(a_state, "deck A")
    _assert_single_head(b_state, "deck B")

    if int(a_meta.get("schema_version", -1)) != int(b_meta.get("schema_version", -2)):
        raise ValueError("deck A and B checkpoints use different RL schemas")
    if a_meta.get("action_grammar_version") != b_meta.get("action_grammar_version"):
        raise ValueError("deck A and B checkpoints use different action grammars")
    if a_meta.get("prefix_semantics") != b_meta.get("prefix_semantics"):
        raise ValueError("deck A and B checkpoints use different prefix semantics")

    backbone = str(backbone).strip().lower()
    if backbone not in {"a", "b"}:
        raise ValueError("backbone must be 'a' or 'b'")
    backbone_state = a_state if backbone == "a" else b_state
    backbone_meta = a_meta if backbone == "a" else b_meta
    kwargs = _model_kwargs(backbone_meta, backbone_state)
    model = SharedDeckHeadsPolicyValueNet(**kwargs)

    text_weight = backbone_state.get("text_embedding.weight")
    text_lookup = backbone_state.get("text_card_id_lookup")
    if text_weight is not None and getattr(text_weight, "ndim", 0) == 2:
        lookup_size = int(text_lookup.shape[0]) if text_lookup is not None else 1
        model._init_text_embedding_modules(text_weight.shape[0], text_weight.shape[1], lookup_size)

    target = model.state_dict()
    copied_backbone = 0
    for key, value in backbone_state.items():
        if key.startswith("logit_head.") or key.startswith("value_head."):
            continue
        if key in target and target[key].shape == value.shape:
            target[key] = value.detach().clone()
            copied_backbone += 1

    copied_heads = {"a": 0, "b": 0}
    for deck, source in (("a", a_state), ("b", b_state)):
        for old_prefix, new_prefix in (("logit_head.", f"logit_heads.{deck}."), ("value_head.", f"value_heads.{deck}.")):
            for key, value in source.items():
                if not key.startswith(old_prefix):
                    continue
                new_key = new_prefix + key[len(old_prefix):]
                if new_key not in target or target[new_key].shape != value.shape:
                    raise ValueError(f"cannot map {key} -> {new_key}: incompatible shape")
                target[new_key] = value.detach().clone()
                copied_heads[deck] += 1

    model.load_state_dict(target, strict=True)

    metadata = copy.deepcopy(dict(backbone_meta))
    training_config = copy.deepcopy(metadata.get("training_config", {}))
    if not isinstance(training_config, dict):
        training_config = {}
    model_cfg = copy.deepcopy(training_config.get("model", {}))
    if not isinstance(model_cfg, dict):
        model_cfg = {}
    model_cfg["architecture"] = SharedDeckHeadsPolicyValueNet.ARCHITECTURE
    training_config["model"] = model_cfg
    metadata["training_config"] = training_config
    metadata["model_manifest"] = model.model_manifest()
    metadata["update"] = 0
    metadata["total_decisions"] = 0
    metadata["total_games"] = 0
    metadata["created_unix"] = time.time()
    metadata["architecture_version"] = SharedDeckHeadsPolicyValueNet.ARCHITECTURE
    metadata["shared_ab_initialization"] = {
        "backbone_source": backbone,
        "deck_a_checkpoint": str(Path(deck_a_checkpoint)),
        "deck_b_checkpoint": str(Path(deck_b_checkpoint)),
        "deck_a_sha256": _sha256(deck_a_checkpoint),
        "deck_b_sha256": _sha256(deck_b_checkpoint),
        "deck_a_source_update": int(a_meta.get("update", 0)),
        "deck_b_source_update": int(b_meta.get("update", 0)),
        "copied_backbone_keys": copied_backbone,
        "copied_a_head_keys": copied_heads["a"],
        "copied_b_head_keys": copied_heads["b"],
        "note": "A head was trained with A's original backbone; run A-head adaptation before judging strength if backbone source is B.",
    }

    out = Path(output)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".tmp")
    torch.save({"model": model.state_dict(), "metadata": metadata}, tmp)
    tmp.replace(out)
    return out


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Build gwent_v2 shared-backbone A/B-head checkpoint")
    parser.add_argument("--deck-a", required=True)
    parser.add_argument("--deck-b", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--backbone", choices=["a", "b"], default="b")
    args = parser.parse_args(argv)
    out = build_shared_ab_checkpoint(args.deck_a, args.deck_b, args.output, backbone=args.backbone)
    print(f"[GWENT_V2] wrote {out}")


if __name__ == "__main__":
    main()
