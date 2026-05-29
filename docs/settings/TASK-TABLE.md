# Config System Remediation — Task Execution Table

> Auto-generated from `./docs/settings/tasks/*/README.md` dependency fields.
> **Update this table after completing each task** — change Status and update Date Done.

**Last updated:** 2026-05-29

## Sorted by Dependency Order (Topological Sort)

| # | Task | Priority | Depends On | Status | Date Done |
|---|------|----------|------------|--------|-----------|
| 1 | `core-validation-fix` | P0 | — | pending | — |
| 2 | `physics-config-fix` | P0 | — | pending | — |
| 3 | `thread-safety-fix` | P0 | #1 | pending | — |
| 4 | `secrets-management` | P0 | #1 | pending | — |
| 5 | `observability-tooling` | P2 | #1 | pending | — |
| 6 | `hotreload-consumer-fix` | P0 | #1, #3 | pending | — |
| 7 | `runtime-environment-selection` | P0 | #1, #6 | pending | — |
| 8 | `server-operations` | P1 | #1, #3, #6 | pending | — |
| 9 | `game-business-config` | P1 | #1, #6 | pending | — |
| 10 | `health-check-probes` | P0 | #1, #7 | pending | — |
| 11 | `config-manager-unification` | P1 | #1, #3, #2 | pending | — |
| 12 | `client-config-framework` | P0 | #1, #7, #11 | pending | — |

## Parallel Execution Batches

Tasks in the same batch have no inter-dependencies and can run concurrently.

| Batch | Tasks | Unblocked By |
|-------|-------|--------------|
| A | #1 `core-validation-fix`, #2 `physics-config-fix` | (start) |
| B | #3 `thread-safety-fix`, #4 `secrets-management`, #5 `observability-tooling` | A |
| C | #6 `hotreload-consumer-fix` | #1 + #3 done |
| D | #7 `runtime-environment-selection`, #8 `server-operations`, #9 `game-business-config` | #6 done |
| E | #10 `health-check-probes`, #11 `config-manager-unification` | #7 done for #10; #2 + #3 done for #11 |
| F | #12 `client-config-framework` | #7 + #11 done |

## Status Legend

| Status | Meaning |
|--------|---------|
| `pending` | Not started |
| `in_progress` | Currently being worked on |
| `done` | Completed and verified |
| `blocked` | Waiting on dependency |
