from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, replace
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / "src"
ARTIFACTS_DIR = ROOT / "artifacts"
DEFAULT_BIN_DIR = ARTIFACTS_DIR / "bin"
DEFAULT_LIB_DIR = ARTIFACTS_DIR / "lib"
DEFAULT_DIST_DIR = ARTIFACTS_DIR / "release"
RESOURCES_DIR = ROOT / "resources"
VERSION_FILE = ROOT / "scripts" / "VERSION"

SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\."
    r"(0|[1-9]\d*)\."
    r"(0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z.-]+)?"
    r"(?:\+[0-9A-Za-z.-]+)?$"
)
PLAIN_SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
SAFE_COMPONENT_RE = re.compile(r"^[A-Za-z0-9._-]+$")


@dataclass(frozen=True)
class ReleaseSpec:
    kind: str
    package_name: str
    version_file: Path
    build_targets: tuple[str, ...]
    artifact_names: tuple[str, ...]
    symbol_names: tuple[str, ...]


def is_windows() -> bool:
    return os.name == "nt"


def target_is_windows(args: argparse.Namespace) -> bool:
    target_os = getattr(args, "target_os", "")
    if target_os:
        return target_os.lower() == "windows"
    return is_windows()


def exe_name(base: str) -> str:
    return f"{base}.exe" if is_windows() else base


def shared_library_name(base: str) -> str:
    if is_windows():
        return f"{base}.dll"
    if sys.platform == "darwin":
        return f"lib{base}.dylib"
    return f"lib{base}.so"


def server_spec() -> ReleaseSpec:
    return ReleaseSpec(
        kind="server",
        package_name="GameCloudServer",
        version_file=VERSION_FILE,
        build_targets=("GameServer",),
        artifact_names=(exe_name("GameServer"),),
        symbol_names=("GameServer.pdb",) if is_windows() else (),
    )


def client_spec() -> ReleaseSpec:
    return ReleaseSpec(
        kind="client",
        package_name="GameCloudClient",
        version_file=VERSION_FILE,
        build_targets=("GameClient", "GameClientApp"),
        artifact_names=(exe_name("GameClientApp"), shared_library_name("GameClient")),
        symbol_names=("GameClientApp.pdb", "GameClient.pdb") if is_windows() else (),
    )


def full_spec() -> ReleaseSpec:
    return ReleaseSpec(
        kind="full",
        package_name="GameCloud",
        version_file=VERSION_FILE,
        build_targets=("GameClient", "GameClientApp", "GameServer"),
        artifact_names=(exe_name("GameClientApp"), shared_library_name("GameClient"), exe_name("GameServer")),
        symbol_names=("GameClientApp.pdb", "GameClient.pdb", "GameServer.pdb") if is_windows() else (),
    )


def run(cmd: list[str], cwd: Path = ROOT) -> None:
    print("[release]", " ".join(str(item) for item in cmd))
    subprocess.run(cmd, cwd=str(cwd), check=True)


