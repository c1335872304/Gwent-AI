from __future__ import annotations

import argparse
import shutil
import time
from pathlib import Path
from typing import Any

import torch

from .collector import RlCollector
from .config import ExperimentConfig, load_experiment_config
from .device import device_name, resolve_torch_device
from .eval_matchup import MatchupStats, allocate_weighted_games, evaluate_matchup_once, evaluate_policies
from .experiment import (
    CsvMetricLogger,
    JsonlLogger,
    create_run_dir,
    load_checkpoint_model,
    policy_from_checkpoint,
    save_checkpoint,
)
from .policy import (
    CandidatePolicyValueNet,
    SharedDeckHeadsPolicyValueNet,
    SharedDeckPrivatePolicyValueNet,
)
from .training.initialization import checkpoint_sha256, warm_start_policy
from .ppo import PPOConfig, ppo_update
from .runner import LearnerVsFrozenRunner, RolloutRunner
from .text_embeddings import resolve_text_embedding_table


def _override_config(config: ExperimentConfig, args: argparse.Namespace) -> ExperimentConfig:
    if args.num_envs is not None:
        config.collector.num_envs = args.num_envs
        config.collector.max_batch_size = args.num_envs if args.max_batch_size is None else args.max_batch_size
        config.eval.num_envs = min(args.num_envs, config.eval.num_envs)
    if args.max_batch_size is not None:
        config.collector.max_batch_size = args.max_batch_size
    if args.steps_per_update is not None:
        config.ppo.steps_per_update = args.steps_per_update
    if args.games_per_update is not None:
        config.ppo.games_per_update = args.games_per_update
    if args.updates is not None:
        config.ppo.updates = args.updates
    if args.total_games is not None:
        config.ppo.total_games = args.total_games
    if getattr(args, "model_architecture", None) is not None:
        config.model.architecture = args.model_architecture
    if args.hidden_dim is not None:
        config.model.hidden_dim = args.hidden_dim
    if args.num_attention_heads is not None:
        config.model.num_attention_heads = args.num_attention_heads
    if args.attention_layers is not None:
        config.model.attention_layers = args.attention_layers
    if args.dropout is not None:
        config.model.dropout = args.dropout
    if args.text_embedding_mode is not None:
        config.model.text_embedding_mode = args.text_embedding_mode
    if args.text_embedding_path is not None:
        config.model.text_embedding_path = args.text_embedding_path
    if args.text_embedding_dim is not None:
        config.model.text_embedding_dim = args.text_embedding_dim
    if args.freeze_text_embeddings:
        config.model.freeze_text_embeddings = True
    if args.train_text_embeddings:
        config.model.freeze_text_embeddings = False
    if args.disable_candidate_object_attention:
        config.model.use_candidate_object_attention = False
    if args.disable_candidate_self_attention:
        config.model.use_candidate_self_attention = False
    if args.learning_rate is not None:
        config.ppo.learning_rate = args.learning_rate
    if getattr(args, "shared_learning_rate", None) is not None:
        config.ppo.shared_learning_rate = args.shared_learning_rate
    if getattr(args, "private_learning_rate", None) is not None:
        config.ppo.private_learning_rate = args.private_learning_rate
    if args.epochs is not None:
        config.ppo.epochs = args.epochs
    if args.minibatch_size is not None:
        config.ppo.minibatch_size = args.minibatch_size
    if args.gamma is not None:
        config.ppo.gamma = args.gamma
    if args.gae_lambda is not None:
        config.ppo.gae_lambda = args.gae_lambda
    if args.reward_mode is not None:
        config.collector.reward_mode = args.reward_mode
    if args.seed is not None:
        config.seed = args.seed
        config.collector.base_seed = args.seed
    if args.device is not None:
        config.device = args.device
    if args.run_dir is not None:
        config.run_dir = args.run_dir
    if args.checkpoint_interval is not None:
        config.checkpoint_interval = args.checkpoint_interval
    if args.eval_interval is not None:
        config.eval.interval = args.eval_interval
    if args.eval_games is not None:
        config.eval.games = args.eval_games
    if args.eval_num_envs is not None:
        config.eval.num_envs = args.eval_num_envs
    if args.enable_invariants:
        config.collector.enable_invariants = True
    return config


def build_policy_from_config(config: ExperimentConfig, device: torch.device) -> CandidatePolicyValueNet:
    text_table = resolve_text_embedding_table(
        config.model.text_embedding_mode,
        config.model.text_embedding_path,
        config.model.text_embedding_dim,
    )
    text_dim = int(text_table.dim) if text_table is not None else 0
    architecture = str(getattr(config.model, "architecture", "single_head")).strip().lower()
    if architecture == SharedDeckPrivatePolicyValueNet.ARCHITECTURE:
        policy_cls = SharedDeckPrivatePolicyValueNet
    elif architecture == SharedDeckHeadsPolicyValueNet.ARCHITECTURE:
        policy_cls = SharedDeckHeadsPolicyValueNet
    elif architecture == "single_head":
        policy_cls = CandidatePolicyValueNet
    else:
        raise ValueError(f"unsupported model architecture: {architecture!r}")
    policy = policy_cls(
        hidden_dim=config.model.hidden_dim,
        num_attention_heads=config.model.num_attention_heads,
        attention_layers=config.model.attention_layers,
        dropout=config.model.dropout,
        card_vocab_size=config.model.card_vocab_size,
        text_embedding_dim=text_dim,
        freeze_text_embeddings=config.model.freeze_text_embeddings,
        use_candidate_object_attention=config.model.use_candidate_object_attention,
        use_candidate_self_attention=config.model.use_candidate_self_attention,
    )
    if text_table is not None:
        policy.set_text_embedding_table(text_table, freeze=config.model.freeze_text_embeddings)
    return policy.to(device)


