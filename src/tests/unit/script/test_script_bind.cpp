#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <string>
#include <thread>

#include "log_init.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/script/bind/script_bind.h"
#include "runtime/vm/main_thread_vm.h"

using namespace engine;

namespace {

struct RuntimeBindingsFixture {
	MainThreadScriptVM vm;
	TimerManager timer_mgr;

	RuntimeBindingsFixture() {
		timer_mgr.initialize();
		vm.ExportRuntimeBindings(timer_mgr);
	}

	~RuntimeBindingsFixture() {
		vm.ShutdownNetworkBindings();
		vm.ShutdownTimerBindings();
		vm.ShutdownProfilerBindings();
		timer_mgr.shutdown();
	}

	bool RunLuaResult(const std::string& code, std::string& result) {
		return vm.DoString(code, "test_script_bind", nullptr, &result);
	}
};

}  // namespace

TEST_CASE("MainThreadScriptVM exports the complete core Lua API surface",
		  "[script_bind][runtime_bindings]") {
	RuntimeBindingsFixture f;
	std::string result;

	REQUIRE(f.RunLuaResult(
		R"lua(
local required = {
    log_info,
    log_warn,
    log_error,
    timer and timer.timeout,
    timer and timer.interval,
    timer and timer.cancel,
    net and net.client and net.client.connect,
    net and net.server and net.server.listen,
    net and net.http and net.http.get,
    entity and entity.create,
    cmsgpack and cmsgpack.pack,
    cmsgpack and cmsgpack.unpack,
    cmsgpack_safe and cmsgpack_safe.unpack,
    json and json.encode,
    json and json.decode,
    space and space.create,
    space and space.poll,
    aoi and aoi.init,
    rpc and rpc.new_server,
    rpc and rpc.new_client,
    auth and auth.authenticate,
    auth and auth.create_session,
    config and config.get,
    profiler and profiler.is_runtime_enabled,
    profiler and profiler.set_enabled_groups,
    import and import.setpath,
}

for i, fn in ipairs(required) do
    if type(fn) ~= 'function' then
        return 'bad:' .. i .. ':' .. type(fn)
    end
end

return 'ok'
)lua",
		result));
	REQUIRE(result == "ok");
}

TEST_CASE("MainThreadScriptVM exposes optional Lua modules when their features are enabled",
		  "[script_bind][runtime_bindings]") {
	RuntimeBindingsFixture f;
	std::string result;

#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
	REQUIRE(f.RunLuaResult(
		R"lua(
local required = {
    orm and orm.define,
    orm and orm.cache_stats,
    bson and bson.new,
    mongoc and mongoc.uri_new,
    db_is_running,
    db_metrics,
}

for i, fn in ipairs(required) do
    if type(fn) ~= 'function' then
        return 'bad:' .. i .. ':' .. type(fn)
    end
end

return 'ok'
)lua",
		result));
	REQUIRE(result == "ok");
#else
	REQUIRE(f.RunLuaResult(
		"return tostring(orm == nil) .. ',' .. tostring(bson == nil) .. ',' .. tostring(mongoc == nil)",
		result));
	REQUIRE(result == "true,true,true");
#endif

#if defined(ENGINE_MEM_STATS_ENABLED)
	REQUIRE(f.RunLuaResult("return type(mem.is_enabled) .. ',' .. type(mem.get_stats)", result));
	REQUIRE(result == "function,function");
#else
	REQUIRE(f.RunLuaResult("return tostring(mem == nil)", result));
	REQUIRE(result == "true");
#endif
}

TEST_CASE("MainThreadScriptVM rejects runtime binding export from non-owner thread",
		  "[script_bind][runtime_bindings]") {
	MainThreadScriptVM vm;
	TimerManager timer_mgr;
	std::string error;

	std::thread worker([&] {
		try {
			vm.ExportRuntimeBindings(timer_mgr);
		} catch (const std::exception& e) {
			error = e.what();
		}
	});
	worker.join();

	REQUIRE(error.find("MainThreadScriptVM::ExportRuntimeBindings") != std::string::npos);
	REQUIRE(error.find("owner thread") != std::string::npos);
}

