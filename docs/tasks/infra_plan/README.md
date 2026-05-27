# CloudEngine Infrastructure Development Plans

This directory contains fine-grained development plans derived from `docs/infra/deficiency-analysis.md`. Each plan is a self-contained, actionable document.

## Plan Organization

Plans are organized by priority level matching the deficiency analysis:

| Priority | Definition | Directory |
|----------|-----------|-----------|
| P0 | Blocking defects — cannot carry real game business without resolution | [p0/](p0/) |
| P1 | First milestone — improves development efficiency and safety | [p1/](p1/) |
| P2 | Production readiness | [p2/](p2/) |
| P3 | Continuous improvement | [p3/](p3/) |

---

## P0 — Blocking Defects (7 plans)

| ID | Plan | Category | Est. Effort |
|----|------|----------|-------------|
| P0-1 | [Entity Model — Entity/Actor/GameObject Abstraction](p0/p0-1-entity-model.md) | Core Architecture | ~1500 LOC C++ + Lua |
| P0-2 | [Message Framing Protocol — LengthPrefixedCodec](p0/p0-2-message-framing.md) | Network | ~490 LOC C++ |
| P0-3 | [Test Infrastructure](p0/p0-3-test-infrastructure.md) | Quality | ~2000 LOC |
| P0-4 | [luaL_error Exception Safety — Prevent C++ Destructor Bypass](p0/p0-4-lual-error-safety.md) | Script Binding | ~200 LOC |
| P0-5 | [Message/Payload Size Limits — DoS Prevention](p0/p0-5-message-size-limits.md) | Security | ~100 LOC |
| P0-6 | [Cross-thread RunInLoop Lifetime Safety](p0/p0-6-runinloop-safety.md) | Concurrency | ~300 LOC |
| P0-7 | [Lua Sandbox — Replace luaL_openlibs with Whitelist](p0/p0-7-lua-sandbox.md) | Security | ~150 LOC |

## P1 — First Milestone (9 plans)

| ID | Plan | Category | Est. Effort |
|----|------|----------|-------------|
| P1-1 | [Binding Boilerplate Elimination — Unify Network Binding Patterns](p1/p1-1-binding-boilerplate.md) | Code Quality | ~800 LOC |
| P1-2 | [Config Hot-Reload Notification Mechanism](p1/p1-2-config-hot-reload.md) | Config | ~200 LOC |
| P1-3 | [TCP Client Explicit Shutdown](p1/p1-3-tcp-client-shutdown.md) | Network | ~150 LOC |
| P1-4 | [PhysicsThread EventLoop — Replace 50ms Poll-Sleep with condition_variable](p1/p1-4-physics-cv-wakeup.md) | Physics | ~100 LOC |
| P1-5 | [Physics Result Integration — Connect Physics Output to Game Layer](p1/p1-5-physics-result-integration.md) | Physics | ~300 LOC |
| P1-6 | [Database Backpressure Notification](p1/p1-6-db-backpressure.md) | Database | ~150 LOC |
| P1-7 | [Lua Coroutine Integration](p1/p1-7-coroutine-integration.md) | Script | ~500 LOC |
| P1-8 | [Hot-Reload System — File Watch + Validate + Rollback](p1/p1-8-hot-reload.md) | Script | ~400 LOC |
| P1-9 | [Entity-Connection Binding — Multi-VM Architecture](p1/p1-9-multi-vm-architecture.md) | Core Architecture | ~600 LOC |

## P2 — Production Readiness (23 plans)

