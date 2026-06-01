#!/usr/bin/env python3
"""Build and package the server/client release artifacts.

The script builds:
  - GameServer
  - GameClient
  - GameClientApp

It then creates a self-contained release folder with resources and launcher
scripts. Use --smoke to start server first, then client, and verify that the
Lua CS handshake completes.
"""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
import time
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

    write_text(
        dist / "README.md",
        "\n".join(
            [
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
                "The CS handshake and gameplay round trip are implemented in Lua:",
                "",
                "- `resources/script/shared/cs_protocol.lua`",
                "- `resources/script/server/init.lua`",
                "- `resources/script/client/init.lua`",
                "",
            ]
        ),
    )

    print(f"[release] packaged: {dist}")
    return dist


def wait_for_port(host: str, port: int, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.25):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def read_log(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def smoke(args: argparse.Namespace, dist: Path) -> None:
    host = "127.0.0.1"
    port = args.port
    server_log = dist / "server_smoke.log"
    client_log = dist / "client_smoke.log"

    for path in (server_log, client_log):
        if path.exists():
            path.unlink()

    server_cmd = [
        str(dist / exe_name("GameServer")),
        "--config_dir=resources/config",
        "--log_prefix=GameServerSmoke",
    ]
    client_cmd = [
        str(dist / exe_name("GameClientApp")),
        "--config_dir=resources/config",
        "--log_prefix=GameClientSmoke",
        f"--duration_ms={args.client_duration_ms}",
        "--tick_ms=16",
    ]

    server_proc: subprocess.Popen[str] | None = None
    try:
        with server_log.open("w", encoding="utf-8", newline="\n") as server_out:
            print("[release] starting server")
            server_proc = subprocess.Popen(
                server_cmd,
                cwd=str(dist),
                stdout=server_out,
                stderr=subprocess.STDOUT,
                text=True,
            )

        if not wait_for_port(host, port, args.smoke_timeout):
            raise RuntimeError(f"server did not listen on {host}:{port}")

        with client_log.open("w", encoding="utf-8", newline="\n") as client_out:
            print("[release] starting client")
            client_result = subprocess.run(
                client_cmd,
                cwd=str(dist),
                stdout=client_out,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=args.smoke_timeout,
            )
        if client_result.returncode != 0:
            raise RuntimeError(f"client exited with code {client_result.returncode}")

        combined = read_log(server_log) + "\n" + read_log(client_log)
        if "CS_CLIENT_SUCCESS" not in combined:
            raise RuntimeError(
                "smoke run did not observe CS_CLIENT_SUCCESS; see server_smoke.log and client_smoke.log"
            )
        print("[release] smoke passed: client connected to server and completed Lua CS round trip")
    finally:
        if server_proc and server_proc.poll() is None:
            server_proc.terminate()
            try:
                server_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server_proc.kill()
                server_proc.wait(timeout=5)


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
    parser.add_argument("--smoke", action="store_true", help="Start server then client and verify Lua CS communication")
    parser.add_argument("--smoke-timeout", type=float, default=15.0)
    parser.add_argument("--client-duration-ms", type=int, default=4000)
    parser.add_argument("--port", type=int, default=7777)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.build_dir = args.build_dir.resolve()
    args.dist_dir = args.dist_dir.resolve()

    if not args.skip_configure:
        configure(args)
    if not args.skip_build:
        build(args)

    dist = package(args)
    if args.smoke:
        smoke(args, dist)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