TEST_CASE("Lua auth binding exposes permission and manual session APIs", "[script_bind][auth]") {
	RuntimeBindingsFixture f;
	std::string result;

	REQUIRE(f.RunLuaResult(
		R"lua(
auth.set_token_backend()

local before = auth.has_permission('player-1', 'admin')
auth.grant_permission('player-1', 'admin')
local after_grant = auth.has_permission('player-1', 'admin')
auth.revoke_permission('player-1', 'admin')
local after_revoke = auth.has_permission('player-1', 'admin')

local session_id = auth.create_session('player-1')
local valid_before_revoke = auth.validate_session(session_id)
auth.revoke_session(session_id)
local valid_after_revoke = auth.validate_session(session_id)
auth.cleanup_expired()

return table.concat({
    tostring(before),
    tostring(after_grant),
    tostring(after_revoke),
    tostring(valid_before_revoke),
    tostring(valid_after_revoke),
}, ',')
)lua",
		result));
	REQUIRE(result == "false,true,false,true,false");
}

TEST_CASE("Lua auth binding ignores non-string parameter keys during authenticate",
		  "[script_bind][auth]") {
	RuntimeBindingsFixture f;
	std::string result;

	REQUIRE(f.RunLuaResult(
		R"lua(
auth.set_token_backend()
auth.add_token('good-token', 'player-1')

local ok, entity_id, session_id = auth.authenticate('token', {
    [1] = 'ignored',
    token = 'good-token',
})

return table.concat({
    tostring(ok),
    tostring(entity_id),
    tostring(type(session_id) == 'string' and #session_id > 0),
}, ',')
)lua",
		result));
	REQUIRE(result == "true,player-1,true");
}

TEST_CASE("import.loaded reports corrupted package state without crashing",
		  "[script_bind][import]") {
	RuntimeBindingsFixture f;
	std::string result;

	REQUIRE(f.RunLuaResult(
		R"lua(
local original_package = package
local ok_missing, err_missing = pcall(function()
    package = nil
    return import.loaded()
end)
package = original_package

local original_loaded = package.loaded
local ok_bad_loaded, err_bad_loaded = pcall(function()
    package.loaded = false
    return import.loaded()
end)
package.loaded = original_loaded

return table.concat({
    tostring(ok_missing),
    tostring(type(err_missing) == 'string' and err_missing:find('package table') ~= nil),
    tostring(ok_bad_loaded),
    tostring(type(err_bad_loaded) == 'string' and err_bad_loaded:find('package.loaded') ~= nil),
}, ',')
)lua",
		result));
	REQUIRE(result == "false,true,false,true");
}

TEST_CASE("cmsgpack rejects cyclic tables and oversized unpack arguments",
		  "[script_bind][msgpack]") {
	RuntimeBindingsFixture f;
	std::string result;

	REQUIRE(f.RunLuaResult(
		R"lua(
local cyclic = {}
cyclic.self = cyclic
local ok_cycle, err_cycle = pcall(cmsgpack.pack, cyclic)

local huge = 9223372036854775807
local ok_offset, err_offset = pcall(cmsgpack.unpack_one, '', huge)
local ok_limit, err_limit = pcall(cmsgpack.unpack_limit, '', huge)
local value, safe_err = cmsgpack_safe.unpack_one('', huge)

return table.concat({
    tostring(ok_cycle),
    tostring(type(err_cycle) == 'string' and err_cycle:find('nesting depth') ~= nil),
    tostring(ok_offset),
    tostring(type(err_offset) == 'string' and err_offset:find('offset') ~= nil),
    tostring(ok_limit),
    tostring(type(err_limit) == 'string' and err_limit:find('limit') ~= nil),
    tostring(value == nil),
    tostring(type(safe_err) == 'string' and safe_err:find('offset') ~= nil),
}, ',')
)lua",
		result));
	REQUIRE(result == "false,true,false,true,false,true,true,true");
}

TEST_CASE("space binding rejects negative identifiers before unsigned conversion",
		  "[script_bind][space]") {
	RuntimeBindingsFixture f;
	std::string result;

	REQUIRE(f.RunLuaResult(
		R"lua(
local ok_get = pcall(space.get, -1)
local ok_destroy = pcall(space.destroy, -1)
local ok_space = pcall(space.send, -1, 1, 'x')
local ok_target = pcall(space.send, 1, -1, 'x')
local ok_source = pcall(space.send, 1, 1, 'x', -1)
return table.concat({
    tostring(ok_get == false),
    tostring(ok_destroy == false),
    tostring(ok_space == false),
    tostring(ok_target == false),
    tostring(ok_source == false),
}, ',')
)lua",
		result));
	REQUIRE(result == "true,true,true,true,true");
}
