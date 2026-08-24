from __future__ import annotations

import hashlib
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Mapping

import torch

from gwent_rl.policy import CandidatePolicyValueNet
from gwent_rl.schema import ACTION_GRAMMAR_VERSION, DECISION_KIND_NAMES, OPTION_KIND_NAMES, PREFIX_SEMANTICS, SCHEMA_VERSION
from gwent_rl.training.migrations import get_migration_spec




@dataclass
class WarmStartReport:
    """一次 warm start 的可审计报告。"""

    checkpoint: str
    sha256: str
    source_schema: int
    source_action_grammar: int
    target_schema: int
    target_action_grammar: int
    migration_id: str
    source_update: int
    source_total_games: int
    copied_parameter_keys: int
    target_initialized_keys: list[str]
    reset_decision_embeddings: list[str]
    reset_option_embeddings: list[str]
    optimizer_reset: bool = True

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def _name_to_id(table: Mapping[int, str], name: str, kind: str) -> int:
    normalized = str(name).strip().lower()
    for idx, value in table.items():
        if value.lower() == normalized:
            return int(idx)
    raise ValueError(f"unknown {kind} embedding name: {name!r}")


def checkpoint_sha256(path: str | Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_checkpoint(path: str | Path) -> dict[str, Any]:
    """只读取 checkpoint 元数据，供 Planner/Agent 做启动前检查。"""

    payload = torch.load(path, map_location="cpu", weights_only=False)
    if not isinstance(payload, Mapping):
        raise ValueError(f"checkpoint {path} is not a structured payload")
    metadata = payload.get("metadata", {})
    if not isinstance(metadata, Mapping):
        raise ValueError(f"checkpoint {path} metadata is invalid")
    return dict(metadata)


def _ordered_unique(*groups: list[str] | tuple[str, ...]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for group in groups:
        for value in group:
            normalized = str(value).strip()
            if normalized and normalized not in seen:
                seen.add(normalized)
                result.append(normalized)
    return result


def warm_start_policy(
    path: str | Path,
    policy: CandidatePolicyValueNet,
    *,
    expected_source_schema: int,
    expected_source_action_grammar: int,
    reset_decision_embeddings: list[str] | None = None,
    reset_option_embeddings: list[str] | None = None,
    expected_sha256: str | None = None,
) -> WarmStartReport:
    """Migrate strategy weights into the current model without optimizer state.

    The loader fails closed.  A non-identical source contract must have an exact
    reviewed migration in ``training.migrations``.  Every target-only parameter,
    projection shape change, and semantic embedding reset must be declared there;
    arbitrary ``strict=False`` loading is intentionally forbidden.
    """

    checkpoint = Path(path)
    actual_sha256 = checkpoint_sha256(checkpoint)
    if expected_sha256:
        expected = expected_sha256.strip().lower()
        if actual_sha256 != expected:
            raise ValueError(
                f"warm-start checkpoint SHA-256 mismatch: actual={actual_sha256}, expected={expected}"
            )

    payload = torch.load(checkpoint, map_location="cpu", weights_only=False)
    if not isinstance(payload, Mapping):
        raise ValueError(f"checkpoint {checkpoint} is not a structured payload")
    metadata = payload.get("metadata", {})
    if not isinstance(metadata, Mapping):
        raise ValueError(f"checkpoint {checkpoint} metadata is invalid")

    source_schema = int(metadata.get("schema_version", -1))
    source_grammar = int(metadata.get("action_grammar_version", -1))
    source_prefix = metadata.get("prefix_semantics")
    if source_schema != int(expected_source_schema):
        raise ValueError(
            f"warm-start checkpoint schema={source_schema}, task expects {expected_source_schema}"
        )
    if source_grammar != int(expected_source_action_grammar):
        raise ValueError(
            f"warm-start checkpoint grammar={source_grammar}, task expects {expected_source_action_grammar}"
        )
    if source_prefix != PREFIX_SEMANTICS:
        raise ValueError(
            f"warm-start checkpoint prefix_semantics={source_prefix!r}, expected {PREFIX_SEMANTICS!r}"
        )

    exact_contract = source_schema == SCHEMA_VERSION and source_grammar == ACTION_GRAMMAR_VERSION
    migration = None
    if not exact_contract:
        migration = get_migration_spec(
            source_schema,
            source_grammar,
            SCHEMA_VERSION,
            ACTION_GRAMMAR_VERSION,
        )
        if migration is None:
            raise ValueError(
                "warm-start contract migration not allowed: "
                f"schema {source_schema}/grammar {source_grammar} -> "
                f"schema {SCHEMA_VERSION}/grammar {ACTION_GRAMMAR_VERSION}"
            )

    state = payload.get("model")
    if not isinstance(state, Mapping):
        raise ValueError(f"checkpoint {checkpoint} does not contain model state")

    required_decisions = migration.required_reset_decision_embeddings if migration else ()
    required_options = migration.required_reset_option_embeddings if migration else ()
    decision_names = _ordered_unique(required_decisions, list(reset_decision_embeddings or []))
    option_names = _ordered_unique(required_options, list(reset_option_embeddings or []))

    # Capture fresh rows before any source weights are copied.  This is important
    # when the table shape itself is unchanged but a previously unused row gains
    # a new semantic meaning in the target action grammar.
    decision_rows: dict[tuple[str, int], torch.Tensor] = {}
    for name in decision_names:
        idx = _name_to_id(DECISION_KIND_NAMES, name, "decision")
        if hasattr(policy, "decision_embeddings"):
            for deck in ("a", "b"):
                table = policy.decision_embeddings[deck]
                decision_rows[(deck, idx)] = table.weight[idx].detach().clone()
        else:
            decision_rows[("shared", idx)] = policy.decision_embedding.weight[idx].detach().clone()
    option_rows: dict[int, torch.Tensor] = {}
    for name in option_names:
        idx = _name_to_id(OPTION_KIND_NAMES, name, "option")
        option_rows[idx] = policy.option_kind_embedding.weight[idx].detach().clone()

    target_state = policy.state_dict()
    copied_parameter_keys = 0
    target_initialized_keys: list[str] = []

    if exact_contract:
        # Same contract still requires exact network identity.  User-requested
        # semantic resets are applied only after the strict load.
        policy.load_state_dict(state, strict=True)
        copied_parameter_keys = len(target_state)
    else:
        assert migration is not None
        source_keys = set(state)
        target_keys = set(target_state)
        missing = target_keys - source_keys
        extra = source_keys - target_keys
        expected_missing = set(migration.target_only_keys)
        if missing != expected_missing or extra:
            raise ValueError(
                "warm-start migration parameter keys differ from reviewed policy: "
                f"missing={sorted(missing)}, expected_missing={sorted(expected_missing)}, "
                f"extra={sorted(extra)}"
            )

        migrated = {key: value.detach().clone() for key, value in target_state.items()}
        target_initialized_keys.extend(sorted(expected_missing))

        for key, source_value in state.items():
            target_value = migrated[key]
            if key in migration.keep_target_initialized_keys:
                target_initialized_keys.append(key)
                continue
            if source_value.shape == target_value.shape:
                migrated[key] = source_value.detach().clone()
                copied_parameter_keys += 1
                continue
            if (
                key in migration.appended_feature_projections
                and source_value.ndim == 2
                and target_value.ndim == 2
            ):
                if (
                    source_value.shape[0] != target_value.shape[0]
                    or source_value.shape[1] > target_value.shape[1]
                ):
                    raise ValueError(
                        f"warm-start appended projection shape is incompatible for {key}: "
                        f"source={tuple(source_value.shape)}, target={tuple(target_value.shape)}"
                    )
                target_value[:, : source_value.shape[1]].copy_(source_value)
                copied_parameter_keys += 1
                continue
            raise ValueError(
                f"warm-start migration has no reviewed shape rule for {key}: "
                f"source={tuple(source_value.shape)}, target={tuple(target_value.shape)}"
            )
        policy.load_state_dict(migrated, strict=True)

    with torch.no_grad():
        for (deck, idx), row in decision_rows.items():
            if deck == "shared":
                policy.decision_embedding.weight[idx].copy_(row)
            else:
                policy.decision_embeddings[deck].weight[idx].copy_(row)
        for idx, row in option_rows.items():
            policy.option_kind_embedding.weight[idx].copy_(row)

    return WarmStartReport(
        checkpoint=str(checkpoint),
        sha256=actual_sha256,
        source_schema=source_schema,
        source_action_grammar=source_grammar,
        target_schema=SCHEMA_VERSION,
        target_action_grammar=ACTION_GRAMMAR_VERSION,
        migration_id=migration.migration_id if migration else "exact-contract",
        source_update=int(metadata.get("update", 0)),
        source_total_games=int(metadata.get("total_games", 0)),
        copied_parameter_keys=copied_parameter_keys,
        target_initialized_keys=sorted(set(target_initialized_keys)),
        reset_decision_embeddings=decision_names,
        reset_option_embeddings=option_names,
    )
