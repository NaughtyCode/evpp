#!/usr/bin/env python3
"""Build and package the server/client release artifacts.

The script builds:
  - GameServer
  - GameClient
  - GameClientApp

It then creates a self-contained release folder with resources and launcher
scripts.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / "src"


def is_windows() -> bool:
    return os.name == "nt"


def exe_name(base: str) -> str:
    return f"{base}.exe" if is_windows() else base


def dll_name(base: str) -> str:
    if is_windows():
        return f"{base}.dll"
    if sys.platform == "darwin":
        return f"lib{base}.dylib"
    return f"lib{base}.so"


def run(cmd: list[str], cwd: Path = ROOT) -> None:
    print("[release]", " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd), check=True)


def configure(args: argparse.Namespace) -> None:
    cmd = [
        "cmake",
        "-S",
        str(SRC_DIR),
        "-B",
        str(args.build_dir),
        f"-DENGINE_MONGODB_ENABLED={'ON' if args.with_mongodb else 'OFF'}",
        f"-DENGINE_PHYSICS_ENABLED={'ON' if args.with_physics else 'OFF'}",
        f"-DENGINE_PROFILER_ENABLED={'ON' if args.with_profiler else 'OFF'}",
        "-DBUILD_TESTING=OFF",
    ]
    if args.generator:
        cmd.extend(["-G", args.generator])
    if args.arch:
        cmd.extend(["-A", args.arch])
    for item in args.cmake_arg:
        cmd.append(item)
    run(cmd)


def build(args: argparse.Namespace) -> None:
    cmd = [
        "cmake",
        "--build",
        str(args.build_dir),
        "--config",
        args.config,
        "--target",
        "GameServer",
        "GameClient",
        "GameClientApp",
        "--parallel",
    ]
    run(cmd)


def copy_file(src: Path, dst: Path) -> None:
    if not src.exists():
        raise FileNotFoundError(f"required artifact not found: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def package(args: argparse.Namespace) -> Path:
    source_bin = ROOT / "artifacts" / "bin" / args.config
    dist = args.dist_dir / args.config

    if args.clean_dist and dist.exists():
        resolved = dist.resolve()
        allowed = (ROOT / "artifacts").resolve()
        if allowed not in resolved.parents:
            raise RuntimeError(f"refusing to delete outside artifacts/: {resolved}")
        shutil.rmtree(dist)

    dist.mkdir(parents=True, exist_ok=True)
    copy_file(source_bin / exe_name("GameServer"), dist / exe_name("GameServer"))
    copy_file(source_bin / exe_name("GameClientApp"), dist / exe_name("GameClientApp"))
    copy_file(source_bin / dll_name("GameClient"), dist / dll_name("GameClient"))

    resources_src = ROOT / "resources"
    resources_dst = dist / "resources"
    if resources_dst.exists():
        shutil.rmtree(resources_dst)
    shutil.copytree(resources_src, resources_dst)

    if is_windows():
        write_text(
            dist / "run_server.bat",
            '@echo off\ncd /d "%~dp0"\nGameServer.exe --config_dir=resources/config %*\n',
        )
        write_text(
            dist / "run_client.bat",
            '@echo off\ncd /d "%~dp0"\nGameClientApp.exe --config_dir=resources/config %*\n',
        )
    else:
        write_text(
            dist / "run_server.sh",
            '#!/usr/bin/env sh\ncd "$(dirname "$0")"\n./GameServer --config_dir=resources/config "$@"\n',
        )
        write_text(
            dist / "run_client.sh",
            '#!/usr/bin/env sh\ncd "$(dirname "$0")"\n./GameClientApp --config_dir=resources/config "$@"\n',
        )
        os.chmod(dist / "run_server.sh", 0o755)
        os.chmod(dist / "run_client.sh", 0o755)

    readme_lines = [
        "# Release Artifacts",
        "",
        "Start the server first, then the client.",
        "",
        "Windows:",
        "",
        "```bat",
        "run_server.bat",
        "run_client.bat",
        "```",
        "",
        "Unix:",
        "",
        "```sh",
        "./run_server.sh",
        "./run_client.sh",
        "```",
        "",
        "The package includes the base resources copied from `resources/`.",
        "Configure `resources/config` or pass runtime options for project-specific scripts.",
        "",
    ]
    write_text(dist / "README.md", "\n".join(readme_lines))

    print(f"[release] packaged: {dist}")
    return dist


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="Release", help="CMake build config")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "artifacts" / "build-release")
    parser.add_argument("--dist-dir", type=Path, default=ROOT / "artifacts" / "release")
    parser.add_argument("--generator", default="", help="Optional CMake generator")
    parser.add_argument("--arch", default="", help="Optional CMake architecture, e.g. x64")
    parser.add_argument("--cmake-arg", action="append", default=[], help="Extra CMake configure arg")
    parser.add_argument("--skip-configure", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--no-clean-dist", dest="clean_dist", action="store_false")
    parser.add_argument("--with-mongodb", action="store_true")
    parser.add_argument("--with-physics", action="store_true")
    parser.add_argument("--with-profiler", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.build_dir = args.build_dir.resolve()
    args.dist_dir = args.dist_dir.resolve()

    if not args.skip_configure:
        configure(args)
    if not args.skip_build:
        build(args)

    package(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
