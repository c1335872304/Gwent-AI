#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
ROOT="$PWD"

PY="/home/ly00005757/.conda/envs/crk/bin/python"
LIB="$ROOT/build-release/libgwent_core.so.0.1.0"
A="$ROOT/artifacts/policies/deck_a.pt"
B="$ROOT/artifacts/policies/deck_b.pt"
RUN="$ROOT/../gwent_runtime/runs/tasks/deck_b_focus_next_500k"

export PYTHONPATH="$ROOT/python/src:${PYTHONPATH:-}"
export GWENT_COLLECTOR_THREADS="${GWENT_COLLECTOR_THREADS:-12}"

A_SHA="$(sha256sum "$A" | awk '{print $1}')"
B_SHA="$(sha256sum "$B" | awk '{print $1}')"

mkdir -p "$RUN"

echo "[DECK-B NEXT TRAIN]"
echo "fixed A : $A"
echo "start B : $B"
echo "run     : $RUN"
echo "A sha   : $A_SHA"
echo "B sha   : $B_SHA"

exec "$PY" -m gwent_rl.train_ppo \
  --config "$ROOT/configs/training/ppo_128env_ab_strategic.yaml" \
  --total-games 500000 \
  --games-per-update 2500 \
  --num-envs 512 \
  --max-batch-size 512 \
  --minibatch-size 2048 \
  --device auto \
  --checkpoint-interval 2 \
  --eval-interval 2 \
  --eval-games 2000 \
  --eval-num-envs 128 \
  --best-promote-win-rate 0.55 \
  --run-dir "$RUN" \
  --training-mode learner_vs_frozen \
  --learner-deck deck_b \
  --opponent-deck deck_a \
  --frozen-opponent-checkpoint "$A" \
  --frozen-opponent-sha256 "$A_SHA" \
  --library "$LIB" \
  --initialize-from "$B" \
  --initialize-source-schema 13 \
  --initialize-source-action-grammar 5 \
  --initialize-checkpoint-sha256 "$B_SHA"
