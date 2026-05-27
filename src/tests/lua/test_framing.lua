-- ═══════════════════════════════════════════════════════════════════════════
-- Integration test: length-prefixed message framing over TCP
--
-- This test verifies that the TCP server/client bindings correctly use
-- the LengthPrefixedCodec to split messages regardless of TCP segmentation.
-- Without framing, rapid sends would result in coalesced on_message calls.
-- With framing, each send() produces exactly one on_message call.
-- ═══════════════════════════════════════════════════════════════════════════

local harness = require("tests.harness.test_harness")

-- ── Test: multiple rapid sends arrive as separate messages ───────────────

local function test_rapid_messages()
    local received = {}
    local port = harness.find_free_port()

    local server = harness.create_server("127.0.0.1:" .. port, {
        on_connect = function(self, conn)
            conn.received = {}
            conn:set_on_message(function(self, data)
                table.insert(self.received, data)
            end)
        end,
    })

    local client = harness.create_client("127.0.0.1:" .. port, {
        on_connect = function(self)
            -- Send multiple messages rapidly — TCP may coalesce them into
            -- a single TCP segment, but the codec should split them.
            self:send("message_one")
            self:send("message_two")
            self:send("message_three")
        end,
    })

    harness.wait_for_condition(function()
        return #received >= 3
    end, 2000)

    harness.assert_equals(#received, 3, "should receive exactly 3 messages")
    harness.assert_equals(received[1], "message_one")
    harness.assert_equals(received[2], "message_two")
    harness.assert_equals(received[3], "message_three")

    client:disconnect()
    server:stop()
end

-- ── Test: large message round-trip ───────────────────────────────────────

local function test_large_message()
    local port = harness.find_free_port()
    local large_payload = string.rep("X", 10000)
    local received_data = nil

    local server = harness.create_server("127.0.0.1:" .. port, {
        on_connect = function(self, conn)
            conn:set_on_message(function(self, data)
                received_data = data
            end)
        end,
    })

    local client = harness.create_client("127.0.0.1:" .. port, {
        on_connect = function(self)
            self:send(large_payload)
        end,
    })

    harness.wait_for_condition(function()
        return received_data ~= nil
    end, 2000)

    harness.assert_equals(#received_data, #large_payload,
        "received payload length should match sent length")
    harness.assert_equals(received_data, large_payload)

    client:disconnect()
    server:stop()
end

-- ── Test: empty message round-trip ───────────────────────────────────────

local function test_empty_message()
    local port = harness.find_free_port()
    local received_empty = false

    local server = harness.create_server("127.0.0.1:" .. port, {
        on_connect = function(self, conn)
            conn:set_on_message(function(self, data)
                if #data == 0 then
                    received_empty = true
                end
            end)
        end,
    })

    local client = harness.create_client("127.0.0.1:" .. port, {
        on_connect = function(self)
            self:send("")
        end,
    })

    harness.wait_for_condition(function()
        return received_empty
    end, 2000)

    harness.assert_true(received_empty, "should receive empty message")

    client:disconnect()
    server:stop()
end

-- ── Test: binary data round-trip (not just text) ─────────────────────────

local function test_binary_data()
    local port = harness.find_free_port()
    local binary_payload = string.char(0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD)
    local received_binary = nil

    local server = harness.create_server("127.0.0.1:" .. port, {
        on_connect = function(self, conn)
            conn:set_on_message(function(self, data)
                received_binary = data
            end)
        end,
    })

    local client = harness.create_client("127.0.0.1:" .. port, {
        on_connect = function(self)
            self:send(binary_payload)
        end,
    })

    harness.wait_for_condition(function()
        return received_binary ~= nil
    end, 2000)

    harness.assert_equals(#received_binary, #binary_payload)
    harness.assert_equals(received_binary, binary_payload)

    client:disconnect()
    server:stop()
end

-- ── Run all tests ────────────────────────────────────────────────────────

harness.run_tests("TCP Framing", {
    test_rapid_messages = test_rapid_messages,
    test_large_message = test_large_message,
    test_empty_message = test_empty_message,
    test_binary_data = test_binary_data,
})
