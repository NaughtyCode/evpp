# Runtime Deep Review - 2026-06-01

## Context

Reviewed `src/runtime` from server lifecycle, Lua binding safety, network resource limits, and data-driven config robustness angles.

## Fixes

- Made TCP server/client shutdown work when the owning `EventLoop` has already exited, instead of queuing close work that can no longer run.
- Kept Lua TCP server binding contexts alive until the underlying TCP server stop callback completes, avoiding queued stop callbacks touching freed state.
- Prevented `config.unregister(id)` from one Lua VM from unregistering and leaking callbacks owned by another VM.
- Replaced `config.get_module`'s fragile module JSON path with Glaze parsing and explicit Lua conversion, returning `(nil, error)` for malformed module files.
- Enforced `server.resource_limits.max_http_body_size` for `net.http.post`.
- Enforced `server.resource_limits.max_message_size` for UDP `do_request` paths, matching UDP send/send_to and KCP request behavior.
- Enforced `server.msgpack.max_payload_size` on MessagePack decode input, not just encode output.
- Made `SessionManager` max-session eviction deterministic when multiple sessions are created in the same second.

## Tests

- Added config binding coverage for malformed module JSON and cross-VM unregister isolation.
- Added net binding coverage for oversized HTTP POST bodies and UDP request payloads.
- Added TCP integration coverage for stopping a server after the event loop exits.
- Added Lua MessagePack coverage for oversized decode input.
- Re-ran the auth and connection-limit failures exposed by full CTest after fixing their root causes.
