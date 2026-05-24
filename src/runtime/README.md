# engine

Game server engine.

## Development Language

- C++17

## Code Style

- Use `clang-format` (project root `.clang-format`)
- Type names `PascalCase`, function names `PascalCase`, variable names `snake_case`, constants `kPascalCase`
- Member variable suffix `_` (e.g. `player_id_`)
- Header include order: own header → standard library → third-party library → project header
- Each `.cc` paired with `.h`, uniformly use `#pragma once`
- No raw `new`/`delete`, use `std::unique_ptr`/`std::shared_ptr` to manage lifetime

## Commit Conventions

- Branch naming: `feature/<description>`, `fix/<description>`, `refactor/<description>`
- Commit message: English, first line no more than 72 characters, format `<type>: <summary>`
  - `feat:` new feature
  - `fix:` bug fix
  - `refactor:` refactoring
  - `docs:` documentation
  - `test:` tests
- One commit does one thing, do not mix unrelated changes