| ID | Plan | Category | Est. Effort |
|----|------|----------|-------------|
| P2-1 | [AOI System — Spatial Index / Area of Interest](p2/p2-1-aoi-system.md) | Game Systems | ~1000 LOC |
| P2-2 | [ORM + Cache Layer for Database](p2/p2-2-orm-cache-layer.md) | Database | ~800 LOC |
| P2-3 | [RPC Framework — msgpack-based Service Communication](p2/p2-3-rpc-framework.md) | Network | ~1000 LOC |
| P2-4 | [Authentication Framework](p2/p2-4-auth-framework.md) | Security | ~500 LOC |
| P2-5 | [Monitoring Metrics + Admin HTTP Endpoint](p2/p2-5-monitoring-admin.md) | Observability | ~400 LOC |
| P2-6 | [Windows Signal Handling](p2/p2-6-windows-signals.md) | Platform | ~50 LOC |
| P2-7 | [Compiler Warning Configuration Unification (UNIX/MSVC)](p2/p2-7-compiler-warnings.md) | Build | ~30 LOC |
| P2-8 | [evpp Release Build Thread Safety Checks](p2/p2-8-evpp-release-safety.md) | Concurrency | ~100 LOC |
| P2-9 | [msgpack Encode Size/Depth Limits](p2/p2-9-msgpack-limits.md) | Security | ~50 LOC |
| P2-10 | [Lua Error Dispatch Strategy Unification](p2/p2-10-lua-error-dispatch.md) | Script | ~200 LOC |
| P2-11 | [RunInLoop — weak_ptr/shared_ptr Migration from Delayed Delete](p2/p2-11-runinloop-smart-ptr.md) | Concurrency | ~300 LOC |
| P2-12 | [Global Variable Ownership Tracking + ClearCache Cleanup](p2/p2-12-global-tracking.md) | Script | ~200 LOC |
| P2-13 | [PhysicsSystem::FetchResult — Replace Busy-Wait with condition_variable](p2/p2-13-physics-fetch-cv.md) | Physics | ~80 LOC |
| P2-14 | [SerializeCursor Document Count Limit — Prevent GB-level OOM](p2/p2-14-serialize-cursor-limit.md) | Database | ~50 LOC |
| P2-15 | [kDeleteMany Empty Filter Safety — Require Explicit Confirmation](p2/p2-15-delete-many-safety.md) | Database | ~80 LOC |
| P2-16 | [HTTP Pending Refs O(1) — vector → unordered_set](p2/p2-16-http-pending-refs.md) | Network | ~30 LOC |
| P2-17 | [ExportMongo X-macro Simplification — 246 → 82 Maintenance Points](p2/p2-17-export-mongo-xmacro.md) | Code Quality | ~200 LOC |
| P2-18 | [fprintf Cleanup — Unify All Diagnostics to Quill Logging](p2/p2-18-fprintf-cleanup.md) | Observability | ~100 LOC |
| P2-19 | [Eliminate 4 abort() Calls — Graceful Error Handling](p2/p2-19-abort-elimination.md) | Reliability | ~80 LOC |
| P2-20 | [Connection-Level Encryption (Non-HTTP Paths)](p2/p2-20-connection-encryption.md) | Security | ~300 LOC |
| P2-21 | [Connection Count Limit — Prevent FD Exhaustion](p2/p2-21-connection-limit.md) | Network | ~100 LOC |
| P2-22 | [DNS Resolver shared_ptr RAII Improvement](p2/p2-22-dns-leak-fix.md) | Network | ~30 LOC |
| P2-23 | [DoString Script Source Restriction + Execution Limits](p2/p2-23-dostring-limits.md) | Security | ~100 LOC |

## P3 — Continuous Improvement (13 plans)

| ID | Plan | Category | Est. Effort |
|----|------|----------|-------------|
| P3-1 | [Multi-Database Backend Abstraction](p3/p3-1-multi-db-backend.md) | Database | ~500 LOC |
| P3-2 | [CI/CD Pipeline Configuration](p3/p3-2-ci-cd-pipeline.md) | DevOps | ~580 LOC |
| P3-3 | [Message Priority Queue + Per-Connection Rate Limiting](p3/p3-3-message-priority-limits.md) | Network | ~300 LOC |
| P3-4 | [Singleton Decoupling — Enable Multi-Engine Instances](p3/p3-4-singleton-decoupling.md) | Architecture | ~500 LOC |
| P3-5 | [TODO/FIXME/HACK Resolution](p3/p3-5-todo-resolution.md) | Code Quality | ~200 LOC |
| P3-6 | [Embedded Test Code Cleanup — Remove DB Smoke Test from engine.cc](p3/p3-6-embedded-test-cleanup.md) | Code Quality | ~50 LOC |
| P3-7 | [Buffer Cleanup — Remove Stale TODO + Fix int64 Endian](p3/p3-7-buffer-fixes.md) | Network | ~30 LOC |
| P3-8 | [Engine::Cleanup Lifecycle Order Documentation + Assertions](p3/p3-8-cleanup-lifecycle-docs.md) | Reliability | ~30 LOC |
| P3-9 | [MongoDB Binding Return Value Unification](p3/p3-9-mongo-return-unification.md) | Database | ~200 LOC |
| P3-10 | [Cursor Pre-allocation Pattern Safety](p3/p3-10-cursor-prealloc-safety.md) | Database | ~80 LOC |
| P3-11 | [HTTP Graceful Shutdown Implementation](p3/p3-11-http-graceful-shutdown.md) | Network | ~100 LOC |
| P3-12 | [Connector Retry/Reconnection Logic](p3/p3-12-connector-retry.md) | Network | ~100 LOC |
| P3-13 | [Remaining Minor Fixes](p3/p3-13-remaining-minor-fixes.md) — send() return value, circular import detection, config schema validation, scene path config | Misc | ~160 LOC |

