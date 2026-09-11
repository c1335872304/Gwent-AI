#!/usr/bin/env python3
"""Build a small, safe architecture ZIP from an explicit source allowlist.

The archive is intended for architecture review, code review, or handing the
local product structure to another developer.  It is deliberately *not* a
reproducible runnable release: models, checkpoints, embeddings, build output,
node_modules, run data, secrets, and large card-reference exports are omitted.

Example:
    python scripts/package_architecture_bundle.py
    python scripts/package_architecture_bundle.py --dry-run
    python scripts/package_architecture_bundle.py --output packages/review.zip
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_ROOT = "gwent_v3_architecture"
DEFAULT_MAX_FILE_SIZE = 5 * 1024 * 1024

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".ts", ".tsx", ".css", ".html", ".sh"}
DOCUMENT_SUFFIXES = {".md"}
CONFIG_SUFFIXES = {".json", ".yaml", ".yml", ".toml", ".txt", ".conf", ".in"}
ALLOWED_SUFFIXES = SOURCE_SUFFIXES | DOCUMENT_SUFFIXES | CONFIG_SUFFIXES

# These directories are selected because they explain the product's current
# runtime architecture.  A whitelist is safer than trying to blacklist every
# possible training artifact or private local file.
TREE_RULES: dict[str, set[str]] = {
    "include": {".h", ".hpp"},
    "src": {".c", ".cc", ".cpp", ".h", ".hpp"},
    "cmake": {".in"},
    "python/src": {".py"},
    "python/tests": {".py"},
    "tests": {".c", ".cc", ".cpp", ".h", ".hpp"},
    "apps/web/backend/app": {".py"},
    "apps/web/backend/tests": {".py"},
    "apps/web/frontend/src": {".ts", ".tsx", ".css"},
    "services/teacher": {".py", ".md", ".txt"},
    "docs": {".md"},
    ".agents/skills": {".md", ".py"},
    "configs": {".yaml", ".yml", ".json", ".md"},
    "training": {".yaml", ".yml", ".json", ".md"},
}

EXACT_FILES = {
    "README.md",
    "AGENTS.md",
    "CMakeLists.txt",
    "config/rl_contract.json",
    "contracts/README.md",
    "python/pyproject.toml",
    "apps/web/backend/requirements.txt",
    "apps/web/backend/requirements-dev.txt",
    "apps/web/backend/.env.example",
    "apps/web/frontend/index.html",
    "apps/web/frontend/package.json",
    "apps/web/frontend/package-lock.json",
    "apps/web/frontend/tsconfig.json",
    "apps/web/frontend/tsconfig.app.json",
    "apps/web/frontend/tsconfig.node.json",
    "apps/web/frontend/vite.config.ts",
    "deploy/docker/Dockerfile.core",
    "deploy/docker/Dockerfile.bff",
    "deploy/docker/Dockerfile.teacher",
    "deploy/docker/Dockerfile.web",
    "deploy/docker/compose.cpu.yml",
    "deploy/docker/requirements.core.cpu.txt",
    "deploy/docker/nginx.conf",
    "deploy/docker/entrypoint-core.sh",
    "data/cards/README.md",
    "data/cards/supported_cards.json",
    "data/decks/README.md",
    "data/decks/deck_a.json",
    "data/decks/deck_b.json",
    "tools/server/human_vs_ai.py",
    "tools/server/package_project.py",
}

EXACT_PREFIXES = (
    "scripts/",
    "tools/server/tests/",
)

FORBIDDEN_PARTS = {
    ".git",
    ".idea",
    ".vscode",
    ".pytest_cache",
    ".mypy_cache",
    ".ruff_cache",
    "__pycache__",
    "build",
    "build-release",
    "build-debug",
    ".build",
    ".agent-build",
    "node_modules",
    "dist",
    "runs",
    "models",
    "artifacts",
    "packages",
}
FORBIDDEN_SUFFIXES = {
    ".pt",
    ".pth",
    ".ckpt",
    ".onnx",
    ".safetensors",
    ".npz",
    ".pyc",
    ".pyo",
    ".so",
    ".dll",
    ".dylib",
    ".exe",
    ".zip",
    ".tar",
    ".gz",
    ".7z",
    ".log",
}


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def is_forbidden(path: Path) -> bool:
    rel = path.relative_to(ROOT)
    if any(part in FORBIDDEN_PARTS for part in rel.parts):
        return True
    if path.name == ".env" or path.name.endswith(".env"):
        return True
    return path.suffix.lower() in FORBIDDEN_SUFFIXES


def has_allowed_suffix(path: Path, suffixes: set[str]) -> bool:
    return path.suffix.lower() in suffixes


def iter_tree(rule_root: str, suffixes: set[str]) -> Iterable[Path]:
    directory = ROOT / rule_root
    if not directory.exists():
        return
    for path in sorted(directory.rglob("*")):
        if path.is_file() and not is_forbidden(path) and has_allowed_suffix(path, suffixes):
            yield path


def iter_exact_prefix(prefix: str) -> Iterable[Path]:
    directory = ROOT / prefix
    if not directory.exists():
        return
    for path in sorted(directory.rglob("*")):
        if path.is_file() and not is_forbidden(path) and has_allowed_suffix(path, ALLOWED_SUFFIXES):
            yield path


def collect_files(include_tests: bool, max_file_size: int) -> tuple[list[Path], list[str]]:
    selected: set[Path] = set()
    skipped: list[str] = []

    for raw in sorted(EXACT_FILES):
        path = ROOT / raw
        if not path.is_file():
            skipped.append(f"missing allowlisted file: {raw}")
        elif is_forbidden(path):
            skipped.append(f"forbidden allowlisted file: {raw}")
        else:
            selected.add(path)

    for rule_root, suffixes in TREE_RULES.items():
        if not include_tests and rule_root in {"python/tests", "tests", "apps/web/backend/tests"}:
            continue
        selected.update(iter_tree(rule_root, suffixes))

    if include_tests:
        for prefix in EXACT_PREFIXES:
            selected.update(iter_exact_prefix(prefix))
    else:
        # The scripts themselves explain the build/contract workflow and are
        # useful even when test cases are intentionally omitted.
        selected.update(iter_exact_prefix("scripts/"))

    files: list[Path] = []
    for path in sorted(selected, key=relative):
        try:
            size = path.stat().st_size
        except OSError as exc:
            skipped.append(f"unreadable file: {relative(path)} ({exc})")
            continue
        if size > max_file_size:
            skipped.append(f"oversize file ({size} bytes): {relative(path)}")
            continue
        files.append(path)
    return files, skipped


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def human_size(size: int) -> str:
    value = float(size)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024:
            return f"{value:.2f} {unit}"
        value /= 1024
    return f"{value:.2f} TB"


def manifest(files: list[Path], skipped: list[str], max_file_size: int) -> str:
    created = datetime.now(timezone.utc).isoformat(timespec="seconds")
    lines = [
        "# Gwent AI 架构包",
        "",
        f"生成时间（UTC）：{created}",
        "",
        "这是用于架构审阅的小型源码包，不是可直接运行的发布包。",
        "",
        "## 已包含",
        "",
        "- C/C++ Core、C ABI、Python Strategy adapter；",
        "- React/FastAPI/Teacher 源码与关键测试；",
        "- 当前 Markdown 文档、Skills、RL contract、训练配置和 Docker 定义；",
        "- 受支持卡牌与两套示例卡组的轻量 JSON 定义。",
        "",
        "## 已排除",
        "",
        "- policy/checkpoint、embeddings、训练 runs、artifacts、构建产物、node_modules；",
        "- `.env`、本地密钥、缓存、日志、压缩包和超过",
        f"  {human_size(max_file_size)} 的文件；",
        "- 完整卡牌参考导出及不属于当前本地产品架构的评测产物。",
        "",
        "## 使用方式",
        "",
        "从原仓库重新生成：",
        "",
        "```powershell",
        "python scripts/package_architecture_bundle.py",
        "```",
        "",
        "## 文件清单",
        "",
    ]
    lines.extend(f"- `{relative(path)}`" for path in files)
    if skipped:
        lines.extend(["", "## 未纳入的 allowlist 文件", ""])
        lines.extend(f"- {item}" for item in skipped)
    lines.append("")
    return "\n".join(lines)


def checksum_manifest(files: list[Path]) -> str:
    return "\n".join(f"{sha256(path)}  {relative(path)}" for path in files) + "\n"


def archive_name(relative_name: str) -> str:
    return f"{ARCHIVE_ROOT}/{relative_name}"


def archive_member_is_safe(name: str) -> bool:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not path.parts or path.parts[0] != ARCHIVE_ROOT:
        return False
    relative_parts = path.parts[1:]
    if any(part in FORBIDDEN_PARTS for part in relative_parts):
        return False
    filename = path.name
    return filename != ".env" and not filename.endswith(".env") and path.suffix.lower() not in FORBIDDEN_SUFFIXES


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        help="ZIP output path; default: packages/gwent_architecture_<timestamp>.zip",
    )
    parser.add_argument("--without-tests", action="store_true", help="omit test source files")
    parser.add_argument("--dry-run", action="store_true", help="print the selected files without creating a ZIP")
    parser.add_argument("--quiet", action="store_true", help="do not print each selected file")
    parser.add_argument("--overwrite", action="store_true", help="allow replacing an existing --output ZIP")
    parser.add_argument(
        "--max-file-size-mb",
        type=float,
        default=DEFAULT_MAX_FILE_SIZE / (1024 * 1024),
        help="omit allowlisted files larger than this limit (default: 5)",
    )
    return parser.parse_args()


def output_path(args: argparse.Namespace) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    candidate = Path(args.output) if args.output else Path("packages") / f"gwent_architecture_{timestamp}.zip"
    if not candidate.is_absolute():
        candidate = ROOT / candidate
    return candidate.resolve()


def write_archive(destination: Path, files: list[Path], skipped: list[str], max_file_size: int) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix="gwent_architecture_", suffix=".zip", dir=destination.parent, delete=False) as handle:
        temporary = Path(handle.name)
    try:
        with zipfile.ZipFile(temporary, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in files:
                archive.write(path, archive_name(relative(path)))
            archive.writestr(archive_name("ARCHITECTURE_PACKAGE.md"), manifest(files, skipped, max_file_size))
            archive.writestr(archive_name("SHA256SUMS"), checksum_manifest(files))

        with zipfile.ZipFile(temporary, "r") as archive:
            invalid = [name for name in archive.namelist() if not archive_member_is_safe(name)]
            expected = {archive_name(relative(path)) for path in files}
            actual = set(archive.namelist()) - {
                archive_name("ARCHITECTURE_PACKAGE.md"),
                archive_name("SHA256SUMS"),
            }
            if invalid or actual != expected:
                raise RuntimeError(
                    "archive verification failed: "
                    + (f"unsafe members={invalid[:5]} " if invalid else "")
                    + ("file manifest differs" if actual != expected else "")
                )
        shutil.move(str(temporary), destination)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    if args.max_file_size_mb <= 0:
        raise ValueError("--max-file-size-mb must be positive")
    max_file_size = int(args.max_file_size_mb * 1024 * 1024)
    files, skipped = collect_files(include_tests=not args.without_tests, max_file_size=max_file_size)
    if not files:
        raise RuntimeError("architecture allowlist selected no files")

    destination = output_path(args)
    if destination.exists() and not args.overwrite:
        raise FileExistsError(f"output already exists: {destination}; choose another --output or pass --overwrite")

    print(f"Project : {ROOT}")
    print(f"Mode    : {'dry run' if args.dry_run else 'create ZIP'}")
    print(f"Files   : {len(files)}")
    print(f"Source  : {human_size(sum(path.stat().st_size for path in files))}")
    if skipped:
        print(f"Skipped : {len(skipped)} allowlisted file(s)")
    if not args.quiet:
        for path in files:
            print(f"ADD  {relative(path)}")
        for item in skipped:
            print(f"SKIP {item}", file=sys.stderr)

    if args.dry_run:
        return 0

    write_archive(destination, files, skipped, max_file_size)
    print(f"ZIP     : {destination}")
    print(f"Size    : {human_size(destination.stat().st_size)}")
    print("[PASS] allowlist and archive safety verification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
