#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_apm.h"

#include <cstring>
#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_apm.h"
#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_oid.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaCmdStarted = "mongoc.apm_cmd_started";
const char* kMetaCmdSucceeded = "mongoc.apm_cmd_succeeded";
const char* kMetaCmdFailed = "mongoc.apm_cmd_failed";
const char* kMetaServerChanged = "mongoc.apm_server_changed";
const char* kMetaServerOpening = "mongoc.apm_server_opening";
const char* kMetaServerClosed = "mongoc.apm_server_closed";
const char* kMetaTopologyChanged = "mongoc.apm_topology_changed";
const char* kMetaTopologyOpening = "mongoc.apm_topology_opening";
const char* kMetaTopologyClosed = "mongoc.apm_topology_closed";
const char* kMetaHbStarted = "mongoc.apm_hb_started";
const char* kMetaHbSucceeded = "mongoc.apm_hb_succeeded";
const char* kMetaHbFailed = "mongoc.apm_hb_failed";
const char* kMetaCallbacks = "mongoc.apm_callbacks";

void PushBsonDocument(lua_State* L, const void* raw_bson) {
	if (!raw_bson) {
		lua_pushnil(L);
		return;
	}
	const auto* b = static_cast<const bson_t*>(raw_bson);
	const uint8_t* data = bson_get_data(b);
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, data, b->len);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		return;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
}

// ═══════════════════════════════════════════════════════════════════════════// MongoApmCommandStartedEvent
int l_apm_cmd_started_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted) = nullptr;
	return 0;
}

int l_apm_cmd_started_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmCommandStartedEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmCommandStartedEvent>(L, kMetaCmdStarted);
	*ud = ev;
	return 1;
}

int l_apm_cmd_started_destroy(lua_State* L) {
	l_apm_cmd_started_gc(L);
	return 0;
}

int l_apm_cmd_started_get_command(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	PushBsonDocument(L, ev->GetCommand());
	return lua_isnil(L, -1) ? 1 : (lua_isstring(L, -1) ? 2 : 1);
}

int l_apm_cmd_started_get_database_name(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	const char* s = ev ? ev->GetDatabaseName() : nullptr;
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_apm_cmd_started_get_command_name(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	const char* s = ev ? ev->GetCommandName() : nullptr;
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_apm_cmd_started_get_request_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetRequestId()) : 0);
	return 1;
}

int l_apm_cmd_started_get_operation_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetOperationId()) : 0);
	return 1;
}

int l_apm_cmd_started_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_cmd_started_get_server_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	lua_pushinteger(L, ev ? ev->GetServerId() : 0);
	return 1;
}

int l_apm_cmd_started_get_service_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	const auto* oid = static_cast<const bson_oid_t*>(ev->GetServiceId());
	if (!oid) {
		lua_pushnil(L);
		return 1;
	}
	char str[25];
	bson_oid_to_string(oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_cmd_started_get_server_connection_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetServerConnectionIdInt64()) : 0);
	return 1;
}

int l_apm_cmd_started_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandStartedEvent>(L, 1, kMetaCmdStarted);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kCmdStartedLib[] = {
	{"apm_cmd_started_new", l_apm_cmd_started_new},
	{"apm_cmd_started_destroy", l_apm_cmd_started_destroy},
	{"apm_cmd_started_get_command", l_apm_cmd_started_get_command},
	{"apm_cmd_started_get_database_name", l_apm_cmd_started_get_database_name},
	{"apm_cmd_started_get_command_name", l_apm_cmd_started_get_command_name},
	{"apm_cmd_started_get_request_id", l_apm_cmd_started_get_request_id},
	{"apm_cmd_started_get_operation_id", l_apm_cmd_started_get_operation_id},
	{"apm_cmd_started_get_host", l_apm_cmd_started_get_host},
	{"apm_cmd_started_get_server_id", l_apm_cmd_started_get_server_id},
	{"apm_cmd_started_get_service_id", l_apm_cmd_started_get_service_id},
	{"apm_cmd_started_get_server_connection_id", l_apm_cmd_started_get_server_connection_id},
	{"apm_cmd_started_get_context", l_apm_cmd_started_get_context},
	{nullptr, nullptr},
};

int l_apm_cmd_succeeded_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded) = nullptr;
	return 0;
}

int l_apm_cmd_succeeded_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmCommandSucceededEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmCommandSucceededEvent>(L, kMetaCmdSucceeded);
	*ud = ev;
	return 1;
}

