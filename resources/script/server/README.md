# server

Base server entry-script directory for `GameServer`.

This directory is intentionally kept free of demo gameplay logic. Product or
project-specific server scripts can be placed here, or a config/CLI override can
point `server.scripts_dir` at another directory.

Expected script lifecycle hooks:

- `InitScript()`
- `UpdateScript()`
- `DestroyScript()`
