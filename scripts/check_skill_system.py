#!/usr/bin/env python3
"""Validate the repository's Skill-driven agent package.

This check is intentionally structural: it verifies that every Coding Agent maps to
one Skill, that Skill metadata is valid, and that referenced local resources exist.
It does not judge prose quality or replace task-specific tests.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKILLS_DIR = ROOT / ".agents" / "skills"
AGENTS_DIR = ROOT / ".codex" / "agents"

EXPECTED = {
    "core": "core-environment",
    "trainer": "training-config",
    "product": "product-integration",
    "teacher": "teacher-explanation",
}

FRONTMATTER_RE = re.compile(r"\A---\s*\n(.*?)\n---\s*\n", re.DOTALL)
RESOURCE_RE = re.compile(r"`((?:references|scripts)/[^`\s]+)`")


def parse_frontmatter(text: str, path: Path) -> dict[str, str]:
    match = FRONTMATTER_RE.match(text)
    if not match:
        raise RuntimeError(f"{path.relative_to(ROOT)}: missing YAML frontmatter")
    data: dict[str, str] = {}
    for raw in match.group(1).splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        if ":" not in raw:
            raise RuntimeError(f"{path.relative_to(ROOT)}: invalid frontmatter line: {raw!r}")
        key, value = raw.split(":", 1)
        data[key.strip()] = value.strip().strip('"').strip("'")
    return data


def validate_skill(skill_name: str) -> list[str]:
    errors: list[str] = []
    skill_dir = SKILLS_DIR / skill_name
    skill_md = skill_dir / "SKILL.md"
    if not skill_md.exists():
        return [f"missing {skill_md.relative_to(ROOT)}"]

    text = skill_md.read_text(encoding="utf-8")
    try:
        meta = parse_frontmatter(text, skill_md)
    except RuntimeError as exc:
        return [str(exc)]

    if meta.get("name") != skill_name:
        errors.append(
            f"{skill_md.relative_to(ROOT)}: name={meta.get('name')!r}, expected {skill_name!r}"
        )
    if not meta.get("description"):
        errors.append(f"{skill_md.relative_to(ROOT)}: missing description")

    for rel in sorted(set(RESOURCE_RE.findall(text))):
        target = skill_dir / rel
        if not target.exists():
            errors.append(f"{skill_md.relative_to(ROOT)}: dangling resource `{rel}`")

    # Project convention: every Skill should expose an explicit deterministic
    # verification entry point, even when that entry point delegates to repo tools.
    verify = skill_dir / "scripts" / "verify.py"
    if not verify.exists():
        errors.append(f"{skill_name}: missing scripts/verify.py")

    for heading in ("## Workflow", "## Invariants", "## Verification", "## Handoff"):
        if heading not in text:
            errors.append(f"{skill_md.relative_to(ROOT)}: missing section {heading}")

    return errors


def validate_agent(agent_name: str, skill_name: str) -> list[str]:
    path = AGENTS_DIR / f"{agent_name}.toml"
    if not path.exists():
        return [f"missing {path.relative_to(ROOT)}"]
    text = path.read_text(encoding="utf-8")
    if f"${skill_name}" not in text:
        return [f"{path.relative_to(ROOT)}: does not reference ${skill_name}"]
    return []


def main() -> int:
    errors: list[str] = []
    actual = {p.name for p in SKILLS_DIR.iterdir() if p.is_dir()} if SKILLS_DIR.exists() else set()
    expected = set(EXPECTED.values())
    if actual != expected:
        errors.append(f"skills mismatch: expected {sorted(expected)}, got {sorted(actual)}")

    for agent, skill in EXPECTED.items():
        errors.extend(validate_agent(agent, skill))
        errors.extend(validate_skill(skill))

    if errors:
        print("FAIL skill system")
        for error in errors:
            print(f"- {error}")
        return 1

    print("PASS skill system: 4 agents -> 4 skills, resources resolved, verify entrypoints present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