int l_apm_cmd_succeeded_destroy(lua_State* L) {
	l_apm_cmd_succeeded_gc(L);
	return 0;
}

int l_apm_cmd_succeeded_get_duration(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetDuration()) : 0);
	return 1;
}

int l_apm_cmd_succeeded_get_reply(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	PushBsonDocument(L, ev->GetReply());
	return lua_isnil(L, -1) ? 1 : (lua_isstring(L, -1) ? 2 : 1);
}

int l_apm_cmd_succeeded_get_command_name(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	const char* s = ev ? ev->GetCommandName() : nullptr;
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_apm_cmd_succeeded_get_database_name(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	const char* s = ev ? ev->GetDatabaseName() : nullptr;
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_apm_cmd_succeeded_get_request_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetRequestId()) : 0);
	return 1;
}

int l_apm_cmd_succeeded_get_operation_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetOperationId()) : 0);
	return 1;
}

int l_apm_cmd_succeeded_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_cmd_succeeded_get_server_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	lua_pushinteger(L, ev ? ev->GetServerId() : 0);
	return 1;
}

int l_apm_cmd_succeeded_get_service_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	const auto* oid = static_cast<const bson_oid_t*>(ev->GetServiceId());
	if (!oid) {
		lua_pushnil(L);
		return 1;
	}
	char str[25];
	bson_oid_to_string(oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_cmd_succeeded_get_server_connection_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetServerConnectionIdInt64()) : 0);
	return 1;
}

int l_apm_cmd_succeeded_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandSucceededEvent>(L, 1, kMetaCmdSucceeded);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kCmdSucceededLib[] = {
	{"apm_cmd_succeeded_new", l_apm_cmd_succeeded_new},
	{"apm_cmd_succeeded_destroy", l_apm_cmd_succeeded_destroy},
	{"apm_cmd_succeeded_get_duration", l_apm_cmd_succeeded_get_duration},
	{"apm_cmd_succeeded_get_reply", l_apm_cmd_succeeded_get_reply},
	{"apm_cmd_succeeded_get_command_name", l_apm_cmd_succeeded_get_command_name},
	{"apm_cmd_succeeded_get_database_name", l_apm_cmd_succeeded_get_database_name},
	{"apm_cmd_succeeded_get_request_id", l_apm_cmd_succeeded_get_request_id},
	{"apm_cmd_succeeded_get_operation_id", l_apm_cmd_succeeded_get_operation_id},
	{"apm_cmd_succeeded_get_host", l_apm_cmd_succeeded_get_host},
	{"apm_cmd_succeeded_get_server_id", l_apm_cmd_succeeded_get_server_id},
	{"apm_cmd_succeeded_get_service_id", l_apm_cmd_succeeded_get_service_id},
	{"apm_cmd_succeeded_get_server_connection_id", l_apm_cmd_succeeded_get_server_connection_id},
	{"apm_cmd_succeeded_get_context", l_apm_cmd_succeeded_get_context},
	{nullptr, nullptr},
};

int l_apm_cmd_failed_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed) = nullptr;
	return 0;
}

int l_apm_cmd_failed_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmCommandFailedEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmCommandFailedEvent>(L, kMetaCmdFailed);
	*ud = ev;
	return 1;
}

int l_apm_cmd_failed_destroy(lua_State* L) {
	l_apm_cmd_failed_gc(L);
	return 0;
}

int l_apm_cmd_failed_get_duration(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetDuration()) : 0);
	return 1;
}

int l_apm_cmd_failed_get_command_name(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	const char* s = ev ? ev->GetCommandName() : nullptr;
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_apm_cmd_failed_get_database_name(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	const char* s = ev ? ev->GetDatabaseName() : nullptr;
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_apm_cmd_failed_get_error(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	mongo::MongoError error;
	ev->GetError(&error);
	if (error.Code() != 0)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	return 1;
}

int l_apm_cmd_failed_get_reply(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	PushBsonDocument(L, ev->GetReply());
	return lua_isnil(L, -1) ? 1 : (lua_isstring(L, -1) ? 2 : 1);
}

int l_apm_cmd_failed_get_request_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetRequestId()) : 0);
	return 1;
}

int l_apm_cmd_failed_get_operation_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetOperationId()) : 0);
	return 1;
}

