"""Python wrapper for gwent-cpp-core RL collector.

The package root intentionally keeps imports lightweight.  Card/rules tooling and
local manual-test servers need schema/ctypes helpers but should not be forced to
import PyTorch.  Training/inference symbols remain available from ``gwent_rl``
through lazy attribute loading and are imported only when first used.
"""

from __future__ import annotations

from importlib import import_module
from typing import Any

from .schema import (
    ACTION_GRAMMAR_VERSION,
    GLOBAL_FEATURE_NAMES,
    OBJECT_FEATURE_NAMES,
    OPTION_FEATURE_NAMES,
    PREFIX_SEMANTICS,
    SCHEMA_VERSION,
)

# Public root-level API kept backward-compatible with the previous eager imports.
_LAZY_EXPORTS: dict[str, tuple[str, str]] = {
    "explain_batch_row": (".explain", "explain_batch_row"),
    "CollectorConfig": (".config", "CollectorConfig"),
    "EvalConfig": (".config", "EvalConfig"),
    "ExperimentConfig": (".config", "ExperimentConfig"),
    "ModelConfig": (".config", "ModelConfig"),
    "PPOTrainConfig": (".config", "PPOTrainConfig"),
    "config_to_dict": (".config", "config_to_dict"),
    "load_experiment_config": (".config", "load_experiment_config"),
    "save_experiment_config": (".config", "save_experiment_config"),
    "CsvMetricLogger": (".experiment", "CsvMetricLogger"),
    "JsonlLogger": (".experiment", "JsonlLogger"),
    "create_run_dir": (".experiment", "create_run_dir"),
    "load_checkpoint_model": (".experiment", "load_checkpoint_model"),
    "policy_from_checkpoint": (".experiment", "policy_from_checkpoint"),
    "save_checkpoint": (".experiment", "save_checkpoint"),
    "REWARD_MODE_NAMES": (".collector", "REWARD_MODE_NAMES"),
    "BatchActions": (".collector", "BatchActions"),
    "InferenceBatch": (".collector", "InferenceBatch"),
    "RlCollector": (".collector", "RlCollector"),
    "load_library": (".collector", "load_library"),
    "random_legal_actions": (".collector", "random_legal_actions"),
    "CandidatePolicyValueNet": (".policy", "CandidatePolicyValueNet"),
    "SharedDeckHeadsPolicyValueNet": (".policy", "SharedDeckHeadsPolicyValueNet"),
    "SharedDeckPrivatePolicyValueNet": (".policy", "SharedDeckPrivatePolicyValueNet"),
    "MeanPoolCandidatePolicyValueNet": (".policy", "MeanPoolCandidatePolicyValueNet"),
    "masked_logits": (".policy", "masked_logits"),
    "sample_actions": (".policy", "sample_actions"),
    "greedy_actions": (".policy", "greedy_actions"),
    "device_name": (".device", "device_name"),
    "resolve_torch_device": (".device", "resolve_torch_device"),
    "TextEmbeddingTable": (".text_embeddings", "TextEmbeddingTable"),
    "make_hash_embedding_table": (".text_embeddings", "make_hash_embedding_table"),
    "load_text_embedding_table": (".text_embeddings", "load_text_embedding_table"),
    "resolve_text_embedding_table": (".text_embeddings", "resolve_text_embedding_table"),
    "RolloutBuffer": (".rollout_buffer", "RolloutBuffer"),
    "LearnerVsFrozenRunner": (".runner", "LearnerVsFrozenRunner"),
    "RolloutRunner": (".runner", "RolloutRunner"),
    "PPOConfig": (".ppo", "PPOConfig"),
    "ppo_update": (".ppo", "ppo_update"),
    "analyze_rows": (".benchmark_analysis", "analyze_rows"),
    "load_benchmark_rows": (".benchmark_analysis", "load_benchmark_rows"),
    "run_analysis": (".benchmark_analysis", "run_analysis"),
    "stage_breakdown": (".benchmark_analysis", "stage_breakdown"),
    "write_markdown_report": (".benchmark_analysis", "write_markdown_report"),
    "write_recommended_configs": (".benchmark_analysis", "write_recommended_configs"),
}


def __getattr__(name: str) -> Any:
    target = _LAZY_EXPORTS.get(name)
    if target is None:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    module_name, attribute_name = target
    value = getattr(import_module(module_name, __name__), attribute_name)
    globals()[name] = value
    return value


def __dir__() -> list[str]:
    return sorted(set(globals()) | set(_LAZY_EXPORTS))


__all__ = [
    "SCHEMA_VERSION",
    "ACTION_GRAMMAR_VERSION",
    "PREFIX_SEMANTICS",
    "GLOBAL_FEATURE_NAMES",
    "OBJECT_FEATURE_NAMES",
    "OPTION_FEATURE_NAMES",
    *_LAZY_EXPORTS.keys(),
]
