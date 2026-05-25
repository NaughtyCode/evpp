# server

Entry scripts for the server (`src/server` / main executable).

## Purpose

Scripts in this directory are loaded when the engine runs in standalone server mode.
The entry script first loads all shared runtime modules, then defines server-specific
lifecycle hooks.

## Loading order

1. `import("runtime.*")` — loads all shared modules from `resources/script/runtime/`.
2. Server-specific `InitScript()` / `UpdateScript()` / `DestroyScript()` are defined.
3. The engine calls `InitScript()` once after all scripts are loaded.
4. The engine calls `UpdateScript()` every frame.
5. The engine calls `DestroyScript()` on shutdown.

## Conventions

- Define `InitScript()`, `UpdateScript()`, and `DestroyScript()` in the entry script.
- Put server-only modules (AI, authority logic, persistence, etc.) as additional `.lua`
  files here.
- Use `import()` to load modules from the runtime directory.