int l_apm_cmd_failed_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_cmd_failed_get_server_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	lua_pushinteger(L, ev ? ev->GetServerId() : 0);
	return 1;
}

int l_apm_cmd_failed_get_service_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	const auto* oid = static_cast<const bson_oid_t*>(ev->GetServiceId());
	if (!oid) {
		lua_pushnil(L);
		return 1;
	}
	char str[25];
	bson_oid_to_string(oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_cmd_failed_get_server_connection_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetServerConnectionIdInt64()) : 0);
	return 1;
}

int l_apm_cmd_failed_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmCommandFailedEvent>(L, 1, kMetaCmdFailed);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kCmdFailedLib[] = {
	{"apm_cmd_failed_new", l_apm_cmd_failed_new},
	{"apm_cmd_failed_destroy", l_apm_cmd_failed_destroy},
	{"apm_cmd_failed_get_duration", l_apm_cmd_failed_get_duration},
	{"apm_cmd_failed_get_command_name", l_apm_cmd_failed_get_command_name},
	{"apm_cmd_failed_get_database_name", l_apm_cmd_failed_get_database_name},
	{"apm_cmd_failed_get_error", l_apm_cmd_failed_get_error},
	{"apm_cmd_failed_get_reply", l_apm_cmd_failed_get_reply},
	{"apm_cmd_failed_get_request_id", l_apm_cmd_failed_get_request_id},
	{"apm_cmd_failed_get_operation_id", l_apm_cmd_failed_get_operation_id},
	{"apm_cmd_failed_get_host", l_apm_cmd_failed_get_host},
	{"apm_cmd_failed_get_server_id", l_apm_cmd_failed_get_server_id},
	{"apm_cmd_failed_get_service_id", l_apm_cmd_failed_get_service_id},
	{"apm_cmd_failed_get_server_connection_id", l_apm_cmd_failed_get_server_connection_id},
	{"apm_cmd_failed_get_context", l_apm_cmd_failed_get_context},
	{nullptr, nullptr},
};

int l_apm_server_changed_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerChangedEvent>(L, 1, kMetaServerChanged);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmServerChangedEvent>(L, 1, kMetaServerChanged) = nullptr;
	return 0;
}

int l_apm_server_changed_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmServerChangedEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmServerChangedEvent>(L, kMetaServerChanged);
	*ud = ev;
	return 1;
}

int l_apm_server_changed_destroy(lua_State* L) {
	l_apm_server_changed_gc(L);
	return 0;
}

int l_apm_server_changed_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerChangedEvent>(L, 1, kMetaServerChanged);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_server_changed_get_topology_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerChangedEvent>(L, 1, kMetaServerChanged);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	bson_oid_t oid;
	ev->GetTopologyId(&oid);
	char str[25];
	bson_oid_to_string(&oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_server_changed_get_previous_desc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerChangedEvent>(L, 1, kMetaServerChanged);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetPreviousDescription() : nullptr));
	return 1;
}

int l_apm_server_changed_get_new_desc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerChangedEvent>(L, 1, kMetaServerChanged);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetNewDescription() : nullptr));
	return 1;
}

int l_apm_server_changed_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerChangedEvent>(L, 1, kMetaServerChanged);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kServerChangedLib[] = {
	{"apm_server_changed_new", l_apm_server_changed_new},
	{"apm_server_changed_destroy", l_apm_server_changed_destroy},
	{"apm_server_changed_get_host", l_apm_server_changed_get_host},
	{"apm_server_changed_get_topology_id", l_apm_server_changed_get_topology_id},
	{"apm_server_changed_get_previous_desc", l_apm_server_changed_get_previous_desc},
	{"apm_server_changed_get_new_desc", l_apm_server_changed_get_new_desc},
	{"apm_server_changed_get_context", l_apm_server_changed_get_context},
	{nullptr, nullptr},
};

int l_apm_server_opening_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerOpeningEvent>(L, 1, kMetaServerOpening);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmServerOpeningEvent>(L, 1, kMetaServerOpening) = nullptr;
	return 0;
}

int l_apm_server_opening_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmServerOpeningEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmServerOpeningEvent>(L, kMetaServerOpening);
	*ud = ev;
	return 1;
}

int l_apm_server_opening_destroy(lua_State* L) {
	l_apm_server_opening_gc(L);
	return 0;
}

