# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Changed
- **TimerManager singleton decoupled**: Removed the global singleton pattern from `TimerManager`. The class is now a regular instance owned by `Engine` and injected via dependency injection.
  - Removed `TimerManager::instance()`, `create_instance()`, `destroy_instance()` static methods.
  - Removed global convenience functions `create_timeout()`, `create_interval()`, `cancel()`.
  - `Engine` now owns `std::unique_ptr<TimerManager>` and exposes it via `GetTimerManager()`.
  - `EntityManager::SetTimerManager()` injects the manager into newly created entities.
  - `ExportTimer()` and `ExportAll()` accept `TimerManager&` for Lua timer bindings.
  - Tests updated to create their own `TimerManager` instances.
