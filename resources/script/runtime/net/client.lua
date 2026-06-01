-- net/client.lua
-- TcpClient wrapper around net.client C++ bindings.
--
-- Provides a stateful, callback-driven TCP client with:
--   - Automatic state tracking (disconnected → connecting → connected)
--   - Event callbacks (on_connect, on_message, on_close, on_error)
--   - Safe disconnect that prevents use-after-free
--
-- Usage:
--   local TcpClient = import("runtime.net.client")
--   local client = TcpClient()
--   client.on_connect = function(self) ... end
--   client.on_message = function(self, data) ... end
--   client.on_close   = function(self) ... end
--   client:connect("127.0.0.1:8080")

local Class = import("runtime.common.class")
local TcpClient = Class("TcpClient")

-- States
local DISCONNECTED = 0
local CONNECTING   = 1
local CONNECTED    = 2

-- State name lookup for logging
local STATE_NAMES = {
    [DISCONNECTED] = "disconnected",
    [CONNECTING]   = "connecting",
    [CONNECTED]    = "connected",
}

-- ── Constructor ─────────────────────────────────────────────────────────────

function TcpClient:__ctor()
    self._state      = DISCONNECTED
    self._raw        = nil
    self._addr       = nil

    -- Public callbacks — set these before calling connect()
    self.on_connect  = nil   -- function(self)
    self.on_message  = nil   -- function(self, data)
    self.on_close    = nil   -- function(self)
    self.on_error    = nil   -- function(self, err_msg)
end

-- ── connect(addr) → ok, err ─────────────────────────────────────────────────

function TcpClient:connect(addr)
    if type(addr) ~= "string" or addr == "" then
        error("TcpClient:connect: addr must be a non-empty string, got " .. type(addr))
    end

    if self._state ~= DISCONNECTED then
        self:disconnect()
    end

    self._addr   = addr
    self._state  = CONNECTING

    local ok, result = pcall(net.client.connect, addr)
    if not ok then
        self._state = DISCONNECTED
        local err = tostring(result)
        log_error("[TcpClient] connect to [" .. addr .. "] failed: " .. err)
        if self.on_error then
            self:_safe_callback(self.on_error, err)
        end
        return false, err
    end

    self._raw = result
    self:_bind_raw()

    log_info("[TcpClient] connecting to [" .. addr .. "] ...")
    return true
end

-- ── send(data) ──────────────────────────────────────────────────────────────

function TcpClient:send(data)
    if not self._raw then
        error("TcpClient:send: not connected (state=" .. STATE_NAMES[self._state] .. ")")
    end
    return self._raw:send(data)
end

-- ── disconnect() ────────────────────────────────────────────────────────────

function TcpClient:disconnect()
    if self._raw then
        log_info("[TcpClient] disconnecting from [" .. (self._addr or "?") .. "]")
        self._raw:disconnect()
        self._raw = nil
    end
    self._state = DISCONNECTED
end

-- ── is_connected() → bool ───────────────────────────────────────────────────

function TcpClient:is_connected()
    return self._state == CONNECTED
end

-- ── get_state() → state_number ──────────────────────────────────────────────

function TcpClient:get_state()
    return self._state
end

-- ── get_state_name() → string ───────────────────────────────────────────────

function TcpClient:get_state_name()
    return STATE_NAMES[self._state] or "unknown"
end

-- ══════════════════════════════════════════════════════════════════════════════
-- Internal
-- ══════════════════════════════════════════════════════════════════════════════

-- Wire up the raw net.client instance callbacks.
-- The C++ dispatcher calls on_connect / on_message / on_close as methods
-- on the raw instance table, passing it as self. We use closures to
-- capture the wrapper instance (this) and delegate to user callbacks.
function TcpClient:_bind_raw()
    local this = self

    self._raw.on_connect = function(_)
        this._state  = CONNECTED
        log_info("[TcpClient] connected to [" .. (this._addr or "?") .. "]")
        if this.on_connect then
            this:_safe_callback(this.on_connect)
        end
    end

    self._raw.on_message = function(_, data)
        if this.on_message then
            this:_safe_callback(this.on_message, data)
        end
    end

    self._raw.on_close = function(_)
        if this._state == DISCONNECTED then
            return  -- already disconnected via disconnect()
        end
        this._state = DISCONNECTED
        this._raw   = nil
        log_info("[TcpClient] connection closed (remote)")
        if this.on_close then
            this:_safe_callback(this.on_close)
        end
    end
end

-- Call a user callback protected, logging any error without crashing.
function TcpClient:_safe_callback(fn, ...)
    local ok, err = pcall(fn, self, ...)
    if not ok then
        log_error("[TcpClient] callback error: " .. tostring(err))
    end
end

return TcpClient
