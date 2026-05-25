#pragma once

#include "runtime/core/engine_api.h"

namespace engine {

class ScriptVM;

namespace script {

// Assemble and export the "net" module to Lua by wiring together
// the client, server, http, udp_client, and udp_server sub-module libraries.
//
// See: net_client_bind.h, net_server_bind.h, net_http_bind.h,
//      net_udp_client_bind.h, net_udp_server_bind.h, and net.lua
// for full API docs.
ENGINE_API void ExportNet(ScriptVM& vm);

// Cancel all network objects and release Lua function references.
ENGINE_API void ShutdownNetBindings();

} // namespace script
} // namespace engine
