#!/usr/bin/env python3
"""Build and package all release platforms."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from release_common import (
    DEFAULT_DIST_DIR,
    VERSION_FILE,
    add_shared_args,
    artifact_search_dirs,
    build,
    capture,
    client_spec,
    configure,
    normalize_args,
    package,
    persist_pending_version_bump,
    rel,
    resolve_release_version,
    resolve_required_artifacts,
    server_spec,
    spec_with_overrides,
    unique_targets,
    validate_path_component,
    validate_resources_available,
    write_text,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    add_shared_args(parser, default_dist_dir=DEFAULT_DIST_DIR)
    parser.add_argument(
        "--platform",
        action="append",
        choices=("client", "server"),
        help="Platform to release. Repeat to select multiple. Default: client and server",
    )
    parser.add_argument("--client-package-name", default=client_spec().package_name)
    parser.add_argument("--server-package-name", default=server_spec().package_name)
    parser.add_argument("--version-file", type=Path, default=VERSION_FILE)
    parser.add_argument("--version", dest="release_version_override", default="", help="Override release version")
    parser.add_argument(
        "--bump-version",
        choices=("major", "minor", "patch"),
        default="",
        help="Bump and persist the unified version file before packaging",
    )
    args = normalize_args(parser.parse_args())
    args.version_file = args.version_file.resolve()
    return args


def selected_specs(args: argparse.Namespace) -> list:
    selected = list(dict.fromkeys(args.platform or ["client", "server"]))
    specs = []
    if "client" in selected:
        specs.append(
            spec_with_overrides(
                client_spec(),
                validate_path_component(args.client_package_name, "--client-package-name"),
                args.version_file,
            )
        )
    if "server" in selected:
        specs.append(
            spec_with_overrides(
                server_spec(),
                validate_path_component(args.server_package_name, "--server-package-name"),
                args.version_file,
            )
        )
    return specs


def component_args(args: argparse.Namespace, spec, release_version: str) -> argparse.Namespace:
    component = argparse.Namespace(**vars(args))
    component.package_name = spec.package_name
    component.version_file = spec.version_file
    component.release_version = release_version
    component.release_version_override = ""
    component.bump_version = ""
    return component


def write_all_manifest(args: argparse.Namespace, packaged: list[tuple[object, argparse.Namespace, Path]]) -> None:
    manifest_path = args.dist_dir / args.config / "all_platforms_manifest.json"
    manifest = {
        "config": args.config,
        "version": packaged[0][1].release_version if packaged else "",
        "version_file": rel(args.version_file),
        "git": {
            "commit": capture(["git", "rev-parse", "--short", "HEAD"]),
            "dirty": bool(capture(["git", "status", "--short"])),
        },
        "platforms": [
            {
                "platform": spec.kind,
                "package": component.package_name,
                "version": component.release_version,
                "path": rel(path),
            }
            for spec, component, path in packaged
        ],
    }
    write_text(manifest_path, json.dumps(manifest, indent=2) + "\n")
    print(f"[release] all-platform manifest: {manifest_path}")


def main() -> int:
    args = parse_args()
    specs = selected_specs(args)

    if not args.skip_configure:
        configure(args)
    if not args.skip_build:
        build(args, unique_targets(specs))

    search_dirs = artifact_search_dirs(args)
    for spec in specs:
        resolve_required_artifacts(spec, search_dirs)
    validate_resources_available()
    release_version = resolve_release_version(args, persist_bump=False)
    print(f"[release] version: {release_version}")

    packaged = []
    for spec in specs:
        component = component_args(args, spec, release_version)
        path = package(spec, component)
        packaged.append((spec, component, path))

    write_all_manifest(args, packaged)
    args.release_version = release_version
    persist_pending_version_bump(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