int l_apm_server_opening_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerOpeningEvent>(L, 1, kMetaServerOpening);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_server_opening_get_topology_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerOpeningEvent>(L, 1, kMetaServerOpening);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	bson_oid_t oid;
	ev->GetTopologyId(&oid);
	char str[25];
	bson_oid_to_string(&oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_server_opening_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerOpeningEvent>(L, 1, kMetaServerOpening);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kServerOpeningLib[] = {
	{"apm_server_opening_new", l_apm_server_opening_new},
	{"apm_server_opening_destroy", l_apm_server_opening_destroy},
	{"apm_server_opening_get_host", l_apm_server_opening_get_host},
	{"apm_server_opening_get_topology_id", l_apm_server_opening_get_topology_id},
	{"apm_server_opening_get_context", l_apm_server_opening_get_context},
	{nullptr, nullptr},
};

int l_apm_server_closed_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerClosedEvent>(L, 1, kMetaServerClosed);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmServerClosedEvent>(L, 1, kMetaServerClosed) = nullptr;
	return 0;
}

int l_apm_server_closed_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmServerClosedEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmServerClosedEvent>(L, kMetaServerClosed);
	*ud = ev;
	return 1;
}

int l_apm_server_closed_destroy(lua_State* L) {
	l_apm_server_closed_gc(L);
	return 0;
}

int l_apm_server_closed_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerClosedEvent>(L, 1, kMetaServerClosed);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_server_closed_get_topology_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerClosedEvent>(L, 1, kMetaServerClosed);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	bson_oid_t oid;
	ev->GetTopologyId(&oid);
	char str[25];
	bson_oid_to_string(&oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_server_closed_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerClosedEvent>(L, 1, kMetaServerClosed);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kServerClosedLib[] = {
	{"apm_server_closed_new", l_apm_server_closed_new},
	{"apm_server_closed_destroy", l_apm_server_closed_destroy},
	{"apm_server_closed_get_host", l_apm_server_closed_get_host},
	{"apm_server_closed_get_topology_id", l_apm_server_closed_get_topology_id},
	{"apm_server_closed_get_context", l_apm_server_closed_get_context},
	{nullptr, nullptr},
};

int l_apm_topology_changed_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyChangedEvent>(L, 1, kMetaTopologyChanged);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmTopologyChangedEvent>(L, 1, kMetaTopologyChanged) = nullptr;
	return 0;
}

int l_apm_topology_changed_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmTopologyChangedEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmTopologyChangedEvent>(L, kMetaTopologyChanged);
	*ud = ev;
	return 1;
}

int l_apm_topology_changed_destroy(lua_State* L) {
	l_apm_topology_changed_gc(L);
	return 0;
}

int l_apm_topology_changed_get_topology_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyChangedEvent>(L, 1, kMetaTopologyChanged);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	bson_oid_t oid;
	ev->GetTopologyId(&oid);
	char str[25];
	bson_oid_to_string(&oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_topology_changed_get_previous_desc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyChangedEvent>(L, 1, kMetaTopologyChanged);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetPreviousDescription() : nullptr));
	return 1;
}

int l_apm_topology_changed_get_new_desc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyChangedEvent>(L, 1, kMetaTopologyChanged);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetNewDescription() : nullptr));
	return 1;
}

int l_apm_topology_changed_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyChangedEvent>(L, 1, kMetaTopologyChanged);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kTopologyChangedLib[] = {
	{"apm_topology_changed_new", l_apm_topology_changed_new},
	{"apm_topology_changed_destroy", l_apm_topology_changed_destroy},
	{"apm_topology_changed_get_topology_id", l_apm_topology_changed_get_topology_id},
	{"apm_topology_changed_get_previous_desc", l_apm_topology_changed_get_previous_desc},
	{"apm_topology_changed_get_new_desc", l_apm_topology_changed_get_new_desc},
	{"apm_topology_changed_get_context", l_apm_topology_changed_get_context},
	{nullptr, nullptr},
};

int l_apm_topology_opening_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyOpeningEvent>(L, 1, kMetaTopologyOpening);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmTopologyOpeningEvent>(L, 1, kMetaTopologyOpening) = nullptr;
	return 0;
}

int l_apm_topology_opening_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmTopologyOpeningEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmTopologyOpeningEvent>(L, kMetaTopologyOpening);
	*ud = ev;
	return 1;
}

int l_apm_topology_opening_destroy(lua_State* L) {
	l_apm_topology_opening_gc(L);
	return 0;
}

