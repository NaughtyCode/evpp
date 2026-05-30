# Runtime Physics Hardening - 2026-05-30

## Summary

Audited the runtime physics implementation under `src/runtime/physics`, including the Jolt integration, physics thread lifecycle, command/result queues, Lua bindings, asset loading, collision layers, ray casts, and physics integration tests.

## Fixes

- Made `PhysicsThread::Start()` wait for `PhysicsWorld` initialization and return failure if startup fails or times out.
- Fixed `PhysicsThread::Stop()` so crashed or early-exited threads are still joined instead of leaving a joinable thread behind.
- Prevented physics recovery from running on the physics thread, avoiding self-join deadlock from `physics.recover()`.
- Registered the post-step callback before launching the physics thread to avoid a callback publication race.
- Changed command queue backpressure to use `commandQueueSize` and result queue trimming to use `resultQueueSize`, instead of incorrectly using `maxPendingFrames` for both.
- Added a small out-of-order frame-result cache so `FetchResult()` no longer discards non-matching frames while waiting for a specific frame id.
- Fixed `physics.on_physics_collision` Lua callback stack handling so error handlers/functions do not leak across collision callbacks.
- Corrected ray casts so the input direction is normalized and scaled by `max_dist`; invalid zero/NaN rays now return no hit.
- Made threshold hot-reload safe by protecting `PhysicsWorld` threshold reads/writes with a mutex.
- Fixed collision contacts at world origin by tracking contact-point validity explicitly instead of treating `(0, 0, 0)` as absent.
- Applied dynamic prototype mass correctly by enabling Jolt mass override with calculated inertia.
- Avoided overflow in temporary allocator sizing for very large body-pair limits.
- Switched physics stats to Jolt's locked body stats instead of reading `state_snapshots_` from the main thread.
- Hardened asset loading validation for shape dimensions, mesh vertices/indices/materials, height fields, quaternion normalization, and dynamic prototype mass.
- Cleaned up partially-created static bodies if asset loading fails before batch-add finalization.
- Made broad-phase layer count robust for sparse layer ids and added config validation for layer mappings, collision rules, job threads, and capacity limits.
- Updated physics integration tests to use the actual physics config directory and no optional script directory.

## Verification Notes

- `git diff --check` passed; only existing Windows LF/CRLF warnings were emitted.
- `cmake --build artifacts\build --config Debug --target test_integ_physics -- /m:1` compiled the touched physics sources without new compiler errors.
- Full target build is still blocked by the existing CMake/MSVC auto-export failure:
  `Auto build dll exports` reports `unrecognized file format` for `artifacts/build/server/CloudEngine.dir/Debug/lapi.obj`.
