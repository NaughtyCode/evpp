#pragma once
#if defined(ENGINE_MONGODB_ENABLED)
struct lua_State;
struct luaL_Reg;
namespace engine {
namespace script {
void RegisterMongoBulkWriteReplaceOneOptsMeta(lua_State* L);
const luaL_Reg* GetMongoBulkWriteReplaceOneOptsLib();
}
}
#endif