int l_apm_topology_opening_get_topology_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyOpeningEvent>(L, 1, kMetaTopologyOpening);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	bson_oid_t oid;
	ev->GetTopologyId(&oid);
	char str[25];
	bson_oid_to_string(&oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_topology_opening_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyOpeningEvent>(L, 1, kMetaTopologyOpening);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kTopologyOpeningLib[] = {
	{"apm_topology_opening_new", l_apm_topology_opening_new},
	{"apm_topology_opening_destroy", l_apm_topology_opening_destroy},
	{"apm_topology_opening_get_topology_id", l_apm_topology_opening_get_topology_id},
	{"apm_topology_opening_get_context", l_apm_topology_opening_get_context},
	{nullptr, nullptr},
};

int l_apm_topology_closed_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyClosedEvent>(L, 1, kMetaTopologyClosed);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmTopologyClosedEvent>(L, 1, kMetaTopologyClosed) = nullptr;
	return 0;
}

int l_apm_topology_closed_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmTopologyClosedEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmTopologyClosedEvent>(L, kMetaTopologyClosed);
	*ud = ev;
	return 1;
}

int l_apm_topology_closed_destroy(lua_State* L) {
	l_apm_topology_closed_gc(L);
	return 0;
}

int l_apm_topology_closed_get_topology_id(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyClosedEvent>(L, 1, kMetaTopologyClosed);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	bson_oid_t oid;
	ev->GetTopologyId(&oid);
	char str[25];
	bson_oid_to_string(&oid, str);
	lua_pushstring(L, str);
	return 1;
}

int l_apm_topology_closed_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmTopologyClosedEvent>(L, 1, kMetaTopologyClosed);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

const luaL_Reg kTopologyClosedLib[] = {
	{"apm_topology_closed_new", l_apm_topology_closed_new},
	{"apm_topology_closed_destroy", l_apm_topology_closed_destroy},
	{"apm_topology_closed_get_topology_id", l_apm_topology_closed_get_topology_id},
	{"apm_topology_closed_get_context", l_apm_topology_closed_get_context},
	{nullptr, nullptr},
};

int l_apm_hb_started_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatStartedEvent>(L, 1, kMetaHbStarted);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmServerHeartbeatStartedEvent>(L, 1, kMetaHbStarted) = nullptr;
	return 0;
}

int l_apm_hb_started_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmServerHeartbeatStartedEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmServerHeartbeatStartedEvent>(L, kMetaHbStarted);
	*ud = ev;
	return 1;
}

int l_apm_hb_started_destroy(lua_State* L) {
	l_apm_hb_started_gc(L);
	return 0;
}

int l_apm_hb_started_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatStartedEvent>(L, 1, kMetaHbStarted);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_hb_started_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatStartedEvent>(L, 1, kMetaHbStarted);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

int l_apm_hb_started_get_awaited(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatStartedEvent>(L, 1, kMetaHbStarted);
	lua_pushboolean(L, ev ? ev->GetAwaited() : false);
	return 1;
}

const luaL_Reg kHbStartedLib[] = {
	{"apm_hb_started_new", l_apm_hb_started_new},
	{"apm_hb_started_destroy", l_apm_hb_started_destroy},
	{"apm_hb_started_get_host", l_apm_hb_started_get_host},
	{"apm_hb_started_get_context", l_apm_hb_started_get_context},
	{"apm_hb_started_get_awaited", l_apm_hb_started_get_awaited},
	{nullptr, nullptr},
};

int l_apm_hb_succeeded_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatSucceededEvent>(L, 1, kMetaHbSucceeded);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmServerHeartbeatSucceededEvent>(L, 1, kMetaHbSucceeded) = nullptr;
	return 0;
}

int l_apm_hb_succeeded_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmServerHeartbeatSucceededEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmServerHeartbeatSucceededEvent>(L, kMetaHbSucceeded);
	*ud = ev;
	return 1;
}

int l_apm_hb_succeeded_destroy(lua_State* L) {
	l_apm_hb_succeeded_gc(L);
	return 0;
}

int l_apm_hb_succeeded_get_duration(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatSucceededEvent>(L, 1, kMetaHbSucceeded);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetDuration()) : 0);
	return 1;
}

int l_apm_hb_succeeded_get_reply(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatSucceededEvent>(L, 1, kMetaHbSucceeded);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	PushBsonDocument(L, ev->GetReply());
	return lua_isnil(L, -1) ? 1 : (lua_isstring(L, -1) ? 2 : 1);
}

