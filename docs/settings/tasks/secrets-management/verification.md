# Verification Checklist: Secrets Management

## Unit Tests

- [ ] `${HOME}` interpolated in string value
- [ ] `${NONEXISTENT:-fallback}` uses "fallback"
- [ ] `${NONEXISTENT}` (no default) → error
- [ ] Multiple interpolations in single string: `${HOST}:${PORT}`
- [ ] Interpolation in MongoDB URI string
- [ ] admin_bind_address = "127.0.0.1" → admin only on localhost
- [ ] admin_bind_address = "0.0.0.0" → admin on all interfaces (with warning)
- [ ] Plaintext credential detection: `mongodb://admin:pass@host` → WARN logged
- [ ] Plaintext credential: `mongodb://host` (no auth) → no warning

## Security Verification

- [ ] `grep -rn "password\|secret\|passwd" resources/config/` returns no hardcoded secrets
- [ ] MongoDB URI with `${MONGO_PASSWORD}` interpolation works

## Manual Verification

- [ ] Set `MONGO_PASSWORD=test` → start server → MongoDB connects with password from env
