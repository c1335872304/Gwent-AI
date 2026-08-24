from __future__ import annotations

import csv
import json
import platform
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Mapping

import torch

from . import _ctypes as abi
from .config import ExperimentConfig, config_to_dict, save_experiment_config
from .device import resolve_torch_device
from .policy import (
    CandidatePolicyValueNet,
    SharedDeckHeadsPolicyValueNet,
    SharedDeckPrivatePolicyValueNet,
)


@dataclass(frozen=True)
class ExperimentPaths:
    run_dir: Path
    checkpoints_dir: Path
    failures_dir: Path
    metrics_csv: Path
    eval_jsonl: Path
    config_json: Path


def create_run_dir(run_dir: str | Path, config: ExperimentConfig) -> ExperimentPaths:
    run = Path(run_dir)
    paths = ExperimentPaths(
        run_dir=run,
        checkpoints_dir=run / "checkpoints",
        failures_dir=run / "replay_failures",
        metrics_csv=run / "metrics.csv",
        eval_jsonl=run / "eval.jsonl",
        config_json=run / "config.resolved.json",
    )
    paths.checkpoints_dir.mkdir(parents=True, exist_ok=True)
    paths.failures_dir.mkdir(parents=True, exist_ok=True)
    save_experiment_config(config, paths.config_json)
    return paths


class CsvMetricLogger:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._fieldnames: list[str] | None = None

    def write(self, row: Mapping[str, Any]) -> None:
        flat = {k: _json_safe(v) for k, v in row.items()}
        if self._fieldnames is None:
            self._fieldnames = list(flat.keys())
            with self.path.open("w", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=self._fieldnames)
                writer.writeheader()
                writer.writerow(flat)
        else:
            for key in flat:
                if key not in self._fieldnames:
                    # Rewrite with a widened header; this keeps the logger robust
                    # when later updates add metrics.
                    old_rows: list[dict[str, Any]] = []
                    if self.path.exists():
                        with self.path.open("r", newline="", encoding="utf-8") as f:
                            old_rows = list(csv.DictReader(f))
                    self._fieldnames.append(key)
                    with self.path.open("w", newline="", encoding="utf-8") as f:
                        writer = csv.DictWriter(f, fieldnames=self._fieldnames)
                        writer.writeheader()
                        for old in old_rows:
                            writer.writerow(old)
                    break
            with self.path.open("a", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=self._fieldnames)
                writer.writerow({k: flat.get(k, "") for k in self._fieldnames})


class JsonlLogger:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)

    def write(self, row: Mapping[str, Any]) -> None:
        with self.path.open("a", encoding="utf-8") as f:
            f.write(json.dumps({k: _json_safe(v) for k, v in row.items()}, sort_keys=True) + "\n")


def _json_safe(value: Any) -> Any:
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    if hasattr(value, "item"):
        return value.item()
    if isinstance(value, Path):
        return str(value)
    return value


def checkpoint_metadata(
    *,
    config: ExperimentConfig,
    update: int,
    total_decisions: int,
    extra: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    meta = {
        "schema_version": abi.GWENT_RL_SCHEMA_VERSION,
        "reward_config_version": abi.GWENT_RL_REWARD_CONFIG_VERSION,
        "action_grammar_version": abi.GWENT_RL_ACTION_GRAMMAR_VERSION,
        "prefix_semantics": abi.GWENT_RL_PREFIX_SEMANTICS,
        "reward_mode": config.collector.reward_mode,
        "training_config": config_to_dict(config),
        "update": int(update),
        "total_decisions": int(total_decisions),
        "created_unix": time.time(),
        "python_version": platform.python_version(),
        "torch_version": str(torch.__version__),
        "abi_dimensions": {
            "global_feature_count": abi.GWENT_RL_GLOBAL_FEATURE_COUNT,
            "max_objects": abi.GWENT_RL_MAX_OBJECTS,
            "object_feature_count": abi.GWENT_RL_OBJECT_FEATURE_COUNT,
            "max_options": abi.GWENT_RL_MAX_OPTIONS,
            "option_feature_count": abi.GWENT_RL_OPTION_FEATURE_COUNT,
            "max_prefix": abi.GWENT_RL_MAX_PREFIX,
        },
    }
    if extra:
        meta.update(dict(extra))
    return meta


def save_checkpoint(
    path: str | Path,
    policy: CandidatePolicyValueNet,
    optimizer: torch.optim.Optimizer,
    *,
    config: ExperimentConfig,
    update: int,
    total_decisions: int,
    extra: Mapping[str, Any] | None = None,
) -> Path:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "model": policy.state_dict(),
        "optimizer": optimizer.state_dict(),
        "metadata": checkpoint_metadata(config=config, update=update, total_decisions=total_decisions, extra={**(extra or {}), "model_manifest": policy.model_manifest()}),
    }
    tmp = p.with_suffix(p.suffix + ".tmp")
    torch.save(payload, tmp)
    tmp.replace(p)
    return p



