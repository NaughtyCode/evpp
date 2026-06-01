-- Client entry script for the Lua-only client/server demo.

import("runtime.init")

local TcpClient = import("runtime.net.client")
local protocol = import("shared.cs_protocol")

local client = nil
local decoder = protocol.new_decoder()
local seq = 0
local endpoint = ""
local retries = 0
local success = false
local session_id = nil
local received_pong = false
local received_state = false
local hello_pending = false
local hello_sent = false
local timeout_ms = 5000
local elapsed_ms = 0
local reconnect_wait_ms = 0
local reconnect_elapsed_ms = 0
local success_disconnect_wait_ms = -1
local frame_ms = 33

local function next_seq()
    seq = seq + 1
    return seq
end

local function send(op, payload)
    if not client or not client:is_connected() then
        return false
    end
    local ok, err = pcall(function()
        client:send(protocol.encode({
            op = op,
            seq = next_seq(),
            payload = payload or {},
        }))
    end)
    if not ok then
        log_error("[CS Client] send failed op=" .. tostring(op) .. ": " .. tostring(err))
        return false
    end
    log_info("[CS Client] sent op=" .. tostring(op))
    return true
end

local function send_hello()
    if hello_sent then
        return
    end
    if send("hello", {
        account = "lua_demo_player",
        client_build = "release-script-demo",
        protocol = protocol.version,
    }) then
        hello_sent = true
        hello_pending = false
        print("CS_CLIENT_SENT hello")
    end
end

local function connect_to_server()
    log_info("[CS Client] connecting to " .. endpoint)
    local ok, err = client:connect(endpoint)
    if not ok then
        log_error("[CS Client] connect failed: " .. tostring(err))
        return false
    end
    return true
end

local function reset_roundtrip_state()
    decoder = protocol.new_decoder()
    session_id = nil
    received_pong = false
    received_state = false
    hello_pending = true
    hello_sent = false
    elapsed_ms = 0
    success_disconnect_wait_ms = -1
end

local function mark_success()
    if success then
        return
    end
    if session_id and received_pong and received_state then
        success = true
        log_info("[CS Client] completed server handshake and gameplay round trip")
        print("CS_CLIENT_SUCCESS session=" .. tostring(session_id))
        success_disconnect_wait_ms = 250
    end
end

local function handle_message(message)
    if message.op == "welcome" then
        local payload = message.payload or {}
        session_id = payload.session_id
        log_info("[CS Client] welcome session=" .. tostring(session_id) ..
                 " player=" .. tostring(payload.player_id))
        print("CS_CLIENT_WELCOME session=" .. tostring(session_id))

        send("ping", { client_time_ms = 1 })
        send("input", { x = 10, y = 20, buttons = "move" })
        send("chat", { text = "hello from lua client" })
        mark_success()
    elseif message.op == "pong" then
        received_pong = true
        log_info("[CS Client] pong from server frame=" ..
                 tostring((message.payload or {}).server_frame))
        mark_success()
    elseif message.op == "authoritative_state" then
        received_state = true
        local payload = message.payload or {}
        log_info("[CS Client] authoritative state x=" .. tostring(payload.x) ..
                 " y=" .. tostring(payload.y))
        mark_success()
    elseif message.op == "server_event" then
        local payload = message.payload or {}
        log_info("[CS Client] event " .. tostring(payload.kind) ..
                 " from " .. tostring(payload.player_id))
    elseif message.op == "error" then
        local payload = message.payload or {}
        log_error("[CS Client] server error: " .. tostring(payload.reason))
        print("CS_CLIENT_SERVER_ERROR " .. tostring(payload.reason))
    else
        log_warn("[CS Client] unknown op: " .. tostring(message.op))
    end
end

local function schedule_reconnect()
    if success then
        return
    end

    local max_retries = tonumber(config.get("client.network.reconnect_max_retries")) or 10
    if retries >= max_retries then
        print("CS_CLIENT_FAILED retries_exhausted")
        log_error("[CS Client] reconnect attempts exhausted")
        return
    end

    retries = retries + 1
    local base_delay = tonumber(config.get("client.network.reconnect_base_delay_ms")) or 500
    local max_delay = tonumber(config.get("client.network.reconnect_max_delay_ms")) or 30000
    reconnect_wait_ms = base_delay * retries
    if reconnect_wait_ms > max_delay then
        reconnect_wait_ms = max_delay
    end
    reconnect_elapsed_ms = 0
end

function InitScript()
    endpoint = protocol.endpoint_from_config()
    client = TcpClient()
    timeout_ms = tonumber(config.get("client.network.timeout_ms")) or 5000

    client.on_connect = function()
        retries = 0
        reset_roundtrip_state()
        log_info("[CS Client] connected to " .. endpoint)
        print("CS_CLIENT_CONNECTED " .. endpoint)
    end

    client.on_message = function(_, data)
        local ok, err = protocol.feed(decoder, data, handle_message)
        if not ok then
            log_error("[CS Client] protocol error: " .. tostring(err))
            print("CS_CLIENT_PROTOCOL_ERROR " .. tostring(err))
            client:disconnect()
        end
    end

    client.on_close = function()
        log_info("[CS Client] disconnected")
        schedule_reconnect()
    end

    client.on_error = function(_, err)
        log_error("[CS Client] connection error: " .. tostring(err))
        schedule_reconnect()
    end

    if not connect_to_server() then
        schedule_reconnect()
    end
end

function UpdateScript()
    if success then
        if success_disconnect_wait_ms >= 0 then
            success_disconnect_wait_ms = success_disconnect_wait_ms - frame_ms
            if success_disconnect_wait_ms <= 0 and client then
                success_disconnect_wait_ms = -1
                client:disconnect()
            end
        end
        return
    end

    if client and client:is_connected() then
        elapsed_ms = elapsed_ms + frame_ms
        if hello_pending and not hello_sent then
            send_hello()
        end
        if elapsed_ms >= timeout_ms then
            print("CS_CLIENT_FAILED timeout")
            log_error("[CS Client] server communication timed out")
            client:disconnect()
        end
        return
    end

    if reconnect_wait_ms > 0 then
        reconnect_elapsed_ms = reconnect_elapsed_ms + frame_ms
        if reconnect_elapsed_ms >= reconnect_wait_ms then
            reconnect_wait_ms = 0
            reconnect_elapsed_ms = 0
            log_info("[CS Client] reconnect attempt " .. tostring(retries) ..
                     " to " .. endpoint)
            if not connect_to_server() then
                schedule_reconnect()
            end
        end
    end
end

function DestroyScript()
    if client then
        client:disconnect()
        client = nil
    end
    log_info("[CS Client] shutdown")
end
