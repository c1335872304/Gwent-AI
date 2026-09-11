#!/bin/sh
set -eu

mode="${GWENT_SERVER_MODE:-${SERVER_MODE:-manual_test}}"
checkpoint="${GWENT_SERVER_CHECKPOINT:-${SERVER_CHECKPOINT:-/app/models/v3/policy.pt}}"

if [ "$mode" = "human_vs_ai" ]; then
    if [ ! -s "$checkpoint" ]; then
        echo "[ERROR] human_vs_ai requires a non-empty checkpoint: $checkpoint" >&2
        exit 64
    fi

    installed_metadata="$(dirname "$checkpoint")/installed.json"
    if [ ! -s "$installed_metadata" ]; then
        # A downloaded final policy is sufficient for inference.  Provenance is
        # valuable but must not force users to download server-side training
        # history or run the installer before their first local game.
        echo "[WARN] no optional model provenance record: $installed_metadata" >&2
        echo "[HINT] run scripts/install_model.py later to record a validated source hash." >&2
    fi
fi

exec "$@"
