from __future__ import annotations

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
REQUIRED = [
    ROOT / "services/teacher/agent.py",
    ROOT / "services/teacher/evidence.py",
    ROOT / "services/teacher/providers.py",
    ROOT / "services/teacher/api.py",
    ROOT / "apps/web/backend/app/api/teacher.py",
    ROOT / "apps/web/frontend/src/components/TeacherPanel.tsx",
]


def main() -> int:
    missing = [str(path.relative_to(ROOT)) for path in REQUIRED if not path.exists()]
    if missing:
        print("[teacher-skill] missing:")
        for item in missing:
            print(f"  - {item}")
        return 1

    importlib.import_module("services.teacher.agent")
    importlib.import_module("services.teacher.evidence")
    importlib.import_module("services.teacher.api")
    print("[teacher-skill] PASS: runtime modules and product integration files present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
