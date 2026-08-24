#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from gwent_rl.benchmark_analysis import run_analysis
from gwent_rl.config import load_experiment_config


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", required=True)
    args = parser.parse_args()
    work = Path(args.work_dir)
    bench = work / "bench"
    bench.mkdir(parents=True, exist_ok=True)
    (bench / "benchmark_matrix.csv").write_text(
        "case_id,num_envs,hidden_dim,text_embedding_mode,text_embedding_dim,use_candidate_object_attention,use_candidate_self_attention,attention_layers,num_attention_heads,reward_mode,decisions_per_second,collect_ms,to_torch_ms,forward_ms,action_select_ms,apply_ms,loop_overhead_ms,rss_delta_mb,model_param_count,device\n"
        "case_0001,4,32,none,0,true,true,1,4,terminal,1000,0.1,0.05,0.2,0.01,0.12,0.01,3,50000,cpu\n"
        "case_0002,8,64,hash,16,true,true,1,4,terminal,1800,0.2,0.08,0.35,0.02,0.18,0.02,8,150000,cpu\n",
        encoding="utf-8",
    )
    analysis = run_analysis(bench, output_dir=work / "analysis", top_k=2, write_configs=True)
    if analysis["case_count"] != 2:
        raise SystemExit("unexpected analysis case count")
    cfg = load_experiment_config(work / "analysis" / "configs" / "ppo_best_balanced.yaml")
    if cfg.collector.num_envs not in {4, 8}:
        raise SystemExit("generated config was not parseable")
    print(work / "analysis" / "benchmark_report.md")


if __name__ == "__main__":
    main()
