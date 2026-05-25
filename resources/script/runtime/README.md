# runtime

Shared Lua modules loaded by both client and server entry scripts.

## Purpose

Scripts in this directory are common library code — utility functions, base classes, shared
game logic — used by both the client and server. They are loaded via `import("runtime.*")`
from the client or server entry script before any role-specific initialization runs.

## Loading order

1. The engine loads all `.lua` files from the role-specific scripts directory
   (`resources/script/client/` or `resources/script/server/`).
2. The entry script in that directory calls `import("runtime.*")` to load every module
   in this directory.
3. After all runtime modules are loaded, the entry script defines `InitScript()`,
   `UpdateScript()`, and `DestroyScript()`.

## Conventions

- Each `.lua` file is a self-contained module.
- Return a table from the module to expose its public API.
- Do NOT define `InitScript()` / `UpdateScript()` / `DestroyScript()` here — those
  belong in the client or server entry scripts.