def _validate_checkpoint_schema(metadata: Mapping[str, Any], path: str | Path) -> None:
    saved = metadata.get("schema_version")
    if saved is None:
        return
    try:
        saved_version = int(saved)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"checkpoint {path} has invalid schema_version={saved!r}") from exc
    if saved_version != abi.GWENT_RL_SCHEMA_VERSION:
        raise ValueError(
            f"checkpoint {path} uses RL schema v{saved_version}, but this build uses "
            f"v{abi.GWENT_RL_SCHEMA_VERSION}; observation/model semantics are not compatible"
        )

    saved_grammar = metadata.get("action_grammar_version")
    try:
        saved_grammar_version = int(saved_grammar)
    except (TypeError, ValueError) as exc:
        raise ValueError(
            f"checkpoint {path} has invalid action_grammar_version={saved_grammar!r}"
        ) from exc
    if saved_grammar_version != abi.GWENT_RL_ACTION_GRAMMAR_VERSION:
        raise ValueError(
            f"checkpoint {path} action_grammar_version={saved_grammar_version}, expected "
            f"{abi.GWENT_RL_ACTION_GRAMMAR_VERSION}"
        )
    saved_prefix = metadata.get("prefix_semantics")
    if saved_prefix != abi.GWENT_RL_PREFIX_SEMANTICS:
        raise ValueError(
            f"checkpoint {path} prefix_semantics={saved_prefix!r}, expected "
            f"{abi.GWENT_RL_PREFIX_SEMANTICS!r}"
        )


def policy_from_checkpoint(path: str | Path, device: str | torch.device = "auto") -> tuple[CandidatePolicyValueNet, dict[str, Any]]:
    """Create a policy with the architecture saved in checkpoint metadata."""
    device = resolve_torch_device(device)
    payload = torch.load(path, map_location="cpu", weights_only=False)
    metadata = dict(payload.get("metadata", {})) if isinstance(payload, Mapping) else {}
    _validate_checkpoint_schema(metadata, path)
    training_config = metadata.get("training_config", {}) if isinstance(metadata, Mapping) else {}
    model_cfg = training_config.get("model", {}) if isinstance(training_config, Mapping) else {}
    manifest = metadata.get("model_manifest", {}) if isinstance(metadata, Mapping) else {}
    if isinstance(manifest, Mapping):
        model_cfg = {**model_cfg, **manifest}
    state = payload.get("model", payload) if isinstance(payload, Mapping) else payload
    text_weight = state.get("text_embedding.weight") if isinstance(state, Mapping) else None
    text_lookup = state.get("text_card_id_lookup") if isinstance(state, Mapping) else None
    text_dim = int(model_cfg.get("text_embedding_dim", 0))
    if text_weight is not None and getattr(text_weight, "ndim", 0) == 2:
        text_dim = int(text_weight.shape[1])
    architecture = str(model_cfg.get("architecture", "single_head")).strip().lower()
    if architecture == SharedDeckPrivatePolicyValueNet.ARCHITECTURE:
        policy_cls = SharedDeckPrivatePolicyValueNet
    elif architecture == SharedDeckHeadsPolicyValueNet.ARCHITECTURE:
        policy_cls = SharedDeckHeadsPolicyValueNet
    elif architecture in {"", "single_head"}:
        policy_cls = CandidatePolicyValueNet
    else:
        raise ValueError(f"checkpoint {path} uses unsupported model architecture {architecture!r}")
    policy = policy_cls(
        hidden_dim=int(model_cfg.get("hidden_dim", 128)),
        num_attention_heads=int(model_cfg.get("num_attention_heads", 4)),
        attention_layers=int(model_cfg.get("attention_layers", 1)),
        dropout=float(model_cfg.get("dropout", 0.0)),
        card_vocab_size=int(model_cfg.get("card_vocab_size", 4096)),
        text_embedding_dim=text_dim,
        freeze_text_embeddings=bool(model_cfg.get("freeze_text_embeddings", True)),
        use_candidate_object_attention=bool(model_cfg.get("use_candidate_object_attention", True)),
        use_candidate_self_attention=bool(model_cfg.get("use_candidate_self_attention", True)),
    ).to(device)
    if text_weight is not None and getattr(text_weight, "ndim", 0) == 2:
        lookup_size = int(text_lookup.shape[0]) if text_lookup is not None else 1
        policy._init_text_embedding_modules(text_weight.shape[0], text_weight.shape[1], lookup_size)
    policy.load_state_dict(state)
    policy.to(device)
    policy.eval()
    return policy, metadata

def load_checkpoint_model(path: str | Path, policy: CandidatePolicyValueNet, optimizer: torch.optim.Optimizer | None = None) -> dict[str, Any]:
    payload = torch.load(path, map_location="cpu", weights_only=False)
    metadata = dict(payload.get("metadata", {})) if isinstance(payload, Mapping) else {}
    _validate_checkpoint_schema(metadata, path)
    state = payload.get("model", payload)
    policy.load_state_dict(state)
    if optimizer is not None and isinstance(payload, Mapping) and "optimizer" in payload:
        optimizer.load_state_dict(payload["optimizer"])
    return metadata
