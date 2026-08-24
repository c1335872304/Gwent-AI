from __future__ import annotations

import argparse
import json
from pathlib import Path

from .agent import TeacherAgent
from .models import TeacherRequest


def main() -> int:
    parser = argparse.ArgumentParser(description="Explain one Strategy Core decision packet.")
    parser.add_argument("input", type=Path, help="DecisionPacket JSON, TraceDecision JSON, or trace document")
    parser.add_argument("--decision-index", type=int, default=0, help="index when input is a trace document")
    parser.add_argument("--level", choices=["beginner", "intermediate", "advanced"], default="beginner")
    parser.add_argument("--public-state", type=Path, help="optional public GameState JSON")
    parser.add_argument("--json", action="store_true", help="emit full structured response JSON")
    args = parser.parse_args()

    payload = json.loads(args.input.read_text(encoding="utf-8"))
    if isinstance(payload, dict) and isinstance(payload.get("decisions"), list):
        payload = payload["decisions"][args.decision_index]
    public_state = {}
    if args.public_state:
        public_state = json.loads(args.public_state.read_text(encoding="utf-8"))

    response = TeacherAgent().explain(
        TeacherRequest(
            decision_packet=dict(payload),
            public_state=dict(public_state),
            level=args.level,
        )
    )
    if args.json:
        print(json.dumps(response.to_dict(), ensure_ascii=False, indent=2))
    else:
        print(response.headline)
        print(response.explanation)
        if response.alternatives:
            print("备选：")
            for alt in response.alternatives:
                suffix = f" ({alt.probability * 100:.1f}%)" if alt.probability is not None else ""
                print(f"- {alt.label}{suffix}")
        if response.caveats:
            print("说明：")
            for item in response.caveats:
                print(f"- {item}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
