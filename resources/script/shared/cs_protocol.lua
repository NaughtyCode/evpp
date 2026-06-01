local M = {}

M.version = 1
M.max_frame_size = 64 * 1024

function M.new_decoder()
    return {
        buffer = "",
    }
end

function M.encode(message)
    if type(message) ~= "table" then
        error("cs_protocol.encode expects a table")
    end
    if message.version == nil then
        message.version = M.version
    end
    return json.encode(message) .. "\n"
end

function M.feed(decoder, chunk, on_message)
    if type(decoder) ~= "table" then
        error("cs_protocol.feed decoder must be a table")
    end
    if type(chunk) ~= "string" then
        error("cs_protocol.feed chunk must be a string")
    end
    if type(on_message) ~= "function" then
        error("cs_protocol.feed on_message must be a function")
    end

    decoder.buffer = decoder.buffer .. chunk
    if #decoder.buffer > M.max_frame_size then
        decoder.buffer = ""
        return false, "frame buffer exceeded " .. tostring(M.max_frame_size) .. " bytes"
    end

    while true do
        local pos = string.find(decoder.buffer, "\n", 1, true)
        if not pos then
            break
        end

        local line = string.sub(decoder.buffer, 1, pos - 1)
        decoder.buffer = string.sub(decoder.buffer, pos + 1)

        if #line > 0 then
            local ok, message = pcall(json.decode, line)
            if not ok then
                return false, "invalid json frame: " .. tostring(message)
            end
            if type(message) ~= "table" then
                return false, "json frame must decode to an object"
            end
            if message.version ~= nil and message.version ~= M.version then
                return false, "unsupported protocol version: " .. tostring(message.version)
            end
            on_message(message)
        end
    end

    return true
end

function M.endpoint_from_config()
    local host = config.get("client.network.server_address") or "127.0.0.1"
    local port = config.get("client.network.server_port") or 7777
    return tostring(host) .. ":" .. tostring(port)
end

return M
