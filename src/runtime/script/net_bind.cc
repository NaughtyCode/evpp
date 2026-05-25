#include "runtime/script/net_bind.h"
#include "runtime/script/net_client_bind.h"
#include "runtime/script/net_server_bind.h"
#include "runtime/script/net_http_bind.h"
#include "runtime/script/net_udp_client_bind.h"
#include "runtime/script/net_udp_server_bind.h"
#include "runtime/script/net_kcp_client_bind.h"
#include "runtime/script/net_kcp_server_bind.h"

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
    RegisterUdpClientMetaTable(L);
    RegisterKcpClientMetaTable(L);
    RegisterServerMetaTable(L);
    RegisterConnMetaTable(L);
    RegisterUdpServerMetaTable(L);
    RegisterKcpServerMetaTable(L);

    // Build nested "net" table:
    //   net = { client = { ... }, server = { ... }, http = { ... },
    //           udp_client = { ... }, udp_server = { ... },
    //           kcp_client = { ... }, kcp_server = { ... } }
    lua_createtable(L, 0, 7);           // net table

    // net.client  (only static functions: connect)
    PushClientLibrary(L);                     // net, client
    lua_setfield(L, -2, "client");

    // net.server
    PushServerLibrary(L);                     // net, server
    lua_setfield(L, -2, "server");

    // net.http
    PushHttpLibrary(L);                       // net, http
    lua_setfield(L, -2, "http");

    // net.udp_client
    PushUdpClientLibrary(L);                  // net, udp_client
    lua_setfield(L, -2, "udp_client");

    // net.udp_server
    PushUdpServerLibrary(L);                  // net, udp_server
    lua_setfield(L, -2, "udp_server");

    // net.kcp_client
    PushKcpClientLibrary(L);                  // net, kcp_client
    lua_setfield(L, -2, "kcp_client");

    // net.kcp_server
    PushKcpServerLibrary(L);                  // net, kcp_server
    lua_setfield(L, -2, "kcp_server");

    lua_setglobal(L, "net");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: net module exported "
                    "(net.client/net.server/net.http/net.udp_client/net.udp_server/"
                    "net.kcp_client/net.kcp_server)");
}

void ShutdownNetBindings() {
    // Prevent in-flight HTTP callbacks from touching a freed Lua state
    // and release pending HTTP callback refs.
    ShutdownHttpBindings();

    // TCP client instances are cleaned up by Lua GC (__gc metamethod).
    // We do not maintain a global client map.

    ShutdownServerBindings();
    ShutdownUdpServerBindings();
    ShutdownKcpServerBindings();
}

} // namespace script
} // namespace engine