---

## Dependency Graph

```
P0-2 (Message Framing) ─────────────────────────────────────────────────┐
P0-1 (Entity Model) ───────────────────────────────────────────────────┐│
P0-7 (Lua Sandbox) ───────────────────────────────────────────────────┐││
P0-4 (luaL_error Safety) ────────────────────────────────────────────┐│││
P0-5 (Message Size Limits) ── depends on P0-2 ──────────────────────┐│││││
P0-6 (RunInLoop Safety) ───────────────────────────────────────────┐│││││││
P0-3 (Test Infrastructure) ── depends on P0-1, P0-2 ─────────────┐│││││││││
                                                                  ││││││││││
P1-1 (Binding Boilerplate) ── depends on P0-4, P0-6 ────────────┐││││││││││
P1-2 (Config Hot-Reload) ───────────────────────────────────────┐│││││││││││
P1-3 (TCP Client Shutdown) ── depends on P0-6 ─────────────────┐││││││││││││
P1-4 (Physics CV Wakeup) ─────────────────────────────────────┐│││││││││││││
P1-5 (Physics Result Integration) ── depends on P0-1 ────────┐││││││││││││││
P1-6 (DB Backpressure) ──────────────────────────────────────┐│││││││││││││││
P1-7 (Coroutine Integration) ───────────────────────────────┐││││││││││││││││
P1-8 (Hot-Reload System) ── depends on P1-2 ───────────────┐│││││││││││││││││
P1-9 (Multi-VM Architecture) ── depends on P0-1 ──────────┐││││││││││││││││││
                                                           │││││││││││││││││││
P2-* (Production Readiness) ── most depend on P0/P1 ─────┐│││││││││││││││││││
P3-* (Continuous Improvement) ── can proceed in parallel  ││││││││││││││││││││
```

## Execution Order Recommendation

### Phase 1 — Foundation (P0, ~2-3 weeks)
1. P0-2: Message Framing (prerequisite for P0-5)
2. P0-7: Lua Sandbox (independent)
3. P0-4: luaL_error Safety (independent)
4. P0-5: Message Size Limits (depends on P0-2)
5. P0-6: RunInLoop Safety (independent)
6. P0-1: Entity Model (independent, largest)
7. P0-3: Test Infrastructure (ongoing, parallel)

### Phase 2 — Efficiency (P1, ~2-3 weeks)
8. P1-3: TCP Client Shutdown
9. P1-1: Binding Boilerplate Elimination
10. P1-2: Config Hot-Reload
11. P1-6: DB Backpressure
12. P1-4 + P1-5: Physics Improvements
13. P1-7: Coroutine Integration
14. P1-8: Hot-Reload System
15. P1-9: Multi-VM Architecture

### Phase 3 — Production (P2, ~3-4 weeks)
Execute P2-1 through P2-23 in dependency order.

### Phase 4 — Polish (P3, ongoing)
Execute P3-1 through P3-12 as capacity allows.

---

## Plan Document Format

Each plan document follows this structure:

1. **Objective** — What will be achieved
2. **Current State** — What exists now (from deficiency analysis)
3. **Root Cause** — Why the deficiency exists
4. **Impact** — Quantified consequences of not addressing
5. **Implementation Steps** — Numbered, actionable steps with file paths
6. **Acceptance Criteria** — Verifiable conditions for completion
7. **Dependencies** — Other plans this depends on
8. **Estimated Effort** — LOC and complexity estimate
9. **Risks** — Potential pitfalls during implementation
