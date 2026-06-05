#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {

class ScriptVM;

namespace rpc {
class RpcServer;
class RpcClient;
}  // namespace rpc

namespace script {

CLOUD_ENGINE_API void ExportRpc(ScriptVM& vm);
CLOUD_ENGINE_API void UpdateRpcBindings(ScriptVM& vm);
CLOUD_ENGINE_API void ShutdownRpcBindings(ScriptVM& vm);

// Test utilities — extract underlying C++ objects from Lua instance tables.
// Returns nullptr if the table at idx is not a valid server/client instance.
CLOUD_ENGINE_API rpc::RpcServer* RpcBind_GetServer(lua_State* L, int idx);
CLOUD_ENGINE_API rpc::RpcClient* RpcBind_GetClient(lua_State* L, int idx);

}  // namespace script
}  // namespace engine
