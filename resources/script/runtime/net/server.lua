-- net/server.lua
-- TcpServer + TcpConnection wrappers around net.server C++ bindings.
--
-- TcpServer provides:
--   - Listening on an address with start/stop lifecycle
--   - Active connection tracking (add on connect, remove on close)
--   - Per-connection and server-level callbacks
--   - broadcast() to all connected clients
--
-- TcpConnection wraps a single server-side connection:
--   - send / close / remote_addr
--   - Per-connection on_message / on_close callbacks
--
-- Usage:
--   local TcpServer = import("runtime.net.server")
--   local server = TcpServer()
--   server.on_connect = function(conn) ... end
--   server.on_message = function(conn, data) ... end
--   server.on_close   = function(conn) ... end
--   server:start("0.0.0.0:8080")

local Class = import("runtime.common.class")

-- ══════════════════════════════════════════════════════════════════════════════
-- TcpConnection — wraps a single accepted client connection
-- ══════════════════════════════════════════════════════════════════════════════

local TcpConnection = Class("TcpConnection")

function TcpConnection:__ctor(raw_conn, remote_addr)
    self._raw         = raw_conn
    self._remote_addr = remote_addr
    self._closed      = false

    -- Public per-connection callbacks (optional overrides)
    self.on_message   = nil   -- function(self, data)
    self.on_close     = nil   -- function(self, remote_addr)
end

-- ── send(data) ──────────────────────────────────────────────────────────

function TcpConnection:send(data)
    if self._closed then
        error("TcpConnection:send: connection is closed")
    end
    return self._raw:send(data)
end

-- ── close() ─────────────────────────────────────────────────────────────

function TcpConnection:close()
    if not self._closed then
        self._closed = true
        self._raw:close()
    end
end

-- ── is_connected() → bool ───────────────────────────────────────────────

function TcpConnection:is_connected()
    if self._closed then
        return false
    end
    return self._raw:is_connected()
end

-- ── remote_addr() → string ──────────────────────────────────────────────

function TcpConnection:remote_addr()
    return self._remote_addr
end

-- ══════════════════════════════════════════════════════════════════════════════
-- TcpServer — listens for incoming TCP connections
-- ══════════════════════════════════════════════════════════════════════════════

local TcpServer = Class("TcpServer")

function TcpServer:__ctor()
    self._raw         = nil
    self._addr        = nil
    self._running     = false
    self._connections = {}     -- raw_conn → TcpConnection

    -- Public server-level callbacks (fallback when per-conn handlers absent)
    self.on_connect   = nil    -- function(conn)
    self.on_message   = nil    -- function(conn, data)
    self.on_close     = nil    -- function(conn)
    self.on_error     = nil    -- function(err_msg)
end

-- ── start(addr) → ok, err ───────────────────────────────────────────────

function TcpServer:start(addr)
    if type(addr) ~= "string" or addr == "" then
        error("TcpServer:start: addr must be a non-empty string, got " .. type(addr))
    end

    if self._running then
        self:stop()
    end

    self._addr = addr

    local ok, result = pcall(net.server.listen, addr)
    if not ok then
        local err = tostring(result)
        log_error("[TcpServer] listen on [" .. addr .. "] failed: " .. err)
        if self.on_error then
            self:_safe_callback(self.on_error, err)
        end
        return false, err
    end

    self._raw     = result
    self._running = true
    self:_bind_raw()

    log_info("[TcpServer] listening on [" .. addr .. "]")
    return true
end

-- ── stop() ──────────────────────────────────────────────────────────────

function TcpServer:stop()
    if not self._running then
        return
    end

    log_info("[TcpServer] stopping (addr=[" .. (self._addr or "?") .. "], "
             .. self:connection_count() .. " active connections)")

    self._running = false
    self._connections = {}

    if self._raw then
        self._raw:stop()
        self._raw = nil
    end
end

-- ── is_running() → bool ─────────────────────────────────────────────────

function TcpServer:is_running()
    return self._running
end

-- ── broadcast(data) — send to every connected client ────────────────────

function TcpServer:broadcast(data)
    for _, conn in pairs(self._connections) do
        if conn:is_connected() then
            conn:send(data)
        end
    end
end

-- ── connection_count() → number ─────────────────────────────────────────

function TcpServer:connection_count()
    local n = 0
    for _ in pairs(self._connections) do
        n = n + 1
    end
    return n
end

-- ── get_connections() → {TcpConnection, ...} ────────────────────────────

function TcpServer:get_connections()
    local list = {}
    for _, conn in pairs(self._connections) do
        list[#list + 1] = conn
    end
    return list
end

-- ══════════════════════════════════════════════════════════════════════════════
-- Internal
-- ══════════════════════════════════════════════════════════════════════════════

function TcpServer:_bind_raw()
    local this = self

    -- Intercept new connections: create TcpConnection wrapper, track it,
    -- wire up per-connection callbacks, then notify user.
    self._raw.on_connect = function(_, raw_conn, remote_addr)
        local conn = TcpConnection(raw_conn, remote_addr)
        this._connections[raw_conn] = conn

        -- Per-connection message handler: prefer conn.on_message, fall
        -- back to server.on_message.
        raw_conn.on_message = function(_, data)
            if conn.on_message then
                conn:_safe_callback(conn.on_message, data)
            elseif this.on_message then
                this:_safe_callback(this.on_message, conn, data)
            end
        end

        -- Per-connection close handler: prefer conn.on_close, fall back
        -- to server.on_close. Always clean up tracking.
        raw_conn.on_close = function(_, remote_addr)
            if not conn._closed then
                conn._closed = true
            end
            this._connections[raw_conn] = nil

            if conn.on_close then
                conn:_safe_callback(conn.on_close, remote_addr)
            end
            if this.on_close then
                this:_safe_callback(this.on_close, conn)
            end
        end

        log_info("[TcpServer] accepted connection from [" .. remote_addr .. "]")
        if this.on_connect then
            this:_safe_callback(this.on_connect, conn)
        end
    end
end

function TcpServer:_safe_callback(fn, ...)
    local ok, err = pcall(fn, ...)
    if not ok then
        log_error("[TcpServer] callback error: " .. tostring(err))
    end
end

-- Forward _safe_callback to TcpConnection as well
function TcpConnection:_safe_callback(fn, ...)
    local ok, err = pcall(fn, self, ...)
    if not ok then
        log_error("[TcpConnection] callback error: " .. tostring(err))
    end
end

return TcpServer
