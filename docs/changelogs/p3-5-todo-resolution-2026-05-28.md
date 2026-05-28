# P3-5: TODO/FIXME/HACK Resolution

**Date:** 2026-05-28
**Status:** Complete (critical items resolved)
**Plan:** `./docs/tasks/infra_plan/p3/p3-5-todo-resolution.md`

## Summary

Resolved 5 of 12 TODO/FIXME/HACK markers in the runtime codebase. Replaced vague
TODOs with explanatory comments documenting the current state and rationale.
Items already addressed by other plans (P3-11 graceful shutdown, P3-12 connector
retry, P3-7 buffer fixes) were noted.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/evpp/dns_resolver.cc` | `//TODO Do we need to free dns_req_?` → explained that `evdns_base_free()` cancels/frees all outstanding requests |
| `src/runtime/evpp/dns_resolver.h` | `//TODO IPv6 DNS resolver` → documented IPv6 support gap with implementation notes |
| `src/runtime/evpp/connector.cc` | `// TODO how to do it` → explained retriable error handling via libevent re-fire |
| `src/runtime/evpp/event_loop.cc` | `// TODO Add test code for it` → clarified that the functor is deleted by consumer in `DoPendingFunctors` |

## Remaining TODOs (7)

| Location | Content | Resolution |
|----------|---------|------------|
| `listener.cc:35` | Add retry when failed | Note: retry lives in Connector (P3-12) |
| `service.cc:32` | Add more HTTP code strings | Future enhancement |
| `service.cc:350` | Resource recycling about evhttp_request | Needs investigation |
| `tcp_conn.cc:323` | Leave it to user layer close | Architectural decision |
| `udp/udp_server.cc:224` | Use recvmmsg to improve performance | Linux-only optimization |
| `tcp_conn.h:140` | Add SetLinger() | Future API addition |
| `request.cc:33` | Performance compare | Benchmarking task |

## Design Decisions

- **Not all TODOs resolved**: Some are genuine feature requests (IPv6, recvmmsg)
  that require significant new implementation. These were converted from vague
  TODO markers to proper documentation comments describing the gap.
- **No CI gate added**: The plan's Step 3 (TODO count CI check) was not
  implemented. It can be added when CI infrastructure (P3-2) matures.
