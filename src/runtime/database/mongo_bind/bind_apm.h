#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoApmCommandStartedEventMeta(lua_State* L);
void RegisterMongoApmCommandSucceededEventMeta(lua_State* L);
void RegisterMongoApmCommandFailedEventMeta(lua_State* L);
void RegisterMongoApmServerChangedEventMeta(lua_State* L);
void RegisterMongoApmServerOpeningEventMeta(lua_State* L);
void RegisterMongoApmServerClosedEventMeta(lua_State* L);
void RegisterMongoApmTopologyChangedEventMeta(lua_State* L);
void RegisterMongoApmTopologyOpeningEventMeta(lua_State* L);
void RegisterMongoApmTopologyClosedEventMeta(lua_State* L);
void RegisterMongoApmServerHeartbeatStartedEventMeta(lua_State* L);
void RegisterMongoApmServerHeartbeatSucceededEventMeta(lua_State* L);
void RegisterMongoApmServerHeartbeatFailedEventMeta(lua_State* L);
void RegisterMongoApmCallbacksMeta(lua_State* L);

const luaL_Reg* GetMongoApmCommandStartedEventLib();
const luaL_Reg* GetMongoApmCommandSucceededEventLib();
const luaL_Reg* GetMongoApmCommandFailedEventLib();
const luaL_Reg* GetMongoApmServerChangedEventLib();
const luaL_Reg* GetMongoApmServerOpeningEventLib();
const luaL_Reg* GetMongoApmServerClosedEventLib();
const luaL_Reg* GetMongoApmTopologyChangedEventLib();
const luaL_Reg* GetMongoApmTopologyOpeningEventLib();
const luaL_Reg* GetMongoApmTopologyClosedEventLib();
const luaL_Reg* GetMongoApmServerHeartbeatStartedEventLib();
const luaL_Reg* GetMongoApmServerHeartbeatSucceededEventLib();
const luaL_Reg* GetMongoApmServerHeartbeatFailedEventLib();
const luaL_Reg* GetMongoApmCallbacksLib();

}  // namespace script
}  // namespace engine

#endif