def _ppo_config(c: ExperimentConfig) -> PPOConfig:
    return PPOConfig(
        learning_rate=c.ppo.learning_rate,
        epochs=c.ppo.epochs,
        minibatch_size=c.ppo.minibatch_size,
        gamma=c.ppo.gamma,
        gae_lambda=c.ppo.gae_lambda,
        clip_ratio=c.ppo.clip_ratio,
        value_coef=c.ppo.value_coef,
        entropy_coef=c.ppo.entropy_coef,
        max_grad_norm=c.ppo.max_grad_norm,
        target_kl=c.ppo.target_kl,
        value_clip=c.ppo.value_clip,
    )


def _normalize_deck_id(value: str) -> str:
    deck = str(value).strip().lower()
    if deck in {"a", "deck_a"}:
        return "a"
    if deck in {"b", "deck_b"}:
        return "b"
    raise ValueError(f"unsupported deck id for shared best tracking: {value!r}")


def _shared_best_kind(train_scope: str) -> str:
    scope = str(train_scope).strip().lower()
    if scope in {"a_head_only", "a_private_only"}:
        return "a"
    if scope in {"b_head_only", "b_private_only"}:
        return "b"
    if scope in {"joint", "shared_only"}:
        return "joint"
    raise ValueError(f"unsupported shared train scope: {train_scope!r}")


def _score_rate(stats: MatchupStats) -> float:
    games = max(int(stats.completed_episodes), 1)
    return (float(stats.a_wins) + 0.5 * float(stats.draws)) / games


def _joint_promotion_allowed(
    *,
    joint_score: float,
    a_score: float,
    b_score: float,
    illegal: int,
    promote_threshold: float,
    min_deck_score: float,
) -> bool:
    return (
        int(illegal) == 0
        and float(joint_score) >= float(promote_threshold)
        and float(a_score) >= float(min_deck_score)
        and float(b_score) >= float(min_deck_score)
    )


