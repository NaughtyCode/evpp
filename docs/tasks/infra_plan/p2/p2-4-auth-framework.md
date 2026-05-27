# P2-4: Authentication Framework

## Objective

Implement a connection-level authentication framework supporting token-based auth, session management, and configurable auth backends.

## Current State

No authentication exists. Any TCP connection is immediately accepted and can exchange messages with the server. No session concept, no token validation, no access control.

## Implementation Steps

### Step 1: Define Auth Protocol

**File**: `src/runtime/auth/auth_protocol.h`

```cpp
/* Authentication handshake protocol:
 *   1. Client connects
 *   2. Server sends: {type: "auth_required", methods: ["token", "password"]}
 *   3. Client responds: {type: "auth_request", method: "token", token: "xxx"}
 *   4. Server validates and responds: {type: "auth_ok", session_id: "...", entity_id: ...}
 *      or: {type: "auth_failed", reason: "invalid_token"}
 *   5. Normal message exchange begins
 */
```

### Step 2: Auth Backend Interface

**File**: `src/runtime/auth/auth_backend.h`

```cpp
struct AuthResult {
    bool success = false;
    std::string entity_id;    /* player/account ID */
    std::string session_id;
    std::string reason;       /* failure reason */
    int64_t expires_at = 0;   /* session expiry (unix timestamp) */
};

class AuthBackend {
public:
    virtual ~AuthBackend() = default;
    virtual AuthResult Authenticate(const std::string& method,
                                     const std::map<std::string, std::string>& params) = 0;
    virtual bool ValidateSession(const std::string& session_id) = 0;
    virtual void RevokeSession(const std::string& session_id) = 0;
};

/* Built-in backends */
class TokenAuthBackend : public AuthBackend { /* static token validation */ };
class JwtAuthBackend : public AuthBackend { /* JWT validation */ };
class DatabaseAuthBackend : public AuthBackend { /* DB-backed user/password */ };
```

### Step 3: Session Manager

**File**: `src/runtime/auth/session_manager.h`

```cpp
class SessionManager {
public:
    /* Create a session for an authenticated connection */
    SessionInfo CreateSession(const std::string& entity_id, evpp::TCPConnPtr conn);

    /* Validate a session (check expiry, revocation) */
    bool IsSessionValid(const std::string& session_id);

    /* Revoke a session (logout, kick) */
    void RevokeSession(const std::string& session_id);

    /* Get session by connection */
    std::optional<SessionInfo> GetSession(evpp::TCPConnPtr conn);

    /* Periodic cleanup of expired sessions */
    void CleanupExpired();

private:
    std::unordered_map<std::string, SessionInfo> sessions_;
    std::unordered_map<evpp::TCPConnPtr, std::string> conn_to_session_;
};
```

### Step 4: Connection Auth Wrapper

**File**: `src/runtime/script/auth_bind.cc`

Integrate the auth handshake into the TCP server connection flow. New connections enter an "authenticating" state until the handshake completes.

### Step 5: Config

```json
{
  "auth": {
    "enabled": true,
    "backend": "token",
    "token_backend": {
      "tokens": {"dev-token-1": "player_alice", "dev-token-2": "player_bob"},
      "validate_url": "https://auth.example.com/validate"
    },
    "session_timeout_seconds": 86400,
    "max_sessions_per_account": 5
  }
}
```

### Step 6: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/auth_session_test.cc`:
- Session creation: valid auth → session ID returned, metadata stored
- Session validation: valid session ID → returns session data; invalid → rejected
- Session expiry: expired session → validation returns "expired"
- Session revocation: logout → session removed, subsequent validation fails
- Concurrent session access: two requests for same session, no corruption

**Unit tests** — `src/tests/unit/auth_backend_test.cc`:
- Token backend: valid token → authenticated; invalid → rejected
- Backend switching: config change switches from token to JWT backend

**Integration tests** — `src/tests/integration/auth_flow_test.cc`:
- Full auth handshake: require → challenge → response → ok/fail
- Unauthenticated connection: send message before auth → rejected
- Authenticated connection: send message after auth → delivered

```
src/tests/unit/auth_session_test.cc       # ~60 lines
src/tests/unit/auth_backend_test.cc       # ~40 lines
src/tests/integration/auth_flow_test.cc   # ~60 lines
```

## Acceptance Criteria

1. Auth handshake protocol: require → challenge → response → ok/fail
2. Token-based auth backend
3. Session management with expiry
4. Configurable backend (token, JWT, database)
5. Unauthenticated connections cannot interact with game logic
6. Tests verify auth flow, session lifecycle, and backend switching

## Dependencies

- P0-2 (Message Framing) — auth messages need framing
- P0-1 (Entity Model) — entity creation on successful auth

## Estimated Effort: ~500 lines
