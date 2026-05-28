-- RPC Framework — Lua companion for the C++ rpc bindings.
--
-- The core API (rpc.new_server, rpc.new_client, and all instance
-- methods) is provided by the C++ engine via ExportRpc(), which
-- registers the global "rpc" table.
--
-- This file exists so that require("rpc") returns the C++ module
-- rather than falling through to a file-system search that might
-- find a stale or conflicting module.  It does NOT redefine rpc.

local rpc = rawget(_G, "rpc")
if type(rpc) ~= "table" then
	-- C++ module not yet registered — engine init hasn't run.
	-- Return an empty table so require() doesn't crash; the
	-- caller should detect missing functions.
	rpc = {}
end

return rpc
