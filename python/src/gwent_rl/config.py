from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field, fields, is_dataclass
from pathlib import Path
from typing import Any, Mapping


@dataclass
class CollectorConfig:
    num_envs: int = 32
    max_batch_size: int | None = None
    base_seed: int = 0
    reward_mode: str = "terminal"
    enable_invariants: bool = False
    reward_overrides: dict[str, float] = field(default_factory=dict)
    player0_deck: str = "a"
    player1_deck: str = "a"
    matchups: dict[str, dict[str, Any]] = field(default_factory=dict)


@dataclass
class ModelConfig:
    # single_head keeps legacy checkpoints working; shared_deck_heads_v1 is V2;
    # shared_deck_private_v3 keeps low-level semantics shared and gives each deck
    # an approximately 50% private active-path strategy trunk.
    architecture: str = "single_head"
    hidden_dim: int = 128
    num_attention_heads: int = 4
    attention_layers: int = 1
    dropout: float = 0.0
    card_vocab_size: int = 4096
    text_embedding_mode: str = "npz"
    text_embedding_path: str | None = "data/embeddings/supported_cards_text_embeddings_qwen1024.npz"
    text_embedding_dim: int = 1024
    freeze_text_embeddings: bool = True
    use_candidate_object_attention: bool = True
    use_candidate_self_attention: bool = True


@dataclass
class PPOTrainConfig:
    updates: int = 2
    # Optional total complete-game budget. If >0 together with
    # games_per_update, it is the authoritative stopping criterion.
    total_games: int = 0
    # If >0, collect this many complete games per generation and ignore
    # steps_per_update. The policy is frozen for the entire collection phase.
    games_per_update: int = 0
    steps_per_update: int = 256
    learning_rate: float = 3e-4
    # V3 may use two Adam parameter groups.  None falls back to learning_rate.
    shared_learning_rate: float | None = None
    private_learning_rate: float | None = None
    epochs: int = 2
    minibatch_size: int = 128
    gamma: float = 0.99
    gae_lambda: float = 0.95
    clip_ratio: float = 0.2
    value_coef: float = 0.5
    entropy_coef: float = 0.01
    max_grad_norm: float = 0.5
    target_kl: float = 0.03
    value_clip: float = 0.2


@dataclass
class EvalConfig:
    interval: int = 1
    games: int = 64
    num_envs: int = 32
    opponent: str = "random"
    controlled_player: int = 0
    deterministic: bool = True


@dataclass
class ExperimentConfig:
    seed: int = 0
    device: str = "auto"
    run_dir: str = "runs/debug"
    checkpoint_interval: int = 1
    collector: CollectorConfig = field(default_factory=CollectorConfig)
    model: ModelConfig = field(default_factory=ModelConfig)
    ppo: PPOTrainConfig = field(default_factory=PPOTrainConfig)
    eval: EvalConfig = field(default_factory=EvalConfig)


def _strip_inline_comment(raw: str) -> str:
    in_single = False
    in_double = False
    escaped = False
    for i, ch in enumerate(raw):
        if escaped:
            escaped = False
            continue
        if ch == "\\":
            escaped = True
            continue
        if ch == "'" and not in_double:
            in_single = not in_single
            continue
        if ch == '"' and not in_single:
            in_double = not in_double
            continue
        if ch == "#" and not in_single and not in_double:
            return raw[:i].rstrip()
    return raw


def _parse_scalar(value: str) -> Any:
    text = value.strip()
    if text == "":
        return ""
    if text.lower() in {"true", "yes", "on"}:
        return True
    if text.lower() in {"false", "no", "off"}:
        return False
    if text.lower() in {"null", "none"}:
        return None
    if text == "{}":
        return {}
    if text == "[]":
        return []
    if (text.startswith("'") and text.endswith("'")) or (text.startswith('"') and text.endswith('"')):
        return text[1:-1]
    try:
        if any(ch in text for ch in (".", "e", "E")):
            return float(text)
        return int(text)
    except ValueError:
        return text


def _parse_simple_yaml(text: str) -> dict[str, Any]:
    """Parse the tiny YAML subset used by configs/*.yaml without PyYAML."""
    root: dict[str, Any] = {}
    stack: list[tuple[int, dict[str, Any]]] = [(-1, root)]
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        line = _strip_inline_comment(raw).strip()
        if not line:
            continue
        if ":" not in line:
            raise ValueError(f"unsupported config line: {raw!r}")
        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()
        while stack and indent <= stack[-1][0]:
            stack.pop()
        parent = stack[-1][1]
        if value == "":
            child: dict[str, Any] = {}
            parent[key] = child
            stack.append((indent, child))
        else:
            parent[key] = _parse_scalar(value)
    return root


def _deep_update(base: dict[str, Any], update: Mapping[str, Any]) -> dict[str, Any]:
    for key, value in update.items():
        if isinstance(value, Mapping) and isinstance(base.get(key), dict):
            _deep_update(base[key], value)
        else:
            base[key] = value
    return base


def _dataclass_from_dict(cls, data: Mapping[str, Any]):
    kwargs = {}
    known = {f.name: f for f in fields(cls)}
    for key, value in data.items():
        if key not in known:
            raise ValueError(f"unknown config key for {cls.__name__}: {key}")
        f = known[key]
        default_obj = getattr(cls(), key)
        if is_dataclass(default_obj) and isinstance(value, Mapping):
            kwargs[key] = _dataclass_from_dict(type(default_obj), value)
        else:
            kwargs[key] = value
    return cls(**kwargs)


def config_to_dict(config: ExperimentConfig) -> dict[str, Any]:
    return asdict(config)


def load_mapping_file(path: str | Path) -> dict[str, Any]:
    """读取 JSON 或项目使用的简单 YAML，并返回普通字典。

    训练任务编排与 PPO 配置共用同一套轻量解析器，避免为了任务文件
    额外引入 PyYAML。任务配置刻意只使用嵌套 mapping + scalar。
    """
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    loaded = json.loads(text) if p.suffix.lower() == ".json" else _parse_simple_yaml(text)
    if not isinstance(loaded, Mapping):
        raise ValueError(f"config root must be an object: {p}")
    return dict(loaded)


def load_experiment_config(path: str | Path | None = None, overrides: Mapping[str, Any] | None = None) -> ExperimentConfig:
    data: dict[str, Any] = config_to_dict(ExperimentConfig())
    if path:
        _deep_update(data, load_mapping_file(path))
    if overrides:
        _deep_update(data, overrides)
    return _dataclass_from_dict(ExperimentConfig, data)


def save_experiment_config(config: ExperimentConfig, path: str | Path) -> None:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(config_to_dict(config), indent=2, sort_keys=True), encoding="utf-8")