def train(args: argparse.Namespace) -> None:
    config = _override_config(load_experiment_config(args.config), args)
    torch.manual_seed(config.seed)
    device = resolve_torch_device(config.device)
    paths = create_run_dir(config.run_dir, config)
    metrics = CsvMetricLogger(paths.metrics_csv)
    eval_log = JsonlLogger(paths.eval_jsonl)

    policy = build_policy_from_config(config, device)
    ppo_config = _ppo_config(config)

    train_scope = str(getattr(args, "shared_train_scope", "joint")).strip().lower()
    is_shared_policy = isinstance(policy, SharedDeckHeadsPolicyValueNet)
    if train_scope != "joint" and not is_shared_policy:
        raise ValueError(
            "--shared-train-scope requires shared_deck_heads_v1 or shared_deck_private_v3"
        )

    if isinstance(policy, SharedDeckPrivatePolicyValueNet):
        policy.set_trainable_scope(train_scope)
        shared_lr = (
            float(config.ppo.shared_learning_rate)
            if config.ppo.shared_learning_rate is not None
            else float(ppo_config.learning_rate)
        )
        private_lr = (
            float(config.ppo.private_learning_rate)
            if config.ppo.private_learning_rate is not None
            else float(ppo_config.learning_rate)
        )
        optimizer = torch.optim.Adam(
            [
                {"params": list(policy.shared_parameters()), "lr": shared_lr, "name": "shared"},
                {
                    "params": list(policy.private_parameters("a")) + list(policy.private_parameters("b")),
                    "lr": private_lr,
                    "name": "private",
                },
            ],
            lr=ppo_config.learning_rate,
        )
        partition = policy.parameter_partition_report(trainable_only=True)
        print(
            f"[V3] active_split A={partition['a_shared_fraction']:.3f}/"
            f"{partition['a_private_fraction']:.3f} "
            f"B={partition['b_shared_fraction']:.3f}/"
            f"{partition['b_private_fraction']:.3f} "
            f"shared_lr={shared_lr:g} private_lr={private_lr:g}"
        )
    else:
        optimizer = torch.optim.Adam(policy.parameters(), lr=ppo_config.learning_rate)

    initial_path = paths.checkpoints_dir / "initial.pt"
    latest_path = paths.checkpoints_dir / "latest.pt"
    # best.pt is kept as a compatibility alias for older tooling.  Shared V2
    # training uses scope-aware best checkpoints below.
    best_path = paths.checkpoints_dir / "best.pt"
    best_a_path = paths.checkpoints_dir / "best_a.pt"
    best_b_path = paths.checkpoints_dir / "best_b.pt"
    best_joint_path = paths.checkpoints_dir / "best_joint.pt"

    total_decisions = 0
    total_games = 0
    last_completed_update = 0
    initialization_meta: dict[str, Any] = {"mode": "random"}

    if args.resume and args.initialize_from:
        raise ValueError("--resume and --initialize-from are mutually exclusive")

    if args.resume:
        resume_meta = load_checkpoint_model(args.resume, policy, optimizer)
        total_decisions = int(resume_meta.get("total_decisions", 0))
        total_games = int(resume_meta.get("total_games", 0))
        last_completed_update = int(resume_meta.get("update", 0))
        saved_initialization = resume_meta.get("initialization")
        if isinstance(saved_initialization, dict):
            initialization_meta = dict(saved_initialization)
        else:
            initialization_meta = {"mode": "resume", "source": str(args.resume)}
        print(
            f"[RESUME] checkpoint={args.resume} update={last_completed_update} "
            f"games={total_games} decisions={total_decisions}"
        )
    elif args.initialize_from:
        if args.initialize_source_schema is None or args.initialize_source_action_grammar is None:
            raise ValueError(
                "--initialize-from requires --initialize-source-schema and "
                "--initialize-source-action-grammar"
            )
        report = warm_start_policy(
            args.initialize_from,
            policy,
            expected_source_schema=args.initialize_source_schema,
            expected_source_action_grammar=args.initialize_source_action_grammar,
            reset_decision_embeddings=args.reset_decision_embedding,
            reset_option_embeddings=args.reset_option_embedding,
            expected_sha256=args.initialize_checkpoint_sha256,
        )
        initialization_meta = {"mode": "warm_start", **report.to_dict()}
        # Warm start 只迁移策略知识。optimizer、update 和 game counter 全部从当前任务重新开始。
        save_checkpoint(
            initial_path,
            policy,
            optimizer,
            config=config,
            update=0,
            total_decisions=0,
            extra={
                "total_games": 0,
                "rollout_mode": "warm_start_initial",
                "initialization": initialization_meta,
            },
        )
        shutil.copy2(initial_path, best_path)
        print(
            f"[WARM-START] checkpoint={args.initialize_from} "
            f"source_update={report.source_update} source_games={report.source_total_games} "
            f"contract={report.source_schema}/{report.source_action_grammar}"
            f"->{report.target_schema}/{report.target_action_grammar} optimizer=reset"
        )
        print(
            f"[WARM-START] migration={report.migration_id} copied_keys={report.copied_parameter_keys} "
            f"target_initialized={report.target_initialized_keys or 'none'}"
        )
        print(
            f"[WARM-START] reset decision={report.reset_decision_embeddings or 'none'} "
            f"option={report.reset_option_embeddings or 'none'} sha256={report.sha256[:16]}..."
        )
    else:
        save_checkpoint(
            initial_path,
            policy,
            optimizer,
            config=config,
            update=0,
            total_decisions=0,
            extra={"total_games": 0, "rollout_mode": "initial", "initialization": initialization_meta},
        )
        shutil.copy2(initial_path, best_path)

    if not best_path.exists():
        source = initial_path if initial_path.exists() else Path(args.resume)
        shutil.copy2(source, best_path)
        print(f"[BEST] initialized from {source}")

    if is_shared_policy:
        # Crash-safe migration for V2 runs created before scope-aware best files
        # existed.  An existing legacy best.pt wins over a resume checkpoint,
        # which preserves the pre-patch baseline when resuming mid-run.
        shared_best_seed = best_path
        for scoped_path in (best_a_path, best_b_path, best_joint_path):
            if not scoped_path.exists():
                shutil.copy2(shared_best_seed, scoped_path)
                print(f"[BEST-{scoped_path.stem.split('_')[-1].upper()}] initialized from {shared_best_seed.name}")

    if train_scope != "joint":
        policy.set_trainable_scope(train_scope)
        trainable = sum(p.numel() for p in policy.parameters() if p.requires_grad)
        total = sum(p.numel() for p in policy.parameters())
        print(f"[SHARED] train_scope={train_scope} trainable={trainable}/{total}")

    game_based = int(config.ppo.games_per_update) > 0
    total_game_budget = int(config.ppo.total_games)
    if total_game_budget > 0 and not game_based:
        raise ValueError("ppo.total_games requires ppo.games_per_update > 0")

    training_mode = str(args.training_mode).strip().lower()
    frozen_policy = None
    frozen_opponent_meta: dict[str, Any] | None = None
    frozen_opponent_sha256: str | None = None
    if training_mode == "learner_vs_frozen":
        if not game_based:
            raise ValueError("learner_vs_frozen requires complete-game collection (games_per_update > 0)")
        if not args.learner_deck or not args.opponent_deck:
            raise ValueError("learner_vs_frozen requires --learner-deck and --opponent-deck")
        if not args.frozen_opponent_checkpoint:
            raise ValueError("learner_vs_frozen requires --frozen-opponent-checkpoint")
        frozen_path = Path(args.frozen_opponent_checkpoint)
        frozen_opponent_sha256 = checkpoint_sha256(frozen_path)
        if args.frozen_opponent_sha256:
            expected = str(args.frozen_opponent_sha256).strip().lower()
            if frozen_opponent_sha256 != expected:
                raise ValueError(
                    "frozen-opponent checkpoint SHA-256 mismatch: "
                    f"actual={frozen_opponent_sha256}, expected={expected}"
                )
        frozen_policy, frozen_opponent_meta = policy_from_checkpoint(frozen_path, device=device)
        frozen_policy.eval()
        for parameter in frozen_policy.parameters():
            parameter.requires_grad_(False)
        print(
            f"[FROZEN] checkpoint={frozen_path} "
            f"update={int(frozen_opponent_meta.get('update', 0))} "
            f"games={int(frozen_opponent_meta.get('total_games', 0))} "
            f"sha256={frozen_opponent_sha256[:16]}..."
        )
    elif training_mode != "self_play":
        raise ValueError(f"unknown training mode: {training_mode}")

    print(
        f"[START] device={device_name(device)} mode={training_mode} "
        f"games={total_games}/{total_game_budget if total_game_budget > 0 else 'open'} "
        f"best_eval_every={config.eval.interval} promote_at={args.best_promote_win_rate:.3f}"
    )

    def make_collector(
        base_seed: int,
        *,
        player0_deck: str | None = None,
        player1_deck: str | None = None,
        matchups: dict[str, dict[str, Any]] | None = None,
    ) -> RlCollector:
        return RlCollector(
            num_envs=config.collector.num_envs,
            max_batch_size=config.collector.max_batch_size or config.collector.num_envs,
            base_seed=base_seed,
            reward_mode=config.collector.reward_mode,
            reward_overrides=config.collector.reward_overrides,
            enable_invariants=config.collector.enable_invariants,
            player0_deck=player0_deck or config.collector.player0_deck,
            player1_deck=player1_deck or config.collector.player1_deck,
            matchups=config.collector.matchups if matchups is None else matchups,
            library_path=args.library,
        )

    def checkpoint_extra() -> dict[str, Any]:
        extra = {
            "total_games": total_games,
            "rollout_mode": "games" if game_based else "steps",
            "games_per_update": int(config.ppo.games_per_update),
            "initialization": initialization_meta,
            "training_mode": training_mode,
            "model_architecture": str(getattr(config.model, "architecture", "single_head")),
            "shared_train_scope": train_scope,
        }
        if training_mode == "learner_vs_frozen":
            extra["training_control"] = {
                "mode": training_mode,
                "learner_deck": args.learner_deck,
                "opponent_deck": args.opponent_deck,
                "side_schedule": "alternate",
                "frozen_checkpoint": str(args.frozen_opponent_checkpoint),
                "frozen_checkpoint_sha256": frozen_opponent_sha256,
            }
        return extra

    def _evaluate_head_vs_opponent(
        candidate_policy: CandidatePolicyValueNet,
        opponent_policy: CandidatePolicyValueNet,
        *,
        learner_deck: str,
        opponent_deck: str,
        seed: int,
    ) -> MatchupStats:
        """Evaluate one deck head on both player seats against a fixed opponent."""
        games_p0 = int(config.eval.games) // 2
        games_p1 = int(config.eval.games) - games_p0
        total = MatchupStats()
        if games_p0 > 0:
            total.merge(
                evaluate_matchup_once(
                    candidate_policy,
                    opponent_policy,
                    num_envs=min(config.eval.num_envs, games_p0),
                    games=games_p0,
                    seed=seed,
                    a_player=0,
                    deterministic=config.eval.deterministic,
                    reward_mode=config.collector.reward_mode,
                    player0_deck=learner_deck,
                    player1_deck=opponent_deck,
                    library_path=args.library,
                    device=device,
                )
            )
        if games_p1 > 0:
            total.merge(
                evaluate_matchup_once(
                    candidate_policy,
                    opponent_policy,
                    num_envs=min(config.eval.num_envs, games_p1),
                    games=games_p1,
                    seed=seed + 1_000_003,
                    a_player=1,
                    deterministic=config.eval.deterministic,
                    reward_mode=config.collector.reward_mode,
                    player0_deck=opponent_deck,
                    player1_deck=learner_deck,
                    library_path=args.library,
                    device=device,
                )
            )
        return total

    def _evaluate_joint_candidate(
        best_policy: CandidatePolicyValueNet,
        *,
        update: int,
    ) -> tuple[dict[str, float], dict[str, float]]:
        """Compare a whole shared checkpoint with best_joint and split score by deck."""
        configured = list(config.collector.matchups.items()) or [("fixed", {
            "player0": config.collector.player0_deck,
            "player1": config.collector.player1_deck,
            "weight": 1,
        })]
        game_budget = allocate_weighted_games(
            config.eval.games,
            [(name, int(matchup.get("weight", 1))) for name, matchup in configured],
        )
        overall = MatchupStats()
        by_deck = {"a": MatchupStats(), "b": MatchupStats()}
        matrix: dict[str, dict[str, float]] = {}

        for index, (name, matchup) in enumerate(configured):
            matchup_games = int(game_budget[name])
            player0_deck = str(matchup["player0"])
            player1_deck = str(matchup["player1"])
            games_a0 = matchup_games // 2
            games_a1 = matchup_games - games_a0
            matchup_total = MatchupStats()

            for side, side_games, side_seed in (
                (0, games_a0, config.seed + 100000 + update + index * 1009),
                (1, games_a1, config.seed + 1_100_003 + update + index * 1009),
            ):
                if side_games <= 0:
                    continue
                side_stats = evaluate_matchup_once(
                    policy,
                    best_policy,
                    num_envs=min(config.eval.num_envs, side_games),
                    games=side_games,
                    seed=side_seed,
                    a_player=side,
                    deterministic=config.eval.deterministic,
                    reward_mode=config.collector.reward_mode,
                    player0_deck=player0_deck,
                    player1_deck=player1_deck,
                    library_path=args.library,
                    device=device,
                )
                matchup_total.merge(side_stats)
                overall.merge(side_stats)
                candidate_deck = _normalize_deck_id(player0_deck if side == 0 else player1_deck)
                by_deck[candidate_deck].merge(side_stats)

            item = matchup_total.as_dict()
            matrix[name] = item

        if int(overall.completed_episodes) != int(config.eval.games):
            raise RuntimeError(
                f"evaluation completed {int(overall.completed_episodes)} games, "
                f"expected exactly {int(config.eval.games)}"
            )
        if by_deck["a"].completed_episodes <= 0 or by_deck["b"].completed_episodes <= 0:
            raise RuntimeError(
                "joint best evaluation must include candidate-controlled games for both deck A and deck B"
            )

        stats = overall.as_dict()
        stats["joint_score"] = _score_rate(overall)
        stats["a_score"] = _score_rate(by_deck["a"])
        stats["b_score"] = _score_rate(by_deck["b"])
        stats["a_eval_games"] = float(by_deck["a"].completed_episodes)
        stats["b_eval_games"] = float(by_deck["b"].completed_episodes)
        for name, item in matrix.items():
            stats[f"matchup_{name}_target_games"] = float(game_budget[name])
            stats[f"matchup_{name}_score"] = (
                float(item["a_wins"]) + 0.5 * float(item["draws"])
            ) / max(float(item["completed_episodes"]), 1.0)
            stats[f"matchup_{name}_episodes"] = float(item["completed_episodes"])
        return stats, {
            "a": float(stats["a_score"]),
            "b": float(stats["b_score"]),
            "joint": float(stats["joint_score"]),
        }

    def _evaluate_legacy_best(update: int, *, resume_check: bool = False) -> None:
        """Original single-head current-vs-best gate, kept for compatibility."""
        best_policy, best_meta = policy_from_checkpoint(best_path, device=device)
        best_update = int(best_meta.get("update", 0))
        configured = list(config.collector.matchups.items()) or [("fixed", {
            "player0": config.collector.player0_deck,
            "player1": config.collector.player1_deck,
            "weight": 1,
        })]
        game_budget = allocate_weighted_games(
            config.eval.games,
            [(name, int(matchup.get("weight", 1))) for name, matchup in configured],
        )
        matrix: dict[str, dict[str, float]] = {}
        for index, (name, matchup) in enumerate(configured):
            matchup_games = game_budget[name]
            matrix[name] = evaluate_policies(
                policy,
                best_policy,
                num_envs=min(config.eval.num_envs, matchup_games),
                games=matchup_games,
                seed=config.seed + 100000 + update + index * 1009,
                swap_sides=True,
                deterministic=config.eval.deterministic,
                reward_mode=config.collector.reward_mode,
                player0_deck=str(matchup["player0"]),
                player1_deck=str(matchup["player1"]),
                library_path=args.library,
                device=device,
            )
        episodes = sum(item["completed_episodes"] for item in matrix.values())
        if int(episodes) != int(config.eval.games):
            raise RuntimeError(
                f"evaluation completed {int(episodes)} games, expected exactly {int(config.eval.games)}"
            )
        wins = sum(item["a_wins"] for item in matrix.values())
        losses = sum(item["b_wins"] for item in matrix.values())
        draws = sum(item["draws"] for item in matrix.values())
        stats = {
            "completed_episodes": episodes,
            "a_wins": wins,
            "b_wins": losses,
            "draws": draws,
            "a_win_rate": wins / max(episodes, 1),
            "b_win_rate": losses / max(episodes, 1),
            "draw_rate": draws / max(episodes, 1),
            "illegal_result_count": sum(item["illegal_result_count"] for item in matrix.values()),
            "a_secured_round_opportunities": sum(item["a_secured_round_opportunities"] for item in matrix.values()),
            "a_secured_round_passes": sum(item["a_secured_round_passes"] for item in matrix.values()),
            "a_unsafe_pass_opportunities": sum(item["a_unsafe_pass_opportunities"] for item in matrix.values()),
            "a_unsafe_passes": sum(item["a_unsafe_passes"] for item in matrix.values()),
        }
        stats["a_secured_round_pass_rate"] = stats["a_secured_round_passes"] / max(stats["a_secured_round_opportunities"], 1)
        stats["a_unsafe_pass_rate"] = stats["a_unsafe_passes"] / max(stats["a_unsafe_pass_opportunities"], 1)
        for name, item in matrix.items():
            stats[f"matchup_{name}_target_games"] = float(game_budget[name])
            stats[f"matchup_{name}_win_rate"] = item["a_win_rate"]
            stats[f"matchup_{name}_episodes"] = item["completed_episodes"]
        del best_policy

        win = float(stats["a_win_rate"])
        loss = float(stats["b_win_rate"])
        draw = float(stats["draw_rate"])
        illegal = int(stats["illegal_result_count"])
        promoted = illegal == 0 and win >= float(args.best_promote_win_rate)
        eval_log.write({
            "update": update,
            "total_games": total_games,
            "total_decisions": total_decisions,
            "best_update_before": best_update,
            "promote_threshold": float(args.best_promote_win_rate),
            "promoted": int(promoted),
            **stats,
        })
        tag = "EVAL-RESUME" if resume_check else "EVAL"
        print(
            f"[{tag}] u={update} current_vs_best(u={best_update}) "
            f"W/L/D={win:.3f}/{loss:.3f}/{draw:.3f} illegal={illegal}"
        )
        if promoted:
            save_checkpoint(
                best_path,
                policy,
                optimizer,
                config=config,
                update=update,
                total_decisions=total_decisions,
                extra={**checkpoint_extra(), "best_promoted_from_update": best_update},
            )
            print(f"[BEST] promoted {best_update} -> {update} win_rate={win:.3f}")
        else:
            print(f"[BEST] kept u={best_update} candidate_win_rate={win:.3f}")
        policy.train()

    def evaluate_against_best(update: int, *, resume_check: bool = False) -> None:
        if not is_shared_policy:
            _evaluate_legacy_best(update, resume_check=resume_check)
            return

        best_kind = _shared_best_kind(train_scope)
        scoped_path = {"a": best_a_path, "b": best_b_path, "joint": best_joint_path}[best_kind]
        best_policy, best_meta = policy_from_checkpoint(scoped_path, device=device)
        best_update = int(best_meta.get("update", 0))
        tag = "EVAL-RESUME" if resume_check else "EVAL"

        if best_kind in {"a", "b"}:
            learner_deck = "deck_a" if best_kind == "a" else "deck_b"
            if training_mode == "learner_vs_frozen":
                if frozen_policy is None or not args.opponent_deck:
                    raise RuntimeError("head-only best tracking requires the configured frozen opponent")
                opponent_policy = frozen_policy
                opponent_deck = str(args.opponent_deck)
                # Constant benchmark seeds make best_a/b scores directly comparable
                # across updates instead of letting evaluation-seed noise move best.
                benchmark_seed = config.seed + (700_001 if best_kind == "a" else 800_001)
                candidate_stats = _evaluate_head_vs_opponent(
                    policy,
                    opponent_policy,
                    learner_deck=learner_deck,
                    opponent_deck=opponent_deck,
                    seed=benchmark_seed,
                )
                best_score_value = best_meta.get("best_scope_score")
                if best_score_value is None:
                    baseline_stats = _evaluate_head_vs_opponent(
                        best_policy,
                        opponent_policy,
                        learner_deck=learner_deck,
                        opponent_deck=opponent_deck,
                        seed=benchmark_seed,
                    )
                    best_score = _score_rate(baseline_stats)
                else:
                    best_score = float(best_score_value)
                candidate_score = _score_rate(candidate_stats)
                illegal = int(candidate_stats.illegal_result_count)
                promoted = illegal == 0 and candidate_score > best_score + 1e-12
            else:
                # Fallback for head-only self-play: directly duel the same deck
                # head in current vs best on both seats.
                same_deck = learner_deck
                duel = evaluate_policies(
                    policy,
                    best_policy,
                    num_envs=config.eval.num_envs,
                    games=config.eval.games,
                    seed=config.seed + 900_001 + update,
                    swap_sides=True,
                    deterministic=config.eval.deterministic,
                    reward_mode=config.collector.reward_mode,
                    player0_deck=same_deck,
                    player1_deck=same_deck,
                    library_path=args.library,
                    device=device,
                )
                candidate_score = (
                    float(duel["a_wins"]) + 0.5 * float(duel["draws"])
                ) / max(float(duel["completed_episodes"]), 1.0)
                best_score = 0.5
                illegal = int(duel["illegal_result_count"])
                promoted = illegal == 0 and candidate_score >= float(args.best_promote_win_rate)
                candidate_stats = MatchupStats(
                    completed_episodes=int(duel["completed_episodes"]),
                    a_wins=int(duel["a_wins"]),
                    b_wins=int(duel["b_wins"]),
                    draws=int(duel["draws"]),
                    illegal_result_count=illegal,
                )

            row = {
                "update": update,
                "total_games": total_games,
                "total_decisions": total_decisions,
                "best_scope": best_kind,
                "best_update_before": best_update,
                "candidate_score": candidate_score,
                "best_score_before": best_score,
                "a_score": candidate_score if best_kind == "a" else None,
                "b_score": candidate_score if best_kind == "b" else None,
                "joint_score": None,
                "promoted_a": int(promoted and best_kind == "a"),
                "promoted_b": int(promoted and best_kind == "b"),
                "promoted_joint": 0,
                "promoted": int(promoted),
                "illegal_result_count": illegal,
                "completed_episodes": int(candidate_stats.completed_episodes),
                "a_wins": int(candidate_stats.a_wins),
                "b_wins": int(candidate_stats.b_wins),
                "draws": int(candidate_stats.draws),
            }
            eval_log.write(row)
            print(
                f"[{tag}-{best_kind.upper()}] u={update} candidate={candidate_score:.3f} "
                f"best={best_score:.3f}(u={best_update}) illegal={illegal}"
            )
            if promoted:
                save_checkpoint(
                    scoped_path,
                    policy,
                    optimizer,
                    config=config,
                    update=update,
                    total_decisions=total_decisions,
                    extra={
                        **checkpoint_extra(),
                        "best_scope": best_kind,
                        "best_scope_score": candidate_score,
                        "best_promoted_from_update": best_update,
                    },
                )
                shutil.copy2(scoped_path, best_path)
                print(
                    f"[BEST-{best_kind.upper()}] promoted {best_update} -> {update} "
                    f"score={best_score:.3f}->{candidate_score:.3f}"
                )
            else:
                print(
                    f"[BEST-{best_kind.upper()}] kept u={best_update} "
                    f"candidate={candidate_score:.3f} best={best_score:.3f}"
                )
            del best_policy
            policy.train()
            return

        stats, deck_scores = _evaluate_joint_candidate(best_policy, update=update)
        illegal = int(stats["illegal_result_count"])
        joint_score = float(deck_scores["joint"])
        a_score = float(deck_scores["a"])
        b_score = float(deck_scores["b"])
        promoted = _joint_promotion_allowed(
            joint_score=joint_score,
            a_score=a_score,
            b_score=b_score,
            illegal=illegal,
            promote_threshold=float(args.best_promote_win_rate),
            min_deck_score=float(args.best_min_deck_score),
        )
        row = {
            "update": update,
            "total_games": total_games,
            "total_decisions": total_decisions,
            "best_scope": "joint",
            "best_update_before": best_update,
            "promote_threshold": float(args.best_promote_win_rate),
            "min_deck_score": float(args.best_min_deck_score),
            "promoted_a": 0,
            "promoted_b": 0,
            "promoted_joint": int(promoted),
            "promoted": int(promoted),
            **stats,
        }
        eval_log.write(row)
        print(
            f"[{tag}-JOINT] u={update} current_vs_best_joint(u={best_update}) "
            f"joint={joint_score:.3f} A={a_score:.3f} B={b_score:.3f} "
            f"gate>={args.best_promote_win_rate:.3f} per_deck>={args.best_min_deck_score:.3f} "
            f"illegal={illegal}"
        )
        if promoted:
            save_checkpoint(
                best_joint_path,
                policy,
                optimizer,
                config=config,
                update=update,
                total_decisions=total_decisions,
                extra={
                    **checkpoint_extra(),
                    "best_scope": "joint",
                    "best_joint_score": joint_score,
                    "best_joint_a_score": a_score,
                    "best_joint_b_score": b_score,
                    "best_promoted_from_update": best_update,
                },
            )
            shutil.copy2(best_joint_path, best_path)
            print(
                f"[BEST-JOINT] promoted {best_update} -> {update} "
                f"joint={joint_score:.3f} A={a_score:.3f} B={b_score:.3f}"
            )
        else:
            print(
                f"[BEST-JOINT] kept u={best_update} candidate joint={joint_score:.3f} "
                f"A={a_score:.3f} B={b_score:.3f}"
            )
        del best_policy
        policy.train()

    # If resuming exactly at an evaluation boundary, re-run that comparison.
    # This is crash-safe: a failure during evaluation cannot permanently skip gating.
    if args.resume and last_completed_update > 0 and config.eval.interval > 0:
        if last_completed_update % int(config.eval.interval) == 0:
            evaluate_against_best(last_completed_update, resume_check=True)

    def run_update(update: int, collector: RlCollector, *, learner_actor: int | None = None) -> None:
        nonlocal total_decisions, total_games
        if training_mode == "learner_vs_frozen":
            if frozen_policy is None or learner_actor not in (0, 1):
                raise RuntimeError("focused training requires frozen policy and learner_actor")
            runner = LearnerVsFrozenRunner(
                collector, policy, frozen_policy, learner_actor=int(learner_actor), device=device
            )
        else:
            runner = RolloutRunner(collector, policy, device=device)

        before = time.perf_counter()
        if game_based:
            rollout, rstats = runner.collect_games(config.ppo.games_per_update)
        else:
            rollout, rstats = runner.collect_steps(config.ppo.steps_per_update)
        collect_s = time.perf_counter() - before

        rollout.compute_signed_gae(
            ppo_config.gamma,
            ppo_config.gae_lambda,
            require_complete_episodes=game_based,
        )
        environment_decisions = int(rstats.decisions)
        if training_mode == "learner_vs_frozen":
            assert learner_actor is not None
            training_decisions = rollout.retain_actor(int(learner_actor))
            if training_decisions <= 0:
                raise RuntimeError("focused rollout contains no learner-owned decisions")
        else:
            training_decisions = rollout.size
        tensor_rollout = rollout.as_torch(device=device)
        policy.train()
        stats = ppo_update(policy, optimizer, tensor_rollout, ppo_config)
        total_decisions += int(rstats.decisions)
        total_games += int(rstats.completed_episodes)

        avg_decisions_per_game = (
            float(rstats.decisions) / float(rstats.completed_episodes)
            if rstats.completed_episodes > 0
            else 0.0
        )
        collect_dps = float(rstats.decisions) / max(collect_s, 1e-9)
        row: dict[str, Any] = {
            "update": update,
            "generation": update if game_based else 0,
            "rollout_mode": "games" if game_based else "steps",
            "total_games": total_games,
            "rollout_games": rstats.completed_episodes,
            "total_decisions": total_decisions,
            "rollout_decisions": environment_decisions,
            "training_decisions": training_decisions,
            "learner_actor": int(learner_actor) if learner_actor is not None else -1,
            "avg_decisions_per_game": avg_decisions_per_game,
            "collector_steps": rstats.collector_steps,
            "loss": stats.loss,
            "policy_loss": stats.policy_loss,
            "value_loss": stats.value_loss,
            "entropy": stats.entropy,
            "approx_kl": stats.approx_kl,
            "clip_fraction": stats.clip_fraction,
            "grad_norm": stats.grad_norm,
            "explained_variance": stats.explained_variance,
            "ppo_minibatches": stats.updates_applied,
            "ppo_early_stopped": int(stats.early_stopped),
            "collect_s": collect_s,
            "decisions_per_second": collect_dps,
        }
        metrics.write(row)
        budget_text = str(total_game_budget) if total_game_budget > 0 else "open"
        print(
            f"[TRAIN] u={update} games={total_games}/{budget_text} "
            f"rollout={rstats.completed_episodes} decisions={environment_decisions} "
            f"train_decisions={training_decisions} "
            f"loss={stats.loss:.4f} entropy={stats.entropy:.4f} "
            f"kl={stats.approx_kl:.5f} ev={stats.explained_variance:.3f} dps={collect_dps:.0f}"
        )

        # latest.pt is written after EVERY successful PPO update so resume loses
        # at most the currently collecting generation, not several generations.
        save_checkpoint(
            latest_path,
            policy,
            optimizer,
            config=config,
            update=update,
            total_decisions=total_decisions,
            extra=checkpoint_extra(),
        )

        if config.checkpoint_interval > 0 and update % int(config.checkpoint_interval) == 0:
            archive_path = paths.checkpoints_dir / f"update_{update:06d}.pt"
            save_checkpoint(
                archive_path,
                policy,
                optimizer,
                config=config,
                update=update,
                total_decisions=total_decisions,
                extra=checkpoint_extra(),
            )
            print(f"[SAVE] u={update} checkpoint={archive_path.name}")

        if config.eval.interval > 0 and update % int(config.eval.interval) == 0:
            evaluate_against_best(update)

    start_update = last_completed_update + 1
    if game_based:
        update = start_update
        while True:
            if total_game_budget > 0:
                remaining = total_game_budget - total_games
                if remaining <= 0:
                    break
                generation_games = min(int(config.ppo.games_per_update), remaining)
            else:
                if update > int(config.ppo.updates):
                    break
                generation_games = int(config.ppo.games_per_update)
            generation_seed = config.collector.base_seed + (update - 1) * 1000003
            original_games_per_update = config.ppo.games_per_update
            config.ppo.games_per_update = generation_games
            try:
                if training_mode == "learner_vs_frozen":
                    learner_actor = 0 if (update % 2 == 1) else 1
                    if learner_actor == 0:
                        player0_deck, player1_deck = args.learner_deck, args.opponent_deck
                    else:
                        player0_deck, player1_deck = args.opponent_deck, args.learner_deck
                    with make_collector(
                        generation_seed,
                        player0_deck=player0_deck,
                        player1_deck=player1_deck,
                        matchups={},
                    ) as collector:
                        print(
                            f"[FOCUS] u={update} learner_actor=P{learner_actor} "
                            f"P0={player0_deck} P1={player1_deck}"
                        )
                        run_update(update, collector, learner_actor=learner_actor)
                else:
                    with make_collector(generation_seed) as collector:
                        run_update(update, collector)
            finally:
                config.ppo.games_per_update = original_games_per_update
            update += 1
    else:
        if training_mode == "learner_vs_frozen":
            raise ValueError("learner_vs_frozen does not support step-based updates")
        with make_collector(config.collector.base_seed) as collector:
            for update in range(start_update, int(config.ppo.updates) + 1):
                run_update(update, collector)

    done_best_path = best_path
    done_best_kind = "legacy"
    if is_shared_policy:
        done_best_kind = _shared_best_kind(train_scope)
        done_best_path = {
            "a": best_a_path,
            "b": best_b_path,
            "joint": best_joint_path,
        }[done_best_kind]
    _best_policy, best_meta = policy_from_checkpoint(done_best_path, device="cpu")
    del _best_policy
    print(
        f"[DONE] games={total_games} updates={max(start_update - 1, 0)}+ "
        f"best_kind={done_best_kind} best_update={int(best_meta.get('update', 0))} "
        f"best={done_best_path.name} run_dir={paths.run_dir}"
    )


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="PPO training using latest-policy self-play and best-model evaluation")
    parser.add_argument("--config", default=None, help="JSON or simple YAML experiment config")
    parser.add_argument("--run-dir", default=None)
    parser.add_argument("--resume", default=None, help="Resume model, optimizer, update and game counters from checkpoint")
    parser.add_argument(
        "--initialize-from",
        default=None,
        help="Warm-start model weights from an explicitly compatible older checkpoint; optimizer/counters are reset",
    )
    parser.add_argument("--initialize-source-schema", type=int, default=None)
    parser.add_argument("--initialize-source-action-grammar", type=int, default=None)
    parser.add_argument(
        "--initialize-checkpoint-sha256",
        default=None,
        help="Optional pinned SHA-256 for warm-start source; mismatch fails before model load",
    )
    parser.add_argument(
        "--reset-decision-embedding",
        action="append",
        default=[],
        help="Decision embedding name to reinitialize after warm start; may be repeated",
    )
    parser.add_argument(
        "--reset-option-embedding",
        action="append",
        default=[],
        help="Option embedding name to reinitialize after warm start; may be repeated",
    )
    parser.add_argument(
        "--best-promote-win-rate",
        type=float,
        default=0.55,
        help=(
            "Single-head/current-vs-best promotion threshold; for shared joint training this is the "
            "minimum joint score versus best_joint. Head-only best_a/b instead tracks monotonic score "
            "against the configured frozen teacher."
        ),
    )
    parser.add_argument(
        "--best-min-deck-score",
        type=float,
        default=0.40,
        help=(
            "For shared joint training, require both candidate A and B score rates versus best_joint "
            "to stay at or above this floor before promoting the whole checkpoint. Default 0.40."
        ),
    )
    parser.add_argument(
        "--training-mode",
        choices=["self_play", "learner_vs_frozen"],
        default="self_play",
        help="Policy ownership mode. learner_vs_frozen optimizes only learner-owned decisions.",
    )
    parser.add_argument("--learner-deck", default=None)
    parser.add_argument("--opponent-deck", default=None)
    parser.add_argument("--frozen-opponent-checkpoint", default=None)
    parser.add_argument("--frozen-opponent-sha256", default=None)
    parser.add_argument("--num-envs", type=int, default=None)
    parser.add_argument("--max-batch-size", type=int, default=None)
    parser.add_argument("--steps-per-update", type=int, default=None)
    parser.add_argument("--games-per-update", type=int, default=None)
    parser.add_argument("--updates", type=int, default=None)
    parser.add_argument("--total-games", type=int, default=None)
    parser.add_argument(
        "--model-architecture",
        choices=["single_head", "shared_deck_heads_v1", "shared_deck_private_v3"],
        default=None,
        help=(
            "Neural architecture; shared_deck_private_v3 uses a roughly 50/50 "
            "shared/private active path for each deck."
        ),
    )
    parser.add_argument(
        "--shared-train-scope",
        choices=[
            "joint",
            "a_head_only",
            "b_head_only",
            "a_private_only",
            "b_private_only",
            "shared_only",
        ],
        default="joint",
        help=(
            "Shared-model scope. In V3 a/b_private_only trains the whole deck-private half; "
            "a/b_head_only are compatibility aliases."
        ),
    )
    parser.add_argument("--hidden-dim", type=int, default=None)
    parser.add_argument("--num-attention-heads", type=int, default=None)
    parser.add_argument("--attention-layers", type=int, default=None)
    parser.add_argument("--dropout", type=float, default=None)
    parser.add_argument("--text-embedding-mode", choices=["none", "hash", "npz"], default=None)
    parser.add_argument("--text-embedding-path", default=None)
    parser.add_argument("--text-embedding-dim", type=int, default=None)
    parser.add_argument("--freeze-text-embeddings", action="store_true")
    parser.add_argument("--train-text-embeddings", action="store_true")
    parser.add_argument("--disable-candidate-object-attention", action="store_true")
    parser.add_argument("--disable-candidate-self-attention", action="store_true")
    parser.add_argument("--learning-rate", type=float, default=None)
    parser.add_argument("--shared-learning-rate", type=float, default=None)
    parser.add_argument("--private-learning-rate", type=float, default=None)
    parser.add_argument("--epochs", type=int, default=None)
    parser.add_argument("--minibatch-size", type=int, default=None)
    parser.add_argument("--gamma", type=float, default=None)
    parser.add_argument("--gae-lambda", type=float, default=None)
    parser.add_argument("--reward-mode", default=None)
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--device", default=None)
    parser.add_argument("--checkpoint-interval", type=int, default=None)
    parser.add_argument("--eval-interval", type=int, default=None)
    parser.add_argument("--eval-games", type=int, default=None, help="Complete games per internal evaluation across all configured matchups")
    parser.add_argument("--eval-num-envs", type=int, default=None)
    parser.add_argument("--enable-invariants", action="store_true")
    parser.add_argument("--library", default=None)
    parser.add_argument("--checkpoint", default=None, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if not (0.0 <= args.best_promote_win_rate <= 1.0):
        parser.error("--best-promote-win-rate must be between 0 and 1")
    if not (0.0 <= args.best_min_deck_score <= 1.0):
        parser.error("--best-min-deck-score must be between 0 and 1")
    if args.checkpoint and args.run_dir is None:
        args.run_dir = str(
            Path(args.checkpoint).parent.parent
            if Path(args.checkpoint).parent.name == "checkpoints"
            else Path(args.checkpoint).parent
        )
    train(args)


if __name__ == "__main__":
    main()
