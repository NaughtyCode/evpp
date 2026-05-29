# Implementation Plan: Secrets Management

## Step 1: Implement env-var interpolation

Create new helper in `config.cc` (or `config_interpolation.cc`):
- `InterpolateEnvVars(std::string& value)`: replaces `${VAR}` and `${VAR:-default}`
- Call on every string field after JSON parse, before validation
- Handle edge cases: `${NONEXISTENT}` → error (no default), `${VAR:-}` → empty string default

## Step 2: Integrate interpolation into Load path

Modify `config.cc`:
- After `glz::read_json` succeeds, walk the config struct and interpolate all string members
- This applies to: `resource_dir`, `scripts_dir`, `log.dir`, `mongodb_dev`, `mongodb_public`, `db_service`, connection URI
- Use glaze reflection to walk struct members automatically

## Step 3: Add admin_bind_address

Modify `config.h`:
- Add `admin_bind_address` field to `ServerConfig` (default `"127.0.0.1"`)
- Update `admin_http.cc` to use `admin_bind_address` instead of default `0.0.0.0`

## Step 4: Add plaintext credential detection

Modify `config.cc`:
- After loading MongoDB config, check if `connection.uri` contains `://user:password@` pattern
- If detected, log WARN: "MongoDB URI contains embedded credentials. Use ${ENV_VAR} interpolation instead."

## Step 5: Add TLS config for admin HTTP

Modify `config.h`:
- Add `AdminTlsConfig` struct: `enabled`, `cert_file`, `key_file`
- Add `admin_tls` field to `ServerConfig`
- Update `admin_http.cc` to conditionally enable TLS

## Step 6: Update tests

In `test_config.cpp`:
- Test: `${VAR}` interpolation with VAR set
- Test: `${VAR:-default}` with VAR unset → uses default
- Test: `${NONEXISTENT}` with no default → error
- Test: `${VAR}` nested in URI string
- Test: admin_bind_address config respected
