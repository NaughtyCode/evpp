# client

Base client entry-script directory for `GameClient.dll`.

This directory is intentionally kept free of demo gameplay logic. Product or
project-specific client scripts can be placed here, or a config overlay can
point `client.scripts_dir` at another directory.

The standalone CS demo lives under `resources/demos/cs/` and is copied into release
artifacts by `scripts/build_release.py` as an optional overlay.

Expected script lifecycle hooks:

- `InitScript()`
- `UpdateScript()`
- `DestroyScript()`
