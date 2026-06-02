#include <catch2/catch_test_macros.hpp>

#include <string>

#include "log_init.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/script/script_bind.h"
#include "runtime/vm/vm.h"

using namespace engine;

namespace {

struct ExportAllFixture {
	ScriptVM vm;
	TimerManager timer_mgr;

	ExportAllFixture() {
		timer_mgr.initialize();
		script::ExportAll(vm, timer_mgr);
	}

	~ExportAllFixture() {
		script::ShutdownRpcBindings(vm);
		script::ShutdownConfigBindings(vm);
		script::ShutdownNetBindings();
		script::ShutdownEntityBindings();
		script::ShutdownTimerBindings(vm);
		timer_mgr.shutdown();
	}

	bool RunLuaResult(const std::string& code, std::string& result) {
		return vm.DoString(code, "test_script_bind", nullptr, &result);
	}
};

}  // namespace

TEST_CASE("ExportAll exports the complete core Lua API surface", "[script_bind][export_all]") {
	ExportAllFixture f;
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

TEST_CASE("ExportAll exposes optional Lua modules when their features are enabled",
		  "[script_bind][export_all]") {
	ExportAllFixture f;
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

TEST_CASE("Lua auth binding exposes permission and manual session APIs", "[script_bind][auth]") {
	ExportAllFixture f;
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