def capture(cmd: list[str], cwd: Path = ROOT) -> str:
    try:
        result = subprocess.run(
            cmd,
            cwd=str(cwd),
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return ""
    return result.stdout.strip()


def command_succeeds(cmd: list[str], cwd: Path = ROOT) -> bool:
    try:
        result = subprocess.run(
            cmd,
            cwd=str(cwd),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except OSError:
        return False
    return result.returncode == 0


def rel(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(resolved)


def validate_path_component(value: str, label: str) -> str:
    if not value or not SAFE_COMPONENT_RE.fullmatch(value):
        raise ValueError(f"{label} must contain only letters, numbers, dot, underscore, or dash: {value!r}")
    return value


def validate_version(version: str) -> str:
    version = version.strip()
    if not SEMVER_RE.fullmatch(version):
        raise ValueError(
            "release version must be SemVer, for example 1.2.3, "
            "1.2.3-beta.1, or 1.2.3+build.5"
        )
    return version


def read_version(path: Path) -> str:
    if not path.is_file():
        raise FileNotFoundError(f"version file not found: {path}")
    return validate_version(path.read_text(encoding="utf-8").strip())


def write_version(path: Path, version: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(validate_version(version) + "\n", encoding="utf-8", newline="\n")


def ensure_version_file_tracked(path: Path) -> None:
    if not path.is_relative_to(ROOT):
        raise RuntimeError(f"version file must be inside the repository: {path}")
    if not command_succeeds(["git", "ls-files", "--error-unmatch", "--", rel(path)]):
        raise RuntimeError(f"version file must be tracked by git before releasing: {rel(path)}")


def bump_version(version: str, part: str) -> str:
    match = PLAIN_SEMVER_RE.fullmatch(version)
    if not match:
        raise ValueError("--bump-version requires a plain MAJOR.MINOR.PATCH version")

    major, minor, patch = (int(item) for item in match.groups())
    if part == "major":
        major += 1
        minor = 0
        patch = 0
    elif part == "minor":
        minor += 1
        patch = 0
    elif part == "patch":
        patch += 1
    else:
        raise ValueError(f"unsupported version bump: {part}")
    return f"{major}.{minor}.{patch}"


def resolve_release_version(args: argparse.Namespace, *, persist_update: bool = True) -> str:
    ensure_version_file_tracked(args.version_file)

    if args.release_version_override and args.bump_version:
        raise ValueError("--version and --bump-version cannot be used together")

    current = read_version(args.version_file)
    if args.release_version_override:
        next_version = validate_version(args.release_version_override)
        if next_version == current:
            raise ValueError(f"--version must differ from the current release version: {current}")
    else:
        next_version = bump_version(current, args.bump_version or "patch")

    if persist_update:
        write_version(args.version_file, next_version)
        print(f"[release] updated version: {current} -> {next_version} ({rel(args.version_file)})")
    else:
        args.pending_version_update_from = current
    return next_version


def persist_pending_version_update(args: argparse.Namespace) -> None:
    previous = getattr(args, "pending_version_update_from", "")
    if not previous:
        return
    write_version(args.version_file, args.release_version)
    print(f"[release] updated version: {previous} -> {args.release_version} ({rel(args.version_file)})")
    print(f"[release] commit the version file: git add {rel(args.version_file)} && git commit")
    args.pending_version_update_from = ""


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


def build(args: argparse.Namespace, targets: tuple[str, ...]) -> None:
    cmd = [
        "cmake",
        "--build",
        str(args.build_dir),
        "--config",
        args.config,
        "--target",
        *targets,
        "--parallel",
    ]
    run(cmd)


def candidate_dirs(root_dir: Path, config: str) -> list[Path]:
    root = root_dir.resolve()
    if root.name.lower() == config.lower():
        return [root]
    return [root / config, root]


def artifact_search_dirs(args: argparse.Namespace) -> list[Path]:
    dirs: list[Path] = []
    for root_dir in (args.bin_dir, args.lib_dir):
        for candidate in candidate_dirs(root_dir, args.config):
            if candidate not in dirs:
                dirs.append(candidate)
    return dirs


def find_artifact(name: str, search_dirs: list[Path]) -> Path | None:
    for directory in search_dirs:
        path = directory / name
        if path.is_file():
            return path
    return None


def resolve_required_artifacts(spec: ReleaseSpec, search_dirs: list[Path]) -> list[tuple[str, Path]]:
    resolved: list[tuple[str, Path]] = []
    missing: list[str] = []
    for name in spec.artifact_names:
        source = find_artifact(name, search_dirs)
        if source is None:
            missing.append(name)
        else:
            resolved.append((name, source))

    if missing:
        checked = "\n  ".join(str(path) for path in search_dirs)
        raise FileNotFoundError(
            f"required {spec.kind} release artifacts not found: "
            + ", ".join(missing)
            + "\nchecked:\n  "
            + checked
        )
    return resolved


def ensure_cleanable(path: Path) -> None:
    resolved = path.resolve()
    allowed = ARTIFACTS_DIR.resolve()
    if resolved == allowed or allowed not in resolved.parents:
        raise RuntimeError(
            f"refusing to clean outside artifacts/: {resolved}; "
            "use --no-clean-dist for custom output directories"
        )


def copy_file(src: Path, dst: Path) -> None:
    if not src.exists():
        raise FileNotFoundError(f"required artifact not found: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def count_files(path: Path) -> int:
    return sum(1 for item in path.rglob("*") if item.is_file())


def validate_resources_available() -> None:
    if not RESOURCES_DIR.is_dir():
        raise FileNotFoundError(f"resources directory not found: {RESOURCES_DIR}")


def copy_resources(dist: Path) -> int:
    validate_resources_available()

    resources_dst = dist / "resources"
    if resources_dst.exists():
        shutil.rmtree(resources_dst)
    shutil.copytree(
        RESOURCES_DIR,
        resources_dst,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
    )
    return count_files(resources_dst)


def copy_symbols(
    spec: ReleaseSpec,
    args: argparse.Namespace,
    dist: Path,
    search_dirs: list[Path],
) -> list[dict[str, object]]:
    if not args.include_symbols:
        return []

    copied: list[dict[str, object]] = []
    for name in spec.symbol_names:
        source = find_artifact(name, search_dirs)
        if source is None:
            print(f"[release] optional {spec.kind} symbol not found: {name}")
            continue
        target = dist / name
        copy_file(source, target)
        copied.append(
            {
                "name": name,
                "source": rel(source),
                "size": target.stat().st_size,
            }
        )
    return copied


def target_exe_name(base: str, args: argparse.Namespace) -> str:
    return f"{base}.exe" if target_is_windows(args) else base


def write_server_launcher(dist: Path, args: argparse.Namespace) -> None:
    if target_is_windows(args):
        write_text(
            dist / "run_server.bat",
            '@echo off\ncd /d "%~dp0"\nGameServer.exe --config_dir=resources/config %*\n',
        )
        return
    write_text(
        dist / "run_server.sh",
        f'#!/usr/bin/env sh\ncd "$(dirname "$0")"\n./{target_exe_name("GameServer", args)} --config_dir=resources/config "$@"\n',
    )
    os.chmod(dist / "run_server.sh", 0o755)


def write_client_launcher(dist: Path, args: argparse.Namespace) -> None:
    if target_is_windows(args):
        write_text(
            dist / "run_client.bat",
            '@echo off\ncd /d "%~dp0"\nGameClientApp.exe --config_dir=resources/config %*\n',
        )
        return
    write_text(
        dist / "run_client.sh",
        f'#!/usr/bin/env sh\ncd "$(dirname "$0")"\n./{target_exe_name("GameClientApp", args)} --config_dir=resources/config "$@"\n',
    )
    os.chmod(dist / "run_client.sh", 0o755)


def write_launchers(spec: ReleaseSpec, dist: Path, args: argparse.Namespace) -> None:
    if spec.kind in ("server", "full"):
        write_server_launcher(dist, args)

    if spec.kind in ("client", "full"):
        write_client_launcher(dist, args)

    if spec.kind in ("client", "server", "full"):
        return

    raise ValueError(f"unsupported release kind: {spec.kind}")


def launcher_name(kind: str, args: argparse.Namespace) -> str:
    extension = "bat" if target_is_windows(args) else "sh"
    return f"run_{kind}.{extension}"


def launcher_command(kind: str, args: argparse.Namespace) -> str:
    name = launcher_name(kind, args)
    return name if target_is_windows(args) else f"./{name}"


def write_readme(
    spec: ReleaseSpec,
    dist: Path,
    args: argparse.Namespace,
    artifact_names: list[str],
) -> None:
    if spec.kind == "full":
        commands = [
            f"{launcher_command('server', args)} --env=production --instance_id=shard-a --admin_port=18081",
            f"{launcher_command('client', args)} --log_prefix=ClientA",
        ]
        runtime_args = [
            "Server:",
            "`--env=<development|staging|production>`",
            "`--instance_id=<id>`",
            "`--pid_file=<path>` / `--disable_pid_file`",
            "`--admin_port=<port>`",
            "`--log_prefix=<name>`",
            "`--scripts_dir=<dir>`",
            "Client:",
            "`--log_prefix=<name>`",
            "`--script=<file>`",
            "`--duration_ms=<ms>`",
            "`--tick_ms=<ms>`",
        ]
        component_note = "client and server"
    elif spec.kind == "server":
        if target_is_windows(args):
            commands = ["run_server.bat --env=production --log_prefix=ShardA"]
        else:
            commands = ["./run_server.sh --env=production --log_prefix=ShardA"]
        runtime_args = [
            "`--env=<development|staging|production>`",
            "`--instance_id=<id>`",
            "`--pid_file=<path>` / `--disable_pid_file`",
            "`--admin_port=<port>`",
            "`--log_prefix=<name>`",
            "`--scripts_dir=<dir>`",
        ]
        component_note = "server"
    else:
        commands = ["run_client.bat --log_prefix=ClientA"] if target_is_windows(args) else ["./run_client.sh --log_prefix=ClientA"]
        runtime_args = [
            "`--log_prefix=<name>`",
            "`--script=<file>`",
            "`--duration_ms=<ms>`",
            "`--tick_ms=<ms>`",
        ]
        component_note = "client"

    code_fence = "bat" if target_is_windows(args) else "sh"
    readme_lines = [
        f"# {spec.kind.title()} Release Artifacts",
        "",
        f"Package: `{args.package_name}`",
        f"Version: `{args.release_version}`",
        f"Config: `{args.config}`",
        f"Component: `{spec.kind}`",
        "",
        "Start command:",
        "",
        f"```{code_fence}",
        *commands,
        "```",
        "",
        "Packaged runtime artifacts:",
        "",
        *[f"- `{name}`" for name in artifact_names],
        "",
        "Packaged resources are copied from `resources/` into `resources/`.",
        f"This package contains {component_note} runtime files in the same directory.",
        "Launcher scripts pass all extra arguments through to the executable.",
        "",
        "Useful runtime arguments:",
        "",
        *[f"- {item}" for item in runtime_args],
        "",
        "See `manifest.json` for package metadata.",
        "",
    ]
    write_text(dist / "README.md", "\n".join(readme_lines))


def write_manifest(
    spec: ReleaseSpec,
    dist: Path,
    args: argparse.Namespace,
    artifacts: list[dict[str, object]],
    symbols: list[dict[str, object]],
    resource_file_count: int,
) -> None:
    manifest = {
        "component": spec.kind,
        "package": args.package_name,
        "version": args.release_version,
        "version_file": rel(args.version_file),
        "config": args.config,
        "created_at_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "git": {
            "commit": capture(["git", "rev-parse", "--short", "HEAD"]),
            "dirty": bool(capture(["git", "status", "--short"])),
        },
        "build": {
            "build_dir": rel(args.build_dir),
            "targets": list(spec.build_targets),
            "requested_options": {
                "with_mongodb": args.with_mongodb,
                "with_physics": args.with_physics,
                "with_profiler": args.with_profiler,
            },
        },
        "artifacts": artifacts,
        "symbols": symbols,
        "resources": {
            "source": rel(RESOURCES_DIR),
            "file_count": resource_file_count,
        },
    }
    write_text(dist / "manifest.json", json.dumps(manifest, indent=2) + "\n")


def create_archive(dist: Path) -> Path:
    archive_base = dist.parent / dist.name
    archive_path = Path(f"{archive_base}.zip")
    if archive_path.exists():
        archive_path.unlink()
    shutil.make_archive(str(archive_base), "zip", root_dir=dist.parent, base_dir=dist.name)
    print(f"[release] archived: {archive_path}")
    return archive_path


def release_dist_dir(spec: ReleaseSpec, args: argparse.Namespace) -> Path:
    base = args.dist_dir / args.config
    if args.flat_dist:
        return base / spec.kind
    return base / f"{args.package_name}-{args.release_version}"


def package(spec: ReleaseSpec, args: argparse.Namespace) -> Path:
    search_dirs = artifact_search_dirs(args)
    required_artifacts = resolve_required_artifacts(spec, search_dirs)
    validate_resources_available()
    existing_version = getattr(args, "release_version", "")
    if existing_version:
        args.release_version = validate_version(existing_version)
    else:
        args.release_version = resolve_release_version(args, persist_update=False)
    print(f"[release] {spec.kind} version: {args.release_version}")
    dist = release_dist_dir(spec, args)

    if args.clean_dist and dist.exists():
        ensure_cleanable(dist)
        shutil.rmtree(dist)

    dist.mkdir(parents=True, exist_ok=True)

    packaged_artifacts: list[dict[str, object]] = []
    for name, source in required_artifacts:
        target = dist / name
        copy_file(source, target)
        packaged_artifacts.append(
            {
                "name": name,
                "source": rel(source),
                "size": target.stat().st_size,
            }
        )

    symbols = copy_symbols(spec, args, dist, search_dirs)
    resource_file_count = copy_resources(dist)
    write_launchers(spec, dist, args)
    write_readme(spec, dist, args, [item["name"] for item in packaged_artifacts])
    write_manifest(spec, dist, args, packaged_artifacts, symbols, resource_file_count)

    if args.archive:
        create_archive(dist)

    persist_pending_version_update(args)
    print(f"[release] packaged {spec.kind}: {dist}")
    return dist


def add_shared_args(parser: argparse.ArgumentParser, default_dist_dir: Path = DEFAULT_DIST_DIR) -> None:
    parser.add_argument("--config", default="Release", help="CMake build config")
    parser.add_argument("--build-dir", type=Path, default=ARTIFACTS_DIR / "build-release")
    parser.add_argument("--bin-dir", type=Path, default=DEFAULT_BIN_DIR, help="Binary artifact root or config directory")
    parser.add_argument("--lib-dir", type=Path, default=DEFAULT_LIB_DIR, help="Library artifact root or config directory")
    parser.add_argument("--dist-dir", type=Path, default=default_dist_dir)
    parser.add_argument("--generator", default="", help="Optional CMake generator")
    parser.add_argument("--arch", default="", help="Optional CMake architecture, e.g. x64")
    parser.add_argument("--cmake-arg", action="append", default=[], help="Extra CMake configure arg")
    parser.add_argument("--skip-configure", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--no-clean-dist", dest="clean_dist", action="store_false")
    parser.add_argument("--include-symbols", action="store_true", help="Copy optional debug symbols when present")
    parser.add_argument("--archive", action="store_true", help="Create a zip archive next to the package directory")
    parser.add_argument("--flat-dist", action="store_true", help="Package under <dist-dir>/<Config>/<component>")
    parser.add_argument("--with-mongodb", action="store_true")
    parser.add_argument("--with-physics", action="store_true")
    parser.add_argument("--with-profiler", action="store_true")


def add_component_args(parser: argparse.ArgumentParser, spec: ReleaseSpec) -> None:
    parser.add_argument("--package-name", default=spec.package_name)
    parser.add_argument("--version-file", type=Path, default=spec.version_file)
    parser.add_argument("--version", dest="release_version_override", default="", help="Override release version")
    parser.add_argument(
        "--bump-version",
        choices=("major", "minor", "patch"),
        default="",
        help="Version part to bump after a successful release. Default: patch",
    )


def normalize_args(args: argparse.Namespace) -> argparse.Namespace:
    args.build_dir = args.build_dir.resolve()
    args.bin_dir = args.bin_dir.resolve()
    args.lib_dir = args.lib_dir.resolve()
    args.dist_dir = args.dist_dir.resolve()
    args.config = validate_path_component(args.config, "--config")
    return args


def normalize_component_args(args: argparse.Namespace) -> argparse.Namespace:
    normalize_args(args)
    args.version_file = args.version_file.resolve()
    args.package_name = validate_path_component(args.package_name, "--package-name")
    return args


def component_main(spec: ReleaseSpec, description: str) -> int:
    parser = argparse.ArgumentParser(description=description)
    add_shared_args(parser)
    add_component_args(parser, spec)
    args = normalize_component_args(parser.parse_args())

    if not args.skip_configure:
        configure(args)
    if not args.skip_build:
        build(args, spec.build_targets)

    package(spec, args)
    return 0


def spec_with_overrides(spec: ReleaseSpec, package_name: str, version_file: Path) -> ReleaseSpec:
    return replace(
        spec,
        package_name=validate_path_component(package_name, "--package-name"),
        version_file=version_file.resolve(),
    )


def unique_targets(specs: list[ReleaseSpec]) -> tuple[str, ...]:
    targets: list[str] = []
    for spec in specs:
        for target in spec.build_targets:
            if target not in targets:
                targets.append(target)
    return tuple(targets)
