-- tests/test_net_client_server.lua
-- Client-Server architecture test for the net wrapper classes.
--
-- Demonstrates and validates the TcpClient / TcpServer encapsulation with
-- async callback-driven tests: echo, multi-message, multi-client, reconnect,
-- broadcast.
--
-- Run from InitScript:
--   import("tests.test_net_client_server").run()

local TcpClient = import("runtime.net.client")
local TcpServer = import("runtime.net.server")

-- ══════════════════════════════════════════════════════════════════════════════
-- Test framework
-- ══════════════════════════════════════════════════════════════════════════════

local passed = 0
local failed = 0
local current_test = ""

local function start(name)
    current_test = name
    log_info("")
    log_info("══════════════════════════════════════════════════════════════")
    log_info("  TEST: " .. name)
    log_info("══════════════════════════════════════════════════════════════")
end

local function ok(cond, msg)
    if cond then
        passed = passed + 1
        log_info("  [PASS] " .. (msg or "ok"))
    else
        failed = failed + 1
        log_error("  [FAIL] " .. current_test .. ": " .. (msg or "assertion failed"))
    end
end

local function eq(a, b, msg)
    local equal = (a == b)
    if not equal then
        ok(false, (msg or "eq") .. " — expected [" .. tostring(b) .. "], got [" .. tostring(a) .. "]")
    else
        ok(true, msg)
    end
end

local function summary()
    local total = passed + failed
    log_info("")
    log_info("══════════════════════════════════════════════════════════════")
    if failed == 0 then
        log_info("  RESULT: ALL " .. total .. " assertion(s) PASSED")
    else
        log_error("  RESULT: " .. passed .. "/" .. total .. " passed, "
                  .. failed .. " FAILED")
    end
    log_info("══════════════════════════════════════════════════════════════")
end

-- NOP test to verify the framework works even without network
local function test_framework()
    start("Framework sanity")
    eq(1 + 1, 2, "basic math")
    eq("hello" .. " world", "hello world", "string concat")
    eq(type({}), "table", "type check")
end

-- ══════════════════════════════════════════════════════════════════════════════
-- Test 1: Single-client echo
-- ══════════════════════════════════════════════════════════════════════════════

local function test_echo(next_fn)
    start("Single-client echo")

    local server = TcpServer()
    local client = TcpClient()

    server.on_connect = function(conn)
        log_info("  [server] client connected: " .. conn:remote_addr())
        ok(conn:is_connected(), "server-side conn is connected")

        -- Echo handler: prepend "ECHO: " and send back
        conn.on_message = function(conn, data)
            log_info("  [server] received: " .. data)
            conn:send("ECHO: " .. data)
        end
    end

    server.on_close = function(conn)
        log_info("  [server] client disconnected: " .. conn:remote_addr())
    end

    client.on_connect = function()
        ok(client:is_connected(), "client reports connected")
        log_info("  [client] connected, sending greeting")
        client:send("Hello, Server!")
    end

    client.on_message = function(_, data)
        log_info("  [client] received: " .. data)
        eq(data, "ECHO: Hello, Server!", "echo response matches")
        client:disconnect()
    end

    client.on_close = function()
        ok(not client:is_connected(), "client reports disconnected after close")
        log_info("  [client] disconnected")
        -- Defer server stop to next event-loop tick
        timer.timeout(50, function()
            server:stop()
            ok(not server:is_running(), "server stopped")
            timer.timeout(50, next_fn)
        end)
    end

    server.on_error = function(err)
        ok(false, "server error: " .. tostring(err))
    end
    client.on_error = function(err)
        ok(false, "client error: " .. tostring(err))
    end

    ok(server:start("127.0.0.1:19991"), "server starts on 127.0.0.1:19991")
    ok(client:connect("127.0.0.1:19991"), "client connects to 127.0.0.1:19991")
end

-- ══════════════════════════════════════════════════════════════════════════════
-- Test 2: Multiple messages in a single session
-- ══════════════════════════════════════════════════════════════════════════════

