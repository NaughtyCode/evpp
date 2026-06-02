#!/usr/bin/env python3
"""Build and package the server release artifacts."""

from __future__ import annotations

from release_common import component_main, server_spec


def main() -> int:
    return component_main(server_spec(), __doc__ or "")


if __name__ == "__main__":
    raise SystemExit(main())
