# P3-5: TODO/FIXME/HACK/XXX Resolution

## Objective

Resolve all 18 documented TODO/FIXME/HACK/XXX markers, prioritizing those that directly impact correctness.

## Current State

20 markers across the codebase (excluding third-party `evpphttp/http_parser_cpp.cc`). Key correctness-impacting items:

| Location | Content | Severity |
|----------|---------|----------|
| `buffer.h:122` | Stale TODO on functional Reserve() | P3 |
| `buffer.h:142` | int64 byte order uses custom evppbswap_64 instead of htonll | P3 |
| `http/http_server.cc:304,323` | Graceful shutdown not implemented | P2 |
| `connector.cc:149` | EVUTIL_ERR_CONNECT_RETRIABLE not handled | P3 |
| `dns_resolver.h:14` | IPv6 DNS not implemented | P3 |
| `dns_resolver.cc:276` | dns_req_ freeing not verified | P3 |
| `event_loop.cc:318` | Test code missing for Functor | P3 |
| `udp/udp_server.cc:224` | recvmmsg perf optimization | P3 |

## Implementation Steps

### Step 1: Address Correctness-Critical Items

1. **buffer.h:122**: Remove stale TODO on functional Reserve() — covered in P3-7
2. **buffer.h:142**: Fix int64 byte order (use htonll instead of evppbswap_64) — covered in P3-7
3. **dns_resolver.cc:276**: Verify dns_req_ freeing — `evdns_base_free()` cancels all outstanding requests; confirm dns_req_ does not need explicit free, then replace TODO with explanatory comment
4. **http/http_server.cc:304,323**: Graceful shutdown — covered in P3-11
5. **connector.cc:149**: Handle EVUTIL_ERR_CONNECT_RETRIABLE — covered in P3-12

### Step 2: Address Remaining Items

6. **dns_resolver.h:14**: Add IPv6 support (if needed for deployment)
7. **event_loop.cc:320**: Add missing test for Functor edge case
8. **udp/udp_server.cc:224**: Evaluate `recvmmsg` performance on Linux
9. Remaining 12 documentation/minor items (tcp_conn.h:126/181, tcp_conn.cc:239, listener.cc:35, service.cc:32/350, request.cc:33, etc.)

### Step 3: Add CI Check

Add a CI step that fails if TODO/FIXME/HACK/XXX count increases:

```yaml
- name: Check TODO count
  run: |
    count=$(grep -rn "TODO\|FIXME\|HACK\|XXX" src/ --include="*.cc" --include="*.h" | wc -l)
    if [ $count -gt 20 ]; then
      echo "TODO count increased from 20 to $count"
      exit 1
    fi
```

### Step 4: Resolution Policy

For each marker:
- **Fix now**: Correctness issues (P2 items above)
- **File ticket**: Feature requests (IPv6, recvmmsg)
- **Delete**: Obsolete notes from early development
- **Document**: Convert permanent notes to proper doc comments

### Step 5: Tests

After completing each step, add automated tests in the following categorized locations:

**CI enforcement** — `.github/workflows/lint.yml` (pre-existing, add check):
- CI step fails if TODO/FIXME/HACK/XXX count increases beyond baseline
- Baseline count is committed to `src/tests/fixtures/todo_baseline.txt`

**Unit tests** — `src/tests/unit/todo_tracking_test.cc`:
- Verify the TODO-count script produces correct count on sample files
- Verify CI check rejects a commit that adds new TODO markers
- Verify zero HACK markers in the codebase

```
src/tests/unit/todo_tracking_test.cc     # ~25 lines
src/tests/fixtures/todo_baseline.txt     # committed baseline count
```

## Acceptance Criteria

1. All P2-categorized TODO items are fixed
2. Remaining TODOs are either fixed or tracked as tickets
3. TODO count does not increase (CI enforcement)
4. Zero "HACK" markers remain
5. CI test verifies TODO/HACK count does not regress

## Dependencies: P0-3 (Test Infrastructure), P3-2 (CI Pipeline) | Estimated Effort: ~200 lines + ~25 lines tests