int l_apm_hb_succeeded_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatSucceededEvent>(L, 1, kMetaHbSucceeded);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_hb_succeeded_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatSucceededEvent>(L, 1, kMetaHbSucceeded);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

int l_apm_hb_succeeded_get_awaited(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatSucceededEvent>(L, 1, kMetaHbSucceeded);
	lua_pushboolean(L, ev ? ev->GetAwaited() : false);
	return 1;
}

const luaL_Reg kHbSucceededLib[] = {
	{"apm_hb_succeeded_new", l_apm_hb_succeeded_new},
	{"apm_hb_succeeded_destroy", l_apm_hb_succeeded_destroy},
	{"apm_hb_succeeded_get_duration", l_apm_hb_succeeded_get_duration},
	{"apm_hb_succeeded_get_reply", l_apm_hb_succeeded_get_reply},
	{"apm_hb_succeeded_get_host", l_apm_hb_succeeded_get_host},
	{"apm_hb_succeeded_get_context", l_apm_hb_succeeded_get_context},
	{"apm_hb_succeeded_get_awaited", l_apm_hb_succeeded_get_awaited},
	{nullptr, nullptr},
};

int l_apm_hb_failed_gc(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatFailedEvent>(L, 1, kMetaHbFailed);
	CLOUDENGINE_MEM_DELETE(ev);
	*CheckUserdata<mongo::MongoApmServerHeartbeatFailedEvent>(L, 1, kMetaHbFailed) = nullptr;
	return 0;
}

int l_apm_hb_failed_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "raw event pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* ev = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmServerHeartbeatFailedEvent, raw);
	if (!ev) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmServerHeartbeatFailedEvent>(L, kMetaHbFailed);
	*ud = ev;
	return 1;
}

int l_apm_hb_failed_destroy(lua_State* L) {
	l_apm_hb_failed_gc(L);
	return 0;
}

int l_apm_hb_failed_get_duration(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatFailedEvent>(L, 1, kMetaHbFailed);
	lua_pushinteger(L, ev ? static_cast<lua_Integer>(ev->GetDuration()) : 0);
	return 1;
}

int l_apm_hb_failed_get_error(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatFailedEvent>(L, 1, kMetaHbFailed);
	if (!ev) {
		lua_pushnil(L);
		return 1;
	}
	mongo::MongoError error;
	ev->GetError(&error);
	if (error.Code() != 0)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	return 1;
}

int l_apm_hb_failed_get_host(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatFailedEvent>(L, 1, kMetaHbFailed);
	lua_pushlightuserdata(L, const_cast<void*>(ev ? ev->GetHost() : nullptr));
	return 1;
}

int l_apm_hb_failed_get_context(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatFailedEvent>(L, 1, kMetaHbFailed);
	lua_pushlightuserdata(L, ev ? ev->GetContext() : nullptr);
	return 1;
}

int l_apm_hb_failed_get_awaited(lua_State* L) {
	auto* ev = GetUserdata<mongo::MongoApmServerHeartbeatFailedEvent>(L, 1, kMetaHbFailed);
	lua_pushboolean(L, ev ? ev->GetAwaited() : false);
	return 1;
}

const luaL_Reg kHbFailedLib[] = {
	{"apm_hb_failed_new", l_apm_hb_failed_new},
	{"apm_hb_failed_destroy", l_apm_hb_failed_destroy},
	{"apm_hb_failed_get_duration", l_apm_hb_failed_get_duration},
	{"apm_hb_failed_get_error", l_apm_hb_failed_get_error},
	{"apm_hb_failed_get_host", l_apm_hb_failed_get_host},
	{"apm_hb_failed_get_context", l_apm_hb_failed_get_context},
	{"apm_hb_failed_get_awaited", l_apm_hb_failed_get_awaited},
	{nullptr, nullptr},
};

int l_apm_callbacks_gc(lua_State* L) {
	auto* cb = GetUserdata<mongo::MongoApmCallbacks>(L, 1, kMetaCallbacks);
	CLOUDENGINE_MEM_DELETE(cb);
	*CheckUserdata<mongo::MongoApmCallbacks>(L, 1, kMetaCallbacks) = nullptr;
	return 0;
}

int l_apm_callbacks_new(lua_State* L) {
	auto* cb = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoApmCallbacks);
	if (!cb) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoApmCallbacks>(L, kMetaCallbacks);
	*ud = cb;
	return 1;
}

