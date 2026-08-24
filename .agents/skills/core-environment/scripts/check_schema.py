#!/usr/bin/env python3
"""检查 RL contract 单一版本源、C/Python mirror、维度和当前文档是否同步。"""

from __future__ import annotations

import ast
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
MANIFEST = ROOT / "config/rl_contract.json"
HEADER = ROOT / "include/gwent/c/core.h"
VERSION_HEADER = ROOT / "include/gwent/c/rl_contract_versions.h"
PY_SCHEMA = ROOT / "python/src/gwent_rl/schema.py"
PY_VERSIONS = ROOT / "python/src/gwent_rl/_contract_versions.py"
CURRENT_DOCS = ROOT / "docs/current"
DESIGN_DOCS = ROOT / "docs/design"
MAINTAINED_READMES = [ROOT / "README.md", ROOT / "python/README.md", ROOT / "training/README.md"]

C_TO_PY_SCALARS = {
    "GWENT_RL_MAX_OBJECTS": "MAX_OBJECTS",
    "GWENT_RL_MAX_OPTIONS": "MAX_OPTIONS",
    "GWENT_RL_MAX_PREFIX": "MAX_PREFIX",
}

C_TO_PY_LISTS = {
    "GWENT_RL_GLOBAL_FEATURE_COUNT": "GLOBAL_FEATURE_NAMES",
    "GWENT_RL_OBJECT_FEATURE_COUNT": "OBJECT_FEATURE_NAMES",
    "GWENT_RL_OPTION_FEATURE_COUNT": "OPTION_FEATURE_NAMES",
}

CURRENT_CONTEXT_RE = re.compile(r"(?:\bcurrent\b|当前|现行|运行时|applies\s+to)", re.IGNORECASE)
TRANSITION_CONTEXT_RE = re.compile(r"(?:\bcurrent\s+case\b|迁移|migration|升级路径|→|->|\bintroduced\b|最初|落地时)", re.IGNORECASE)
VERSION_REF_RE = re.compile(
    r"(?:observation\s+schema|(?<!task )\bschema\b|action\s+grammar|\bgrammar\b|reward(?:-config|\s+config)?(?:\s+c\s+abi)?)"
    r"[^\n]{0,32}?\bv?(\d+)\b",
    re.IGNORECASE,
)
OBS_TITLE_RE = re.compile(r"^\s*#\s+RL\s+Observation\s+Schema\s+V?\d+\s*$", re.IGNORECASE)
HEADER_VERSION_METADATA_RE = re.compile(
    r"^\s*(?:\*\*)?(?:Observation\s+Schema|Action\s+Grammar|Reward(?:-Config|\s+Config))(?:\*\*)?\s*[:：]"
    r"\s*(?:\*\*)?v?\d+",
    re.IGNORECASE,
)


def parse_defines(path: Path, names: list[str]) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    out: dict[str, int] = {}
    for name in names:
        match = re.search(rf"^#define\s+{name}\s+(\d+)\s*$", text, re.MULTILINE)
        if not match:
            raise RuntimeError(f"{path.relative_to(ROOT)} 中找不到 {name}")
        out[name] = int(match.group(1))
    return out


def manifest_versions() -> dict[str, int]:
    raw = json.loads(MANIFEST.read_text(encoding="utf-8"))
    return {
        "GWENT_RL_SCHEMA_VERSION": int(raw["observation_schema"]),
        "GWENT_RL_ACTION_GRAMMAR_VERSION": int(raw["action_grammar"]),
        "GWENT_RL_REWARD_CONFIG_VERSION": int(raw["reward_config"]),
    }


def python_generated_versions() -> dict[str, int]:
    tree = ast.parse(PY_VERSIONS.read_text(encoding="utf-8"))
    names = {
        "SCHEMA_VERSION": "GWENT_RL_SCHEMA_VERSION",
        "ACTION_GRAMMAR_VERSION": "GWENT_RL_ACTION_GRAMMAR_VERSION",
        "REWARD_CONFIG_VERSION": "GWENT_RL_REWARD_CONFIG_VERSION",
    }
    out: dict[str, int] = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id in names:
                out[names[target.id]] = int(ast.literal_eval(node.value))
    return out


def python_schema_values() -> dict[str, object]:
    tree = ast.parse(PY_SCHEMA.read_text(encoding="utf-8"))
    out: dict[str, object] = {}
    wanted = {
        "MAX_OBJECTS",
        "MAX_OPTIONS",
        "MAX_PREFIX",
        "GLOBAL_FEATURE_NAMES",
        "OBJECT_FEATURE_NAMES",
        "OPTION_FEATURE_NAMES",
    }
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id in wanted:
                out[target.id] = ast.literal_eval(node.value)
    return out


