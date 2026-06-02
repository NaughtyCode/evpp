#!/usr/bin/env python3
"""Build and package the client release artifacts."""

from __future__ import annotations

from release_common import client_spec, component_main


def main() -> int:
    return component_main(client_spec(), __doc__ or "")


if __name__ == "__main__":
    raise SystemExit(main())
