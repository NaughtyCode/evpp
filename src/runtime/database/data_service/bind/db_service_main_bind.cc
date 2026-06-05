#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/data_service/bind/db_service_main_bind.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "runtime/database/data_service/database_service.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

namespace {

// ── String → DbOperation mapping (case-insensitive) ───────────────────────
//
// Matches against known operation names without accepting embedded-NUL
// truncation or fixed-buffer prefixes. Returns false for unrecognized strings
// (no silent fallback to kNoOp).

bool EqualsAsciiCaseInsensitive(std::string_view value, std::string_view expected) {
	if (value.size() != expected.size()) return false;
	for (size_t i = 0; i < value.size(); ++i) {
		char c = value[i];
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
		if (c != expected[i]) return false;
	}
	return true;
}

bool ParseOperationName(std::string_view s, DbOperation* out) {
	if (!out) return false;

#define OP_MATCH(name, op)                         \
	if (EqualsAsciiCaseInsensitive(s, name)) {     \
		*out = DbOperation::op;                    \
		return true;                               \
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

bool ReadOptionalStringField(lua_State* L,
							 int table_index,
							 const char* field,
							 std::string* out,
							 std::string& error) {
	lua_getfield(L, table_index, field);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return true;
	}
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		error = std::string("db_send_request: ") + field + " must be a string";
		return false;
	}

	size_t len = 0;
	const char* value = lua_tolstring(L, -1, &len);
	out->assign(value ? value : "", value ? len : 0);
	lua_pop(L, 1);
	return true;
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

int l_db_next_request_id(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(DatabaseService::Instance().NextRequestId()));
	return 1;
}

int l_db_metrics(lua_State* L) {
	auto& service = DatabaseService::Instance();
	lua_newtable(L);
	lua_pushinteger(L, static_cast<lua_Integer>(service.GetTotalEnqueued()));
	lua_setfield(L, -2, "enqueued");
	lua_pushinteger(L, static_cast<lua_Integer>(service.GetTotalDropped()));
	lua_setfield(L, -2, "dropped");
	lua_pushinteger(L, static_cast<lua_Integer>(service.GetTotalCompleted()));
	lua_setfield(L, -2, "completed");
	lua_pushinteger(L, static_cast<lua_Integer>(service.GetTotalErrors()));
	lua_setfield(L, -2, "errors");
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
	} else if (lua_type(L, -1) == LUA_TSTRING) {
		size_t len = 0;
		const char* op_name = lua_tolstring(L, -1, &len);
		DbOperation op;
		if (!ParseOperationName(std::string_view(op_name ? op_name : "", op_name ? len : 0),
								&op)) {
			lua_pop(L, 1);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "db_send_request: unknown operation name");
			return 2;
		}
		req.operation = op;
	} else if (lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: operation must be an integer or string");
		return 2;
	} else {
		lua_pop(L, 1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: 'operation' field is required (string or int)");
		return 2;
	}
	lua_pop(L, 1);

	if (req.operation == DbOperation::kNoOp) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: noop is an internal operation");
		return 2;
	}

	std::string string_error;
	if (!ReadOptionalStringField(L, 1, "database", &req.database, string_error) ||
		!ReadOptionalStringField(L, 1, "collection", &req.collection, string_error) ||
		!ReadOptionalStringField(L, 1, "bson_data", &req.bson_data, string_error) ||
		!ReadOptionalStringField(L, 1, "bson_data2", &req.bson_data2, string_error) ||
		!ReadOptionalStringField(L, 1, "script", &req.script, string_error)) {
		lua_pushboolean(L, 0);
		lua_pushlstring(L, string_error.data(), string_error.size());
		return 2;
	}

	// limit
	lua_getfield(L, 1, "limit");
	if (lua_isinteger(L, -1)) {
		lua_Integer v = lua_tointeger(L, -1);
		if (v < 0 || v > std::numeric_limits<int32_t>::max()) {
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
		if (v < 0 || v > std::numeric_limits<int32_t>::max()) {
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

	// max_result_documents
	lua_getfield(L, 1, "max_result_documents");
	if (lua_isinteger(L, -1)) {
		lua_Integer v = lua_tointeger(L, -1);
		if (v < 0 || v > std::numeric_limits<uint32_t>::max()) {
			lua_pop(L, 1);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "db_send_request: max_result_documents must be in [0, 4294967295]");
			return 2;
		}
		req.max_result_documents = static_cast<uint32_t>(v);
	} else if (lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: max_result_documents must be an integer");
		return 2;
	}
	lua_pop(L, 1);

	// allow_empty_filter
	lua_getfield(L, 1, "allow_empty_filter");
	if (lua_isboolean(L, -1)) {
		req.allow_empty_filter = lua_toboolean(L, -1) != 0;
	} else if (!lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "db_send_request: allow_empty_filter must be boolean");
		return 2;
	}
	lua_pop(L, 1);

	if (req.request_id == 0) {
		req.request_id = DatabaseService::Instance().NextRequestId();
	}
	const uint64_t request_id = req.request_id;
	bool ok = DatabaseService::Instance().SendRequest(std::move(req));
	lua_pushboolean(L, ok ? 1 : 0);
	if (ok) {
		lua_pushinteger(L, static_cast<lua_Integer>(request_id));
	} else {
		lua_pushstring(L, "database service is not running or request queue is full");
	}
	return 2;
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
	lua_pushinteger(L, static_cast<lua_Integer>(resp->status));
	lua_setfield(L, -2, "status");
	lua_pushboolean(L, resp->success ? 1 : 0);
	lua_setfield(L, -2, "success");
	lua_pushinteger(L, static_cast<lua_Integer>(resp->error_code));
	lua_setfield(L, -2, "error_code");
	lua_pushlstring(L, resp->error_message.data(), resp->error_message.size());
	lua_setfield(L, -2, "error_message");
	lua_pushlstring(L, resp->result_data.data(), resp->result_data.size());
	lua_setfield(L, -2, "result_data");
	lua_pushinteger(L, static_cast<lua_Integer>(resp->affected_count));
	lua_setfield(L, -2, "affected_count");
	return 1;
}

const luaL_Reg kDbServiceFunctions[] = {
	{"db_is_running", l_db_is_running},
	{"db_is_healthy", l_db_is_healthy},
	{"db_get_thread_count", l_db_get_thread_count},
	{"db_next_request_id", l_db_next_request_id},
	{"db_metrics", l_db_metrics},
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
