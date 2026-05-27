# P3-5: TODO/FIXME/HACK/XXX Resolution

## Objective

Resolve all 18 documented TODO/FIXME/HACK/XXX markers, prioritizing those that directly impact correctness.

## Current State

18 markers across the codebase. Key correctness-impacting items:

| Location | Content | Severity |
|----------|---------|----------|
| `buffer.h:121` | Reserve method empty implementation | P2 |
| `buffer.h:141` | Byte order issue | P2 |
| `http_server.cc:294,307` | Graceful shutdown not implemented | P2 |
| `connector.cc:132` | Reconnect logic not implemented | P2 |
| `dns_resolver.h:14` | IPv6 DNS not implemented | P2 |
| `dns_resolver.cc:217` | dns_req_ may leak | P2 |
| `event_loop.cc:301` | Test code missing | P3 |
| `udp_server.cc:221` | recvmmsg perf optimization | P3 |

## Implementation Steps

### Step 1: Address Correctness-Critical Items

1. **buffer.h:121**: Implement `Reserve()` — covered in P0-2 prerequisite
2. **buffer.h:141**: Fix byte order — covered in P0-2 prerequisite
3. **dns_resolver.cc:217**: Fix shared_ptr leak — covered in P2-22
4. **http_server.cc:294,307**: Graceful shutdown — covered in P3-11
5. **connector.cc:132**: Reconnect logic — covered in P3-12

### Step 2: Address Remaining Items

6. **dns_resolver.h:14**: Add IPv6 support (if needed for deployment)
7. **event_loop.cc:301**: Add missing test for edge case
8. **udp_server.cc:221**: Evaluate `recvmmsg` performance on Linux
9. Remaining 10 documentation/minor items

### Step 3: Add CI Check

Add a CI step that fails if TODO/FIXME/HACK/XXX count increases:

```yaml
- name: Check TODO count
  run: |
    count=$(grep -rn "TODO\|FIXME\|HACK\|XXX" src/ --include="*.cc" --include="*.h" | wc -l)
    if [ $count -gt 18 ]; then
      echo "TODO count increased from 18 to $count"
      exit 1
    fi
```

### Step 4: Resolution Policy

For each marker:
- **Fix now**: Correctness issues (P2 items above)
- **File ticket**: Feature requests (IPv6, recvmmsg)
- **Delete**: Obsolete notes from early development
- **Document**: Convert permanent notes to proper doc comments

## Acceptance Criteria

1. All P2-categorized TODO items are fixed
2. Remaining TODOs are either fixed or tracked as tickets
3. TODO count does not increase (CI enforcement)
4. Zero "HACK" markers remain

## Dependencies: Various (items covered by other plans) | Estimated Effort: ~200 lines
