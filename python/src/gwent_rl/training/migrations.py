"""Explicit warm-start compatibility policy across RL contracts.

A contract bump is never implicitly compatible with old checkpoints.  Every
supported migration is keyed by the complete source/target contract pair
(observation schema + action grammar) and declares exactly which target-only
parameters may stay freshly initialized, which projections may copy an old
prefix of features, and which semantic embedding rows must be reset.

This module deliberately contains policy only; the loader in ``initialization``
executes the policy and fails closed on any undeclared key/shape drift.
"""

from __future__ import annotations

from dataclasses import dataclass


ContractKey = tuple[int, int, int, int]


@dataclass(frozen=True)
class MigrationSpec:
    """Auditable rules for one source->target warm-start path."""

    source_schema: int
    source_action_grammar: int
    target_schema: int
    target_action_grammar: int
    target_only_keys: frozenset[str] = frozenset()
    appended_feature_projections: frozenset[str] = frozenset()
    keep_target_initialized_keys: frozenset[str] = frozenset()
    required_reset_decision_embeddings: tuple[str, ...] = ()
    required_reset_option_embeddings: tuple[str, ...] = ()
    note: str = ""

    @property
    def key(self) -> ContractKey:
        return (
            self.source_schema,
            self.source_action_grammar,
            self.target_schema,
            self.target_action_grammar,
        )

    @property
    def migration_id(self) -> str:
        return (
            f"schema-{self.source_schema}-grammar-{self.source_action_grammar}"
            f"__to__schema-{self.target_schema}-grammar-{self.target_action_grammar}"
        )


_APPEND_V7_TO_V11 = frozenset(
    {
        "global_encoder.0.weight",
        "object_feature_encoder.0.weight",
    }
)
_KEEP_CURRENT_TEXT_TABLE = frozenset({"text_embedding.weight"})
_INSERT_POSITION_TARGET_ONLY = frozenset({"insert_position_embedding.weight"})
_INSERT_POSITION_DECISION_RESET = ("insert_position",)
_INSERT_POSITION_OPTION_RESET = ("choose_insert_position",)
_MULLIGAN_DECISION_RESET = ("mulligan",)
_MULLIGAN_OPTION_RESET = ("mulligan", "keep_hand")


# Direct migrations into the current position-aware contract are intentional.
# The older v6/v7 paths are kept because the repository still registers those
# historical strong checkpoints.  They compose the already-reviewed feature
# growth with the new position embedding without using permissive strict=False.
_SPECS = (
    MigrationSpec(
        source_schema=12,
        source_action_grammar=5,
        target_schema=13,
        target_action_grammar=5,
        note=(
            "Fair-information observation migration. Tensor dimensions and model parameters "
            "are unchanged, but opponent hand identities are removed and public decklists "
            "are exposed as definition-only knowledge objects. Reuse weights only as a "
            "warm start; optimizer/scheduler state must be reset and adaptation training is required."
        ),
    ),
    MigrationSpec(
        source_schema=11,
        source_action_grammar=4,
        target_schema=13,
        target_action_grammar=5,
        target_only_keys=_INSERT_POSITION_TARGET_ONLY,
        required_reset_decision_embeddings=_INSERT_POSITION_DECISION_RESET,
        required_reset_option_embeddings=_INSERT_POSITION_OPTION_RESET,
        note=(
            "Adds dynamic row insertion as a model input/action semantic. "
            "Preserve all compatible strategy weights; initialize the new position "
            "embedding and reset only the newly meaningful decision/option rows."
        ),
    ),
    MigrationSpec(
        source_schema=7,
        source_action_grammar=3,
        target_schema=13,
        target_action_grammar=5,
        target_only_keys=_INSERT_POSITION_TARGET_ONLY,
        appended_feature_projections=_APPEND_V7_TO_V11,
        keep_target_initialized_keys=_KEEP_CURRENT_TEXT_TABLE,
        required_reset_decision_embeddings=_INSERT_POSITION_DECISION_RESET,
        required_reset_option_embeddings=_INSERT_POSITION_OPTION_RESET,
        note="Direct migration from the registered v7 rulefix model into the current contract.",
    ),
    MigrationSpec(
        source_schema=6,
        source_action_grammar=3,
        target_schema=13,
        target_action_grammar=5,
        target_only_keys=_INSERT_POSITION_TARGET_ONLY,
        appended_feature_projections=_APPEND_V7_TO_V11,
        keep_target_initialized_keys=_KEEP_CURRENT_TEXT_TABLE,
        required_reset_decision_embeddings=_INSERT_POSITION_DECISION_RESET,
        required_reset_option_embeddings=_INSERT_POSITION_OPTION_RESET,
        note="Direct migration from a post-Mulligan schema-v6 model into the current contract.",
    ),
    MigrationSpec(
        source_schema=6,
        source_action_grammar=2,
        target_schema=13,
        target_action_grammar=5,
        target_only_keys=_INSERT_POSITION_TARGET_ONLY,
        appended_feature_projections=_APPEND_V7_TO_V11,
        keep_target_initialized_keys=_KEEP_CURRENT_TEXT_TABLE,
        required_reset_decision_embeddings=_MULLIGAN_DECISION_RESET + _INSERT_POSITION_DECISION_RESET,
        required_reset_option_embeddings=_MULLIGAN_OPTION_RESET + _INSERT_POSITION_OPTION_RESET,
        note=(
            "Direct migration from the registered pre-Mulligan model.  Both Mulligan and "
            "dynamic insertion semantics are initialized from the current target model."
        ),
    ),
)

MIGRATION_SPECS: dict[ContractKey, MigrationSpec] = {spec.key: spec for spec in _SPECS}


def get_migration_spec(
    source_schema: int,
    source_action_grammar: int,
    target_schema: int,
    target_action_grammar: int,
) -> MigrationSpec | None:
    """Return the exact reviewed migration, or ``None`` if unsupported."""

    return MIGRATION_SPECS.get(
        (
            int(source_schema),
            int(source_action_grammar),
            int(target_schema),
            int(target_action_grammar),
        )
    )


def is_migration_allowed(
    source_schema: int,
    source_action_grammar: int,
    target_schema: int,
    target_action_grammar: int,
) -> bool:
    return get_migration_spec(
        source_schema,
        source_action_grammar,
        target_schema,
        target_action_grammar,
    ) is not None


# Backwards-compatible summaries for tooling that only displays schema-level
# information.  Do not use these to authorize a load; authorization must use the
# full contract key above.
ALLOWED_SCHEMA_MIGRATIONS: frozenset[tuple[int, int]] = frozenset(
    (spec.source_schema, spec.target_schema) for spec in _SPECS
)
APPENDED_FEATURE_PROJECTIONS: dict[tuple[int, int], frozenset[str]] = {
    (spec.source_schema, spec.target_schema): spec.appended_feature_projections
    for spec in _SPECS
    if spec.appended_feature_projections
}
KEEP_TARGET_INITIALIZED_KEYS: dict[tuple[int, int], frozenset[str]] = {
    (spec.source_schema, spec.target_schema): spec.keep_target_initialized_keys
    for spec in _SPECS
    if spec.keep_target_initialized_keys
}