local function test_multi_message(next_fn)
    start("Multiple messages (batch)")

    local server = TcpServer()
    local client = TcpClient()
    local expected = { "msg1", "msg2", "msg3", "hello", "world" }
    local received = {}
    local echoed = {}

    server.on_connect = function(conn)
        conn.on_message = function(_, data)
            table.insert(echoed, data)
            conn:send("ACK: " .. data)
        end
    end

    client.on_connect = function()
        for _, msg in ipairs(expected) do
            client:send(msg)
        end
        log_info("  [client] sent " .. #expected .. " messages")
    end

    client.on_message = function(_, data)
        table.insert(received, data)
        if #received == #expected then
            -- All responses received
            eq(#received, #expected, "received same count as sent")
            for i, msg in ipairs(expected) do
                eq(received[i], "ACK: " .. msg, "response " .. i .. " matches")
            end
            client:disconnect()
        end
    end

    client.on_close = function()
        timer.timeout(50, function()
            server:stop()
            timer.timeout(50, next_fn)
        end)
    end

    server.on_error = function(err)
        ok(false, "server error: " .. tostring(err))
    end
    client.on_error = function(err)
        ok(false, "client error: " .. tostring(err))
    end

    ok(server:start("127.0.0.1:19992"), "server start")
    ok(client:connect("127.0.0.1:19992"), "client connect")
end

-- ══════════════════════════════════════════════════════════════════════════════
-- Test 3: Multi-client — two clients connected simultaneously
-- ══════════════════════════════════════════════════════════════════════════════

local function test_multi_client(next_fn)
    start("Multi-client (2 concurrent connections)")

    local server = TcpServer()
    local client_a = TcpClient()
    local client_b = TcpClient()
    local conn_count = 0
    local a_done = false
    local b_done = false

    local function check_done()
        if a_done and b_done then
            timer.timeout(50, function()
                server:stop()
                timer.timeout(50, next_fn)
            end)
        end
    end

    server.on_connect = function(conn)
        conn_count = conn_count + 1
        eq(server:connection_count(), conn_count,
           "server connection count = " .. conn_count)

        conn.on_message = function(conn, data)
            log_info("  [server] from [" .. conn:remote_addr() .. "]: " .. data)
            -- Echo back with connection identifier
            conn:send("reply_to_" .. data)
        end
    end

    server.on_close = function(conn)
        conn_count = conn_count - 1
    end

    -- Client A
    client_a.on_connect = function()
        log_info("  [client A] connected")
        client_a:send("A")
    end
    client_a.on_message = function(_, data)
        eq(data, "reply_to_A", "client A gets correct reply")
        a_done = true
        client_a:disconnect()
    end
    client_a.on_close = function()
        log_info("  [client A] disconnected")
        check_done()
    end

    -- Client B
    client_b.on_connect = function()
        log_info("  [client B] connected")
        client_b:send("B")
    end
    client_b.on_message = function(_, data)
        eq(data, "reply_to_B", "client B gets correct reply")
        b_done = true
        client_b:disconnect()
    end
    client_b.on_close = function()
        log_info("  [client B] disconnected")
        check_done()
    end

    server.on_error = function(err)
        ok(false, "server error: " .. tostring(err))
    end
    client_a.on_error = function(err)
        ok(false, "client A error: " .. tostring(err))
    end
    client_b.on_error = function(err)
        ok(false, "client B error: " .. tostring(err))
    end

    ok(server:start("127.0.0.1:19993"), "server start")
    ok(client_a:connect("127.0.0.1:19993"), "client A connect")
    -- Small stagger so connections arrive in order
    timer.timeout(50, function()
        ok(client_b:connect("127.0.0.1:19993"), "client B connect")
    end)
end

-- ══════════════════════════════════════════════════════════════════════════════
-- Test 4: Disconnect & reconnect
-- ══════════════════════════════════════════════════════════════════════════════

local function test_reconnect(next_fn)
    start("Disconnect & reconnect")

    local server = TcpServer()
    local client = TcpClient()
    local connect_count = 0
    local phase = 0          -- 1 = first connect, 2 = reconnect

    server.on_connect = function(conn)
        connect_count = connect_count + 1
        log_info("  [server] connection #" .. connect_count .. " from " .. conn:remote_addr())

        conn.on_message = function(conn, data)
            conn:send("PONG:" .. data)
        end

        if phase == 1 then
            -- First connect: send a message then disconnect
            client:send("ping1")
        elseif phase == 2 then
            -- Reconnect: send another message
            client:send("ping2")
        end
    end

    client.on_connect = function()
        log_info("  [client] connected (phase " .. phase .. ")")
    end

    client.on_message = function(_, data)
        log_info("  [client] received: " .. data)
        if phase == 1 then
            eq(data, "PONG:ping1", "first response correct")
            -- Disconnect and prepare for reconnect
            client:disconnect()
        elseif phase == 2 then
            eq(data, "PONG:ping2", "reconnect response correct")
            client:disconnect()
        end
    end

    client.on_close = function()
        log_info("  [client] disconnected (phase " .. phase .. ")")
        if phase == 1 then
            ok(connect_count == 1, "one connection so far")
            phase = 2
            -- Reconnect after a brief pause
            timer.timeout(100, function()
                log_info("  [client] reconnecting ...")
                ok(client:connect("127.0.0.1:19994"), "client reconnect")
            end)
        elseif phase == 2 then
            ok(connect_count == 2, "two connections total")
            timer.timeout(50, function()
                server:stop()
                timer.timeout(50, next_fn)
            end)
        end
    end

    server.on_error = function(err)
        ok(false, "server error: " .. tostring(err))
    end
    client.on_error = function(err)
        ok(false, "client error: " .. tostring(err))
    end

    phase = 1
    ok(server:start("127.0.0.1:19994"), "server start")
    ok(client:connect("127.0.0.1:19994"), "client initial connect")
end

-- ══════════════════════════════════════════════════════════════════════════════
-- Test 5: Server broadcast
-- ══════════════════════════════════════════════════════════════════════════════

local function test_broadcast(next_fn)
    start("Server broadcast")

    local server = TcpServer()
    local client_a = TcpClient()
    local client_b = TcpClient()
    local a_received = false
    local b_received = false

    local function try_done()
        if a_received and b_received then
            timer.timeout(50, function()
                client_a:disconnect()
                client_b:disconnect()
                server:stop()
                timer.timeout(50, next_fn)
            end)
        end
    end

    server.on_connect = function(conn)
        log_info("  [server] new connection, count=" .. server:connection_count())
        -- When both clients are connected, broadcast
        if server:connection_count() == 2 then
            timer.timeout(30, function()
                log_info("  [server] broadcasting to " .. server:connection_count() .. " clients")
                server:broadcast("HELLO_ALL")
            end)
        end
    end

    server.on_message = function(conn, data)
        log_info("  [server] msg from [" .. conn:remote_addr() .. "]: " .. data)
    end

    server.on_close = function(conn)
        log_info("  [server] client left, count=" .. server:connection_count())
    end

    client_a.on_message = function(_, data)
        eq(data, "HELLO_ALL", "client A received broadcast")
        a_received = true
        try_done()
    end

    client_b.on_message = function(_, data)
        eq(data, "HELLO_ALL", "client B received broadcast")
        b_received = true
        try_done()
    end

    client_a.on_error = function(err)
        ok(false, "client A error: " .. tostring(err))
    end
    client_b.on_error = function(err)
        ok(false, "client B error: " .. tostring(err))
    end
    server.on_error = function(err)
        ok(false, "server error: " .. tostring(err))
    end

    ok(server:start("127.0.0.1:19995"), "server start")
    ok(client_a:connect("127.0.0.1:19995"), "client A connect")
    timer.timeout(30, function()
        ok(client_b:connect("127.0.0.1:19995"), "client B connect")
    end)
end

-- ══════════════════════════════════════════════════════════════════════════════
-- Test runner — chains tests sequentially
-- ══════════════════════════════════════════════════════════════════════════════

local function run()
    log_info("")
    log_info("╔══════════════════════════════════════════════════════════════╗")
    log_info("║     Net Client-Server Architecture Test Suite               ║")
    log_info("╚══════════════════════════════════════════════════════════════╝")

    passed = 0
    failed = 0

    -- Build the test chain from last to first
    local chain = function()
        summary()
    end

    chain = function() test_broadcast(chain) end
    chain = function() test_reconnect(chain) end
    chain = function() test_multi_client(chain) end
    chain = function() test_multi_message(chain) end
    chain = function() test_echo(chain) end

    -- Start with framework sanity check (synchronous), then begin async chain
    test_framework()
    timer.timeout(100, chain)
end

return {
    run = run,
    -- Expose individual tests for selective execution
    test_echo          = test_echo,
    test_multi_message = test_multi_message,
    test_multi_client  = test_multi_client,
    test_reconnect     = test_reconnect,
    test_broadcast     = test_broadcast,
}
