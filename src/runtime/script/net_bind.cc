#include "runtime/script/net_bind.h"
#include "runtime/script/net_client_bind.h"
#include "runtime/script/net_server_bind.h"
#include "runtime/script/net_http_bind.h"

#include "runtime/core/log/log.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

// ======================================================================
// Public API
// ======================================================================

void ExportNet(ScriptVM& vm) {
    lua_State* L = vm.GetState();
    if (!L) return;

    RegisterClientMetaTable(L);

    // Build nested "net" table:
    //   net = { client = { ... }, server = { ... }, http = { ... } }
    lua_createtable(L, 0, 3);           // net table

    // net.client  (only static functions: connect)
    PushClientLibrary(L);                     // net, client
    lua_setfield(L, -2, "client");

    // net.server
    PushServerLibrary(L);                     // net, server
    lua_setfield(L, -2, "server");

    // net.http
    PushHttpLibrary(L);                       // net, http
    lua_setfield(L, -2, "http");

    lua_setglobal(L, "net");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: net module exported "
                    "(net.client/net.server/net.http)");
}

void ShutdownNetBindings() {
    // Prevent in-flight HTTP callbacks from touching a freed Lua state
    // and release pending HTTP callback refs.
    ShutdownHttpBindings();

    // TCP client instances are cleaned up by Lua GC (__gc metamethod).
    // We do not maintain a global client map.

    ShutdownServerBindings();
}

} // namespace script
} // namespace engine
