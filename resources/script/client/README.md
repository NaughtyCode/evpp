# client

Entry scripts for the client (`src/client` / GameClient.dll).

## Purpose

Scripts in this directory are loaded when the engine runs in client (library) mode.
The entry script first loads all shared runtime modules, then defines client-specific
lifecycle hooks.

## Loading order

1. `import("runtime.*")` — loads all shared modules from `resources/script/runtime/`.
2. Client-specific `InitScript()` / `UpdateScript()` / `DestroyScript()` are defined.
3. The engine calls `InitScript()` once after all scripts are loaded.
4. The engine calls `UpdateScript()` every frame.
5. The engine calls `DestroyScript()` on shutdown.

## Conventions

- Define `InitScript()`, `UpdateScript()`, and `DestroyScript()` in the entry script.
- Put client-only modules (UI helpers, prediction, etc.) as additional `.lua` files here.
- Use `import()` to load modules from the runtime directory.