int l_apm_callbacks_destroy(lua_State* L) {
	l_apm_callbacks_gc(L);
	return 0;
}

int l_apm_callbacks_get_raw(lua_State* L) {
	auto* cb = GetUserdata<mongo::MongoApmCallbacks>(L, 1, kMetaCallbacks);
	lua_pushlightuserdata(L, cb ? cb->Raw() : nullptr);
	return 1;
}

int l_apm_callbacks_get_raw_context(lua_State* L) {
	auto* cb = GetUserdata<mongo::MongoApmCallbacks>(L, 1, kMetaCallbacks);
	lua_pushlightuserdata(L, cb ? cb->RawContext() : nullptr);
	return 1;
}

const luaL_Reg kCallbacksLib[] = {
	{"apm_callbacks_new", l_apm_callbacks_new},
	{"apm_callbacks_destroy", l_apm_callbacks_destroy},
	{"apm_callbacks_get_raw", l_apm_callbacks_get_raw},
	{"apm_callbacks_get_raw_context", l_apm_callbacks_get_raw_context},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoApmCommandStartedEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaCmdStarted, nullptr, l_apm_cmd_started_gc);
}

void RegisterMongoApmCommandSucceededEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaCmdSucceeded, nullptr, l_apm_cmd_succeeded_gc);
}

void RegisterMongoApmCommandFailedEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaCmdFailed, nullptr, l_apm_cmd_failed_gc);
}

void RegisterMongoApmServerChangedEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaServerChanged, nullptr, l_apm_server_changed_gc);
}

void RegisterMongoApmServerOpeningEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaServerOpening, nullptr, l_apm_server_opening_gc);
}

void RegisterMongoApmServerClosedEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaServerClosed, nullptr, l_apm_server_closed_gc);
}

void RegisterMongoApmTopologyChangedEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaTopologyChanged, nullptr, l_apm_topology_changed_gc);
}

void RegisterMongoApmTopologyOpeningEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaTopologyOpening, nullptr, l_apm_topology_opening_gc);
}

void RegisterMongoApmTopologyClosedEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaTopologyClosed, nullptr, l_apm_topology_closed_gc);
}

void RegisterMongoApmServerHeartbeatStartedEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaHbStarted, nullptr, l_apm_hb_started_gc);
}

void RegisterMongoApmServerHeartbeatSucceededEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaHbSucceeded, nullptr, l_apm_hb_succeeded_gc);
}

void RegisterMongoApmServerHeartbeatFailedEventMeta(lua_State* L) {
	RegisterMetatable(L, kMetaHbFailed, nullptr, l_apm_hb_failed_gc);
}

void RegisterMongoApmCallbacksMeta(lua_State* L) {
	RegisterMetatable(L, kMetaCallbacks, nullptr, l_apm_callbacks_gc);
}

const luaL_Reg* GetMongoApmCommandStartedEventLib() {
	return kCmdStartedLib;
}
const luaL_Reg* GetMongoApmCommandSucceededEventLib() {
	return kCmdSucceededLib;
}
const luaL_Reg* GetMongoApmCommandFailedEventLib() {
	return kCmdFailedLib;
}
const luaL_Reg* GetMongoApmServerChangedEventLib() {
	return kServerChangedLib;
}
const luaL_Reg* GetMongoApmServerOpeningEventLib() {
	return kServerOpeningLib;
}
const luaL_Reg* GetMongoApmServerClosedEventLib() {
	return kServerClosedLib;
}
const luaL_Reg* GetMongoApmTopologyChangedEventLib() {
	return kTopologyChangedLib;
}
const luaL_Reg* GetMongoApmTopologyOpeningEventLib() {
	return kTopologyOpeningLib;
}
const luaL_Reg* GetMongoApmTopologyClosedEventLib() {
	return kTopologyClosedLib;
}
const luaL_Reg* GetMongoApmServerHeartbeatStartedEventLib() {
	return kHbStartedLib;
}
const luaL_Reg* GetMongoApmServerHeartbeatSucceededEventLib() {
	return kHbSucceededLib;
}
const luaL_Reg* GetMongoApmServerHeartbeatFailedEventLib() {
	return kHbFailedLib;
}
const luaL_Reg* GetMongoApmCallbacksLib() {
	return kCallbacksLib;
}

}  // namespace script
}  // namespace engine

#endif
