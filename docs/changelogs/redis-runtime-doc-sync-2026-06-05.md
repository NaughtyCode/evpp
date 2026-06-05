# Redis Runtime Documentation Sync - 2026-06-05

## Summary

Synchronized documentation for the new server-only Redis runtime module under
`src/runtime/database/redis`.

## Updated Areas

- Lua runtime API: added `redis.command`, `redis.eval`, health helpers,
  callback dispatch semantics, result table shape, and unsupported command
  rules.
- Resource API docs: added `resources/api/redis/api.md` and Redis config paths
  in `resources/api/config/api.md`.
- Architecture docs: updated `README.md`, `docs/architecture.txt`, and
  `docs/architecture.svg` with Redis workers, hiredis, config, lifecycle, and
  Lua binding entries.
- Redis module docs: aligned examples with actual global logging functions and
  nested Redis value tables.
- Database service docs: documented optional Redis export inside DBThread VMs.
- Config docs: documented `server.redis`, `server.redis_required`, and
  `resources/config/server/redis.json`.
