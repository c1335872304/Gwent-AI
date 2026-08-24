#!/usr/bin/env python3
"""Create a clean source ZIP without caches, builds, models, or run outputs."""

from __future__ import annotations

import argparse
import os
import zipfile
from datetime import datetime
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]

EXCLUDED_DIRS = {
    ".git",
    ".idea",
    ".pytest_cache",
    ".mypy_cache",
    ".ruff_cache",
    "__pycache__",
    "build",
    "build-release",
    "build-debug",
    ".build",
    ".agent-build",
}

EXCLUDED_SUFFIXES = {
    ".pt",
    ".pth",
    ".ckpt",
    ".onnx",
    ".safetensors",
    ".pyc",
    ".pyo",
    ".so",
    ".dll",
    ".dylib",
    ".zip",
    ".tar",
    ".gz",
    ".7z",
    ".log",
}

EXCLUDED_FILES = {".DS_Store"}


def should_exclude(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    if any(part in EXCLUDED_DIRS for part in relative.parts[:-1]):
        return True
    if relative.parts and relative.parts[0] == "runs" and relative.as_posix() != "runs/README.md":
        return True
    if path.name in EXCLUDED_FILES:
        return True
    if path.suffix.lower() in EXCLUDED_SUFFIXES:
        return True
    return False


def archive_member_is_forbidden(name: str) -> bool:
    path = PurePosixPath(name)
    if any(part in EXCLUDED_DIRS or part == "packages" for part in path.parts[:-1]):
        return True
    if path.parts and path.parts[0] == "runs" and path.as_posix() != "runs/README.md":
        return True
    if path.name in EXCLUDED_FILES:
        return True
    if path.suffix.lower() in EXCLUDED_SUFFIXES:
        return True
    return False


def human_size(size: int) -> str:
    value = float(size)
    for unit in ["B", "KB", "MB", "GB"]:
        if value < 1024:
            return f"{value:.2f} {unit}"
        value /= 1024
    return f"{value:.2f} TB"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        help="output ZIP path; default: packages/gwent_project_<timestamp>.zip",
    )
    parser.add_argument("--quiet", action="store_true", help="do not print every added file")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    if args.output:
        output_file = Path(args.output).expanduser()
        if not output_file.is_absolute():
            output_file = ROOT / output_file
        output_file = output_file.resolve()
    else:
        output_file = ROOT / "packages" / f"gwent_project_{timestamp}.zip"
    output_file.parent.mkdir(parents=True, exist_ok=True)

    files_added = 0
    files_skipped = 0
    original_size = 0

    print("=" * 60)
    print("Gwent Project Packager")
    print("=" * 60)
    print("Project :", ROOT)
    print("Output  :", output_file)

    with zipfile.ZipFile(output_file, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for current_root, dirs, files in os.walk(ROOT):
            current_root = Path(current_root)
            dirs[:] = sorted(d for d in dirs if d not in EXCLUDED_DIRS and d != "packages")
            for filename in sorted(files):
                path = current_root / filename
                if path.resolve() == output_file or should_exclude(path):
                    files_skipped += 1
                    continue
                relative = path.relative_to(ROOT)
                try:
                    size = path.stat().st_size
                except OSError:
                    files_skipped += 1
                    continue
                original_size += size
                if not args.quiet:
                    print(f"ADD  {relative}")
                zf.write(path, arcname=relative.as_posix())
                files_added += 1

    with zipfile.ZipFile(output_file, "r") as zf:
        bad = [name for name in zf.namelist() if archive_member_is_forbidden(name)]
    if bad:
        output_file.unlink(missing_ok=True)
        raise RuntimeError("forbidden files entered source ZIP: " + ", ".join(bad[:20]))

    zip_size = output_file.stat().st_size
    print("=" * 60)
    print("打包完成")
    print("=" * 60)
    print("文件数量 :", files_added)
    print("跳过数量 :", files_skipped)
    print("原始大小 :", human_size(original_size))
    print("ZIP 大小 :", human_size(zip_size))
    print("[PASS] archive cleanliness check")
    print(output_file)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
