# P3-9: MongoDB Binding Return Value Unification

**Date:** 2026-05-28
**Status:** Complete (return 2 → return 3 batch transformation)
**Plan:** `./docs/tasks/infra_plan/p3/p3-9-mongo-return-unification.md`

## Summary

Eliminated all `return 2;` patterns across 44 MongoDB binding files. Every
function returning `(bool, err_or_nil)` now returns `(bool, err_or_nil, nil)`
— consistently 3 values. The result column is `nil` for operations without
result data (insert, delete, drop, etc.). Functions already returning 3
values (find_and_modify, command_simple, etc.) were left unchanged.

## Changes

### Modified Files (44 files)

All files under `src/runtime/database/mongo_bind/`:
`sed 's/^\(\t*\)return 2;/\1lua_pushnil(L);\n\1return 3;/'`

| Key files | Before | After |
|-----------|--------|-------|
| `bind_collection.cc` | 33 × `return 2` | 0 → all `return 3` |
| `bind_database.cc` | `return 2` | `return 3` + result nil |
| `bind_bulkwrite.cc` | 24 × `return 2` | 0 → all `return 3` |
| `bind_apm.cc` | 6 × `return 2` | 0 → all `return 3` |
| All others | `return 2` | `return 3` + result nil |

## Design Decisions

- **`return 1` unchanged**: Functions returning a single userdata object
  (`find`, `aggregate`, `watch`, getter accessors) were NOT changed. These
  return a cursor/object/nil and are not bool+err patterns. Changing them
  to 3-value returns would break all callers (`local cursor = coll:find(...)`).
- **`return 2` → `return 3` safe**: Adding a 3rd `nil` value is backward
  compatible — Lua ignores extra return values not captured by the caller.
  Code still using `local ok, err = ...` continues to work.
- **Mechanical transformation**: `return 2;` with (bool, err_or_nil) on stack
  → `lua_pushnil(L); return 3;` with (bool, err_or_nil, nil).

## Acceptance Criteria

- [x] Zero `return 2;` remaining in any mongo_bind file
- [x] All operations return 3 values: (bool, err|nil, result|nil)
- [x] Backward compatible: existing 2-value callers still work
- [ ] `return 1` getter functions → 3-value (deferred, requires caller updates)
- [ ] Lua script migration (update callers to capture all 3 values)
