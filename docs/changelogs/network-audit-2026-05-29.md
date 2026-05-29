# Network Module Audit & Fixes — 2026-05-29

Comprehensive audit of the `evpp` network module covering TCP, UDP, KCP, buffer
management, rate limiting, and connection lifecycle.  Six issues were identified
and fixed.

---

## 1. Buffer move semantics — uninitialized `max_capacity_`

**File:** `src/runtime/evpp/buffer.h`
**Severity:** High (undefined behaviour)

The `Buffer` class has no in-class default initializer for `max_capacity_`, and
the move constructor / move assignment operator did not copy or initialize it,
leaving the field with an indeterminate value after a move.  A subsequent call
to `AtMaxCapacity()` or `SetMaxCapacity()` on the moved-to buffer would read
uninitialized memory.

**Fix:** Added `max_capacity_` to the member initializer list and move
assignment, copying the source value and resetting the moved-from buffer to the
default 256 KB.

---

## 2. Partial rate-limit consumption silently drops bytes

**File:** `src/runtime/evpp/tcp_conn.cc` — `TCPConn::Send(..., MessagePriority)`
**Severity:** High (data loss)

When `RateLimiter::Consume()` returned `0 < N < len`, only `N` bytes were sent
and the remaining `len - N` bytes were silently discarded.  The caller was
expected to handle the remainder but never did.

**Fix:** When `allowed < len`, the unsent tail is now pushed to
`pending_messages_` for deferred delivery.  The partial send proceeds with the
allowed portion, and the remainder is delivered later when tokens refill.

---

## 3. Non-retriable write errors silently swallowed

**File:** `src/runtime/evpp/tcp_conn.cc` — `TCPConn::SendInLoop`
**Severity:** High (silent data loss)

The non-SSL write path checked `!EVUTIL_ERR_RW_RETRIABLE(serrno)` but then
narrowed the error-handling guard to only `EPIPE` and `ECONNRESET`.  All other
fatal errors (`ENETDOWN`, `EHOSTUNREACH`, etc.) were logged but the function
continued as if a partial write had succeeded — losing the message without
triggering `HandleError()`.

**Fix:** All non-retriable send errors now set `write_error = true`,
triggering the error-handling path.

---

## 4. RateLimiter — documented thread-safe but unsynchronized

**File:** `src/runtime/evpp/rate_limiter.h`
**Severity:** Medium (data race)

The header comments claimed "Thread-safe" but `tokens_` and
`last_refill_time_` were plain `uint32_t` / `time_point` without any
synchronization.  `TCPConn::Send(priority)` calls `Consume()` from arbitrary
caller threads while the same connection may also be consumed from the I/O
thread.

**Fix:** Added a `std::mutex` guarding both `Consume()` and `SetRate()`.  The
mutex is per-connection (each `TCPConn` owns one `RateLimiter`), so contention
is negligible.

---

## 5. TCPClient::DisconnectInLoop assertion fires for in-flight close

**File:** `src/runtime/evpp/tcp_client.cc` — `TCPClient::DisconnectInLoop`
**Severity:** Medium (crash in debug builds)

When the user calls `Disconnect()` while a server-initiated close is already in
progress, `conn_->IsDisconnecting()` is true and the assertion fires.  This is
a legitimate race — the user is allowed to call `Disconnect()` at any time.

**Fix:** Replaced the assertion with a guard: if the connection is already
`kDisconnected` or `kDisconnecting`, `DisconnectInLoop` logs at trace level
and skips the `Close()` call instead of crashing.

---

## 6. UDP / KCP server busy-wait loops

**Files:**
- `src/runtime/evpp/udp/udp_server.cc`
- `src/runtime/evpp/kcp/kcp_server.cc`
**Severity:** Medium (CPU waste)

The `RecvThread` start/stop sequence used `while (!IsRunning()) { usleep(1); }`
— polling at 1 μs intervals.  The pause loop similarly burned CPU with
`usleep(1)` (UDP) or `usleep(1000)` (KCP).  At scale this wastes measurable
CPU time.

**Fix:** Added `std::mutex` + `std::condition_variable` to `RecvThread`.
- `Run()` waits on the CV until the thread signals `kRunning` or `kStopped`.
- `Stop()` notifies the CV; `WaitUntilStopped()` blocks until `kStopped`.
- The pause loop uses `WaitWhilePaused()` which blocks on the CV instead of
  polling.
- Both UDP and KCP servers received identical treatment.

---

## Audit scope

The following files were reviewed in full:

| Layer | Files |
|---|---|
| Platform | `sys_sockets.h`, `sockets.h/.cc`, `platform_config.h` |
| Event loop | `event_loop.h/.cc`, `event_loop_thread.h/.cc`, `event_loop_thread_pool.h/.cc` |
| I/O channel | `fd_channel.h/.cc`, `event_watcher.h/.cc` |
| TCP | `tcp_conn.h/.cc`, `tcp_server.h/.cc`, `tcp_client.h/.cc` |
| | `connector.h/.cc`, `listener.h/.cc` |
| DNS | `dns_resolver.h/.cc` |
| Buffer | `buffer.h/.cc` |
| Rate limiting | `rate_limiter.h` |
| UDP | `udp/udp_server.h/.cc`, `udp/sync_udp_client.h/.cc` |
| KCP | `kcp/kcp_server.h/.cc`, `kcp/sync_kcp_client.h/.cc` |
| Codec | `network/length_prefixed_codec.h/.cc` |

## Design notes — areas observed but intentionally unchanged

- **output_buffer_ is single-buffer (not scatter-gather).** Switching to
  `writev`/`WSASend` would reduce data copies on large writes, but the current
  approach is simpler and correct for MTU-sized messages.

- **UDP recv uses 500 ms socket timeout** (`SO_RCVTIMEO`) so the thread can
  check stop/pause flags.  A self-pipe trick or `eventfd` would eliminate the
  timer-based latency, but the change is more invasive and the current design
  is adequate for non-real-time UDP services.

- **TCPConn::HandleClose invokes callbacks in `kDisconnecting` state**, then
  transitions to `kDisconnected`.  This is the documented API contract and is
  correct.

- **Connector already captures `auto_reconnect` and `reconnect_interval` before
  invoking the user callback** to avoid use-after-free of `owner_tcp_client_`.
  This pattern is safe.

- **DNSResolver correctly nulls `evdns_cb_arg_` before invoking the callback**,
  preventing double-deletion if `Cancel()` is called from within the callback.

- **`Listener::Stop()` intentionally does not call `chan_->Close()`** — only
  `DisableAllEvent()`.  The `~FdChannel()` destructor handles the actual event
  deletion.  The comment in the source explains the double-close hazard.
