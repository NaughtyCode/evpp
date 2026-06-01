-- Server entry script for the Lua-only client/server demo.

import.addpath("resources/demos/cs/script")
import("runtime.init")

local TcpServer = import("runtime.net.server")
local protocol = import("shared.cs_protocol")

local server = nil
local sessions = {}
local next_session_id = 1
local server_frame = 0

local function send(conn, op, payload, seq)
    conn:send(protocol.encode({
        op = op,
        seq = seq,
        payload = payload or {},
    }))
end

local function broadcast(op, payload)
    for _, session in pairs(sessions) do
        if session.conn and session.conn:is_connected() then
            send(session.conn, op, payload)
        end
    end
end

local function handle_hello(session, message)
    session.authenticated = true
    session.player_id = "player-" .. tostring(session.id)

    send(session.conn, "welcome", {
        session_id = session.id,
        player_id = session.player_id,
        tick_rate = 30,
        server_frame = server_frame,
    }, message.seq)

    log_info("[CS Server] authenticated " .. session.player_id ..
             " from " .. session.conn:remote_addr())
    print("CS_SERVER_WELCOME " .. session.player_id)
end

local function handle_ping(session, message)
    send(session.conn, "pong", {
        client_time_ms = message.payload and message.payload.client_time_ms or 0,
        server_frame = server_frame,
    }, message.seq)
end

local function handle_input(session, message)
    if not session.authenticated then
        send(session.conn, "error", { reason = "input before hello" }, message.seq)
        return
    end

    local input = message.payload or {}
    local x = tonumber(input.x) or 0
    local y = tonumber(input.y) or 0
    session.x = x
    session.y = y

    send(session.conn, "authoritative_state", {
        player_id = session.player_id,
        x = session.x,
        y = session.y,
        server_frame = server_frame,
    }, message.seq)
end

local function handle_chat(session, message)
    if not session.authenticated then
        send(session.conn, "error", { reason = "chat before hello" }, message.seq)
        return
    end

    local text = ""
    if message.payload and message.payload.text then
        text = tostring(message.payload.text)
    end

    broadcast("server_event", {
        kind = "chat",
        player_id = session.player_id,
        text = text,
        server_frame = server_frame,
    })
end

local handlers = {
    hello = handle_hello,
    ping = handle_ping,
    input = handle_input,
    chat = handle_chat,
}

local function dispatch(session, message)
    local handler = handlers[message.op]
    if not handler then
        send(session.conn, "error", { reason = "unknown op: " .. tostring(message.op) }, message.seq)
        return
    end
    handler(session, message)
end

function InitScript()
    local endpoint = protocol.endpoint_from_config()

    server = TcpServer()
    server.on_connect = function(conn)
        local session = {
            id = next_session_id,
            conn = conn,
            decoder = protocol.new_decoder(),
            authenticated = false,
            player_id = "",
            x = 0,
            y = 0,
        }
        next_session_id = next_session_id + 1
        sessions[conn] = session

        conn.on_message = function(_, data)
            local ok, err = protocol.feed(session.decoder, data, function(message)
                dispatch(session, message)
            end)
            if not ok then
                log_error("[CS Server] protocol error: " .. tostring(err))
                send(conn, "error", { reason = err })
                conn:close()
            end
        end

        conn.on_close = function()
            sessions[conn] = nil
            log_info("[CS Server] client disconnected: " .. session.player_id)
        end

        log_info("[CS Server] accepted " .. conn:remote_addr())
    end

    server.on_error = function(err)
        log_error("[CS Server] listen error: " .. tostring(err))
    end

    local ok, err = server:start(endpoint)
    if not ok then
        log_error("[CS Server] failed to listen on " .. endpoint .. ": " .. tostring(err))
        print("CS_SERVER_START_FAILED " .. tostring(err))
        return
    end

    log_info("[CS Server] listening on " .. endpoint)
    print("CS_SERVER_LISTENING " .. endpoint)
end

function UpdateScript()
    server_frame = server_frame + 1
end

function DestroyScript()
    if server then
        server:stop()
        server = nil
    end
    log_info("[CS Server] shutdown")
end
