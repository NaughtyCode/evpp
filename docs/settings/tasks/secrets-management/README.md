# Task 5: Secrets Management

**Priority:** P0 — security compliance
**Status:** pending
**Dependencies:** Task 1 (core-validation-fix)

## Scope

Remove plaintext credentials from JSON config files. Add environment variable interpolation. Secure admin HTTP endpoint.

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P0-5 | MongoDB凭证明文存储在JSON文件中，密码泄露风险 |
| P2-8 | admin_port绑定地址不可配置，可能暴露到公网 |
| P2-9 | 无外部Secret注入支持 |

## Key Changes

1. Implement `${ENV_VAR}` and `${ENV_VAR:-default}` interpolation in JSON values
2. Add `admin_bind_address` to `ServerConfig` (default `127.0.0.1`)
3. Add warning log when MongoDB URI contains embedded credentials
4. Add TLS configuration options for admin HTTP server

## Affected Files

- `src/runtime/config/config.cc` (new interpolation module)
- `src/runtime/config/config.h`
- `resources/config/server/server.json`
- `src/runtime/network/admin_http.cc`
- `src/runtime/config/config_constants.h`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
