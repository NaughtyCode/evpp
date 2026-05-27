-- RPC Framework — msgpack-based service communication
-- Provides rpc.service(), rpc.call(), rpc.call_async()

local rpc = {}

-- Registered local services (name → handler table)
local services = {}

-- Define a local service with method handlers.
-- Usage:
--   local svc = rpc.service("PlayerService", {
--     get_info = function(args) return {name = "Alice"} end,
--   })
function rpc.service(name, handlers)
    local svc = { name = name, handlers = handlers }
    services[name] = svc
    return svc
end

-- Register a service (calls into C++ RpcServer).
function rpc.register_service(name, handler_table)
    services[name] = { name = name, handlers = handler_table }
end

-- Dispatch an incoming RPC request. Called by C++ transport layer.
-- Returns the response to send back.
function rpc.dispatch(service_name, method, args_json)
    local svc = services[service_name]
    if not svc then
        return false, "service not found: " .. tostring(service_name)
    end

    local handler = svc.handlers[method]
    if not handler then
        return false, "method not found: " .. tostring(method)
    end

    local ok, result = pcall(handler, args_json)
    if not ok then
        return false, tostring(result)
    end

    return true, result
end

-- Call a remote service (sync placeholder — actual call through C++ transport).
function rpc.call(service, method, args, options)
    options = options or {}
    local timeout = options.timeout_ms or 5000
    -- Delegate to C++ via the rpc_client global
    if _G._rpc_client and _G._rpc_client.call then
        return _G._rpc_client.call(service, method, args, timeout)
    end
    return nil, "rpc_client not initialized"
end

-- Async call with callback.
function rpc.call_async(service, method, args, callback, options)
    options = options or {}
    local timeout = options.timeout_ms or 5000
    if _G._rpc_client and _G._rpc_client.call_async then
        _G._rpc_client.call_async(service, method, args, callback, timeout)
    elseif callback then
        callback(false, "rpc_client not initialized")
    end
end

return rpc