def check_maintained_docs_do_not_pin_versions() -> list[str]:
    """Current prose should name the manifest, not repeat mutable version numbers.

    Historical/migration statements are allowed. This turns a normal contract bump into
    a manifest/generated-code change instead of a docs-wide number replacement.
    """
    errors: list[str] = []
    files = [
        *MAINTAINED_READMES,
        *sorted(CURRENT_DOCS.glob("*.md")),
        *sorted(DESIGN_DOCS.glob("*.md")),
    ]
    for path in files:
        if not path.exists():
            continue
        rel = path.relative_to(ROOT)
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for index, line in enumerate(lines):
            lineno = index + 1
            if OBS_TITLE_RE.search(line):
                errors.append(
                    f"{rel}:{lineno} 当前规范标题不要写死 schema 版本；改为稳定标题并引用 config/rl_contract.json"
                )
                continue
            if lineno <= 12 and HEADER_VERSION_METADATA_RE.search(line):
                errors.append(
                    f"{rel}:{lineno} 当前规范头部不要写死版本；引用 config/rl_contract.json"
                )
                continue
            if TRANSITION_CONTEXT_RE.search(line):
                continue
            if CURRENT_CONTEXT_RE.search(line) and VERSION_REF_RE.search(line):
                errors.append(
                    f"{rel}:{lineno} 当前版本声明不要复制数字；引用 config/rl_contract.json"
                )
                continue

            # Diagrams/headings often put “current runtime” on one line and the
            # mutable version on the next. Check a short forward window too.
            if CURRENT_CONTEXT_RE.search(line):
                window = " ".join(lines[index : index + 3])
                if not TRANSITION_CONTEXT_RE.search(window) and VERSION_REF_RE.search(window):
                    errors.append(
                        f"{rel}:{lineno} 当前版本声明跨行写死数字；引用 config/rl_contract.json"
                    )
    return errors


def main() -> int:
    manifest = manifest_versions()
    c_versions = parse_defines(VERSION_HEADER, list(manifest))
    py_versions = python_generated_versions()
    c = parse_defines(
        HEADER,
        [
            "GWENT_RL_GLOBAL_FEATURE_COUNT",
            "GWENT_RL_OBJECT_FEATURE_COUNT",
            "GWENT_RL_OPTION_FEATURE_COUNT",
            "GWENT_RL_MAX_OBJECTS",
            "GWENT_RL_MAX_OPTIONS",
            "GWENT_RL_MAX_PREFIX",
        ],
    )
    py = python_schema_values()
    errors: list[str] = []

    header_text = HEADER.read_text(encoding="utf-8")
    if '#include "gwent/c/rl_contract_versions.h"' not in header_text:
        errors.append("include/gwent/c/core.h 未引用生成的 rl_contract_versions.h")

    for name, expected in manifest.items():
        if c_versions.get(name) != expected:
            errors.append(f"{name}: manifest={expected}, C mirror={c_versions.get(name)}")
        if py_versions.get(name) != expected:
            errors.append(f"{name}: manifest={expected}, Python mirror={py_versions.get(name)}")

    for c_name, py_name in C_TO_PY_SCALARS.items():
        expected = c[c_name]
        actual = py.get(py_name)
        if expected != actual:
            errors.append(f"{c_name}={expected}，但 Python {py_name}={actual}")

    for c_name, py_name in C_TO_PY_LISTS.items():
        expected = c[c_name]
        actual = len(py.get(py_name, []))
        if expected != actual:
            errors.append(f"{c_name}={expected}，但 {py_name} 长度={actual}")

    errors.extend(check_maintained_docs_do_not_pin_versions())

    print("=== RL Contract 同步检查 ===")
    print(
        f"Schema v{manifest['GWENT_RL_SCHEMA_VERSION']} / "
        f"Action grammar v{manifest['GWENT_RL_ACTION_GRAMMAR_VERSION']} / "
        f"Reward config v{manifest['GWENT_RL_REWARD_CONFIG_VERSION']}"
    )
    print(
        "维度: "
        f"global={c['GWENT_RL_GLOBAL_FEATURE_COUNT']}, "
        f"objects={c['GWENT_RL_MAX_OBJECTS']}x{c['GWENT_RL_OBJECT_FEATURE_COUNT']}, "
        f"options={c['GWENT_RL_MAX_OPTIONS']}x{c['GWENT_RL_OPTION_FEATURE_COUNT']}, "
        f"prefix={c['GWENT_RL_MAX_PREFIX']}"
    )

    if errors:
        print("\n[失败]")
        for error in errors:
            print(f"- {error}")
        return 1

    print("[通过] 单一版本源、C/Python mirror、当前文档和关键维度一致。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
