#!/usr/bin/env python3
"""Build and package Android release artifacts for all supported ABIs."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from release_common import (
    ARTIFACTS_DIR,
    DEFAULT_DIST_DIR,
    VERSION_FILE,
    ReleaseSpec,
    add_shared_args,
    artifact_search_dirs,
    build,
    capture,
    configure,
    normalize_args,
    package,
    persist_pending_version_update,
    rel,
    resolve_release_version,
    resolve_required_artifacts,
    validate_path_component,
    validate_resources_available,
    write_text,
)


DEFAULT_ANDROID_ABIS = ("arm64-v8a", "armeabi-v7a", "x86", "x86_64")
DEFAULT_ANDROID_PLATFORM = "android-24"


def android_full_spec(package_name: str, version_file: Path) -> ReleaseSpec:
    return ReleaseSpec(
        kind="full",
        package_name=validate_path_component(package_name, "--package-name"),
        version_file=version_file.resolve(),
        build_targets=("GameClient", "GameClientApp", "GameServer"),
        artifact_names=("GameClientApp", "libGameClient.so", "GameServer"),
        symbol_names=(),
    )


def ndk_candidates() -> list[Path]:
    candidates: list[Path] = []
    for name in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK"):
        value = os.environ.get(name)
        if value:
            candidates.append(Path(value))

    for name in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        value = os.environ.get(name)
        if not value:
            continue
        ndk_dir = Path(value) / "ndk"
        if ndk_dir.is_dir():
            candidates.extend(sorted((item for item in ndk_dir.iterdir() if item.is_dir()), reverse=True))

    repo_ndk_dir = ARTIFACTS_DIR / "android-sdk" / "ndk"
    if repo_ndk_dir.is_dir():
        candidates.extend(sorted((item for item in repo_ndk_dir.iterdir() if item.is_dir()), reverse=True))

    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        ndk_dir = Path(local_app_data) / "Android" / "Sdk" / "ndk"
        if ndk_dir.is_dir():
            candidates.extend(sorted((item for item in ndk_dir.iterdir() if item.is_dir()), reverse=True))

    seen: set[Path] = set()
    unique: list[Path] = []
    for candidate in candidates:
        resolved = candidate.expanduser().resolve()
        if resolved not in seen:
            seen.add(resolved)
            unique.append(resolved)
    return unique


def toolchain_from_ndk_root(ndk_root: Path) -> Path:
    return ndk_root / "build" / "cmake" / "android.toolchain.cmake"


def resolve_android_toolchain_file(value: Path | None) -> Path:
    if value:
        candidate = value.expanduser().resolve()
        if candidate.is_dir():
            candidate = toolchain_from_ndk_root(candidate)
        if candidate.is_file():
            return candidate
        raise FileNotFoundError(f"Android CMake toolchain file not found: {candidate}")

    for ndk_root in ndk_candidates():
        candidate = toolchain_from_ndk_root(ndk_root)
        if candidate.is_file():
            return candidate

    raise FileNotFoundError(
        "Android NDK not found. Set ANDROID_NDK_HOME or pass "
        "--android-toolchain-file=<ndk>/build/cmake/android.toolchain.cmake"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    add_shared_args(parser, default_dist_dir=DEFAULT_DIST_DIR / "android")
    parser.set_defaults(build_dir=ARTIFACTS_DIR / "build-android", generator="Ninja")
    parser.add_argument(
        "--abi",
        action="append",
        choices=DEFAULT_ANDROID_ABIS,
        help="Android ABI to build. Repeat to select multiple. Default: all supported ABIs",
    )
    parser.add_argument("--android-platform", default=DEFAULT_ANDROID_PLATFORM)
    parser.add_argument("--android-stl", default="c++_static")
    parser.add_argument("--android-toolchain-file", type=Path)
    parser.add_argument("--android-output-dir", type=Path, default=ARTIFACTS_DIR / "android")
    parser.add_argument("--package-name", default="GameCloudAndroid")
    parser.add_argument("--version-file", type=Path, default=VERSION_FILE)
    parser.add_argument("--version", dest="release_version_override", default="", help="Override release version")
    parser.add_argument(
        "--bump-version",
        choices=("major", "minor", "patch"),
        default="",
        help="Version part to bump after a successful release. Default: patch",
    )
    args = normalize_args(parser.parse_args())
    args.abis = tuple(dict.fromkeys(args.abi or DEFAULT_ANDROID_ABIS))
    args.android_output_dir = args.android_output_dir.resolve()
    args.package_name = validate_path_component(args.package_name, "--package-name")
    args.version_file = args.version_file.resolve()
    return args


def abi_args(
    args: argparse.Namespace,
    abi: str,
    release_version: str,
    toolchain_file: Path | None,
) -> argparse.Namespace:
    component = argparse.Namespace(**vars(args))
    component.build_dir = args.build_dir / "full" / abi
    component.bin_dir = args.android_output_dir / abi / "bin"
    component.lib_dir = args.android_output_dir / abi / "lib"
    if args.flat_dist:
        component.dist_dir = args.dist_dir / abi
    component.package_name = validate_path_component(f"{args.package_name}-{abi}", "--package-name")
    component.release_version = release_version
    component.release_version_override = ""
    component.bump_version = ""
    component.pending_version_update_from = ""
    component.arch = ""
    component.target_os = "android"

    cmake_args = list(args.cmake_arg)
    cmake_args.append(f"-DCMAKE_BUILD_TYPE={args.config}")
    if toolchain_file is not None:
        cmake_args.extend(
            [
                f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
                f"-DANDROID_ABI={abi}",
                f"-DANDROID_PLATFORM={args.android_platform}",
                f"-DANDROID_STL={args.android_stl}",
            ]
        )
    component.cmake_arg = cmake_args
    return component


def write_android_manifest(args: argparse.Namespace, packaged: list[tuple[str, argparse.Namespace, Path]]) -> None:
    manifest_path = args.dist_dir / args.config / "android_all_platforms_manifest.json"
    manifest = {
        "target": "android",
        "abis": list(args.abis),
        "config": args.config,
        "version": packaged[0][1].release_version if packaged else "",
        "version_file": rel(args.version_file),
        "git": {
            "commit": capture(["git", "rev-parse", "--short", "HEAD"]),
            "dirty": bool(capture(["git", "status", "--short"])),
        },
        "packages": [
            {
                "abi": abi,
                "package": component.package_name,
                "version": component.release_version,
                "path": rel(path),
            }
            for abi, component, path in packaged
        ],
    }
    write_text(manifest_path, json.dumps(manifest, indent=2) + "\n")
    print(f"[release] Android all-platform manifest: {manifest_path}")


def main() -> int:
    args = parse_args()
    spec = android_full_spec(args.package_name, args.version_file)
    toolchain_file = None if args.skip_configure else resolve_android_toolchain_file(args.android_toolchain_file)

    validate_resources_available()
    release_version = resolve_release_version(args, persist_update=False)
    print(f"[release] Android version: {release_version}")

    packaged: list[tuple[str, argparse.Namespace, Path]] = []
    for abi in args.abis:
        component = abi_args(args, abi, release_version, toolchain_file)
        abi_spec = android_full_spec(component.package_name, args.version_file)

        if not args.skip_configure:
            configure(component)
        if not args.skip_build:
            build(component, spec.build_targets)

        resolve_required_artifacts(abi_spec, artifact_search_dirs(component))
        path = package(abi_spec, component)
        packaged.append((abi, component, path))

    write_android_manifest(args, packaged)
    args.release_version = release_version
    persist_pending_version_update(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
