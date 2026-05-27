#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/data_service/db_service_main_bind.h"

#include <cstring>

#include "runtime/database/data_service/database_service.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

namespace {

// ── String → DbOperation mapping (case-insensitive) ───────────────────────
//
// Lowercases the input string and matches against known operation names.
// Returns false for unrecognized strings (no silent fallback to kNoOp).

bool ParseOperationName(const char* s, DbOperation* out) {
	if (!s || !out) return false;

	// case-fold into buf
	char buf[32];
	int i = 0;
	for (; s[i] && i < 31; ++i) {
		char c = s[i];
		buf[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
	}
	buf[i] = '\0';

#define OP_MATCH(name, op)             \
	if (std::strcmp(buf, name) == 0) { \
		*out = DbOperation::op;        \
		return true;                   \
	}

	OP_MATCH("find", kFind);
	OP_MATCH("find_one", kFindOne);
	OP_MATCH("insert_one", kInsertOne);
	OP_MATCH("insert_many", kInsertMany);
	OP_MATCH("update_one", kUpdateOne);
	OP_MATCH("update_many", kUpdateMany);
	OP_MATCH("delete_one", kDeleteOne);
	OP_MATCH("delete_many", kDeleteMany);
	OP_MATCH("count", kCount);
	OP_MATCH("aggregate", kAggregate);
	OP_MATCH("command", kCommand);
	OP_MATCH("execute_script", kExecuteScript);
	OP_MATCH("noop", kNoOp);

#undef OP_MATCH
	return false;
}

// ── Status query helpers ──────────────────────────────────────────────────

int l_db_is_running(lua_State* L) {
	lua_pushboolean(L, DatabaseService::Instance().IsRunning() ? 1 : 0);
	return 1;
}

int l_db_is_healthy(lua_State* L) {
	lua_pushboolean(L, DatabaseService::Instance().IsHealthy() ? 1 : 0);
	return 1;
}

int l_db_get_thread_count(lua_State* L) {
	lua_pushinteger(L, DatabaseService::Instance().GetThreadCount());
	return 1;
}

// ── l_db_send_request — parse Lua table → DbRequest → SendRequest ─────────
//
// Reads the table at stack index 1, extracts known fields, and routes the
// request through DatabaseService::SendRequest(). Returns true if the
// request was successfully enqueued.

int l_db_send_request(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);

	DbRequest req;

	// request_id (optional, default 0)
	lua_getfield(L, 1, "request_id");
	if (lua_isinteger(L, -1)) {
		lua_Integer v = lua_tointeger(L, -1);
		if (v < 0) {
			lua_pop(L, 1);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "db_send_request: request_id must be non-negative");
			return 2;
		}
		req.request_id = static_cast<uint64_t>(v);
	} else if (lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: request_id must be an integer");
		return 2;
	}
	lua_pop(L, 1);

	// operation (required, string or integer)
	lua_getfield(L, 1, "operation");
	if (lua_isinteger(L, -1)) {
		lua_Integer v = lua_tointeger(L, -1);
		if (v < static_cast<lua_Integer>(DbOperation::kNoOp) ||
			v > static_cast<lua_Integer>(DbOperation::kExecuteScript)) {
			lua_pop(L, 1);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "db_send_request: operation integer out of range");
			return 2;
		}
		req.operation = static_cast<DbOperation>(v);
	} else if (lua_isstring(L, -1)) {
		DbOperation op;
		if (!ParseOperationName(lua_tostring(L, -1), &op)) {
			lua_pop(L, 1);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "db_send_request: unknown operation name");
			return 2;
		}
		req.operation = op;
	} else {
		lua_pop(L, 1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: 'operation' field is required (string or int)");
		return 2;
	}
	lua_pop(L, 1);

	// database
	lua_getfield(L, 1, "database");
	if (lua_isstring(L, -1)) {
		req.database = lua_tostring(L, -1);
	}
	lua_pop(L, 1);

	// collection
	lua_getfield(L, 1, "collection");
	if (lua_isstring(L, -1)) {
		req.collection = lua_tostring(L, -1);
	}
	lua_pop(L, 1);

	// bson_data
	lua_getfield(L, 1, "bson_data");
	if (lua_isstring(L, -1)) {
		req.bson_data = lua_tostring(L, -1);
	}
	lua_pop(L, 1);

	// bson_data2
	lua_getfield(L, 1, "bson_data2");
	if (lua_isstring(L, -1)) {
		req.bson_data2 = lua_tostring(L, -1);
	}
	lua_pop(L, 1);

	// script
	lua_getfield(L, 1, "script");
	if (lua_isstring(L, -1)) {
		req.script = lua_tostring(L, -1);
	}
	lua_pop(L, 1);

	// limit
	lua_getfield(L, 1, "limit");
	if (lua_isinteger(L, -1)) {
		lua_Integer v = lua_tointeger(L, -1);
		if (v < 0 || v > INT32_MAX) {
			lua_pop(L, 1);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "db_send_request: limit must be in [0, 2147483647]");
			return 2;
		}
		req.limit = static_cast<int32_t>(v);
	} else if (lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: limit must be an integer");
		return 2;
	}
	lua_pop(L, 1);

	// skip
	lua_getfield(L, 1, "skip");
	if (lua_isinteger(L, -1)) {
		lua_Integer v = lua_tointeger(L, -1);
		if (v < 0 || v > INT32_MAX) {
			lua_pop(L, 1);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "db_send_request: skip must be in [0, 2147483647]");
			return 2;
		}
		req.skip = static_cast<int32_t>(v);
	} else if (lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: skip must be an integer");
		return 2;
	}
	lua_pop(L, 1);

	bool ok = DatabaseService::Instance().SendRequest(std::move(req));
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

// ── l_db_poll_response — PollResponse → Lua table ────────────────────────
//
// Calls DatabaseService::PollResponse() and converts the result into a Lua
// table. Returns nil if no response is available (non-blocking).

int l_db_poll_response(lua_State* L) {
	auto resp = DatabaseService::Instance().PollResponse();
	if (!resp) {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);
	lua_pushinteger(L, static_cast<lua_Integer>(resp->request_id));
	lua_setfield(L, -2, "request_id");
	lua_pushboolean(L, resp->success ? 1 : 0);
	lua_setfield(L, -2, "success");
	lua_pushinteger(L, static_cast<lua_Integer>(resp->error_code));
	lua_setfield(L, -2, "error_code");
	lua_pushstring(L, resp->error_message.c_str());
	lua_setfield(L, -2, "error_message");
	lua_pushstring(L, resp->result_data.c_str());
	lua_setfield(L, -2, "result_data");
	lua_pushinteger(L, static_cast<lua_Integer>(resp->affected_count));
	lua_setfield(L, -2, "affected_count");
	return 1;
}

const luaL_Reg kDbServiceFunctions[] = {
	{"db_is_running", l_db_is_running},
	{"db_is_healthy", l_db_is_healthy},
	{"db_get_thread_count", l_db_get_thread_count},
	{"db_send_request", l_db_send_request},
	{"db_poll_response", l_db_poll_response},
	{nullptr, nullptr},
};

}  // namespace

void ExportDbService(ScriptVM& vm) {
	vm.RegisterFunctions(kDbServiceFunctions);
}

}  // namespace script
}  // namespace engine

#endif	// ENGINE_MONGODB_ENABLED
