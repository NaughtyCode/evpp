#include "runtime/script/net_http_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/httpc/request.h>
#include <runtime/evpp/httpc/response.h>

#include "runtime/config/config.h"
#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/vm/vm.h"

#include "runtime/vm/lua_error_handler.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

// HTTP Client bindings

// Call a Lua function with (int, string) for HTTP response.
// Logs and pops errors; does NOT unref.
void call_lua_http_handler(lua_State* L, int ref, int code, const std::string& body) {
	if (!L) return;
	if (ref == LUA_NOREF) return;
	const int base_top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);  // function
	if (!lua_isfunction(L, -1)) {
		lua_settop(L, base_top);
		return;
	}
	lua_pushinteger(L, static_cast<lua_Integer>(code));  // function, code
	lua_pushlstring(L, body.data(), body.size());  // function, code, body
	int msgh = PushLuaErrorHandlerForCall(L, 2);
	if (lua_pcall(L, 2, 0, msgh) != LUA_OK) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "[net.http] callback error: {}", lua_tostring(L, -1));
	}
	lua_settop(L, base_top);
}

// Guards HTTP callbacks from firing after engine shutdown.
// Set to false in ShutdownHttpBindings; HTTP callbacks check this before
// touching the Lua state (which may have been destroyed).
std::atomic<bool> g_net_alive{true};

// Track HTTP callback refs so they can be released during shutdown even
// when the request hasn't completed yet. Without this, refs held by
// in-flight HTTP requests would leak in the Lua registry.
struct HttpPendingRef {
	lua_State* L = nullptr;
	int ref = LUA_NOREF;
	uint64_t generation = 0;
};

std::vector<HttpPendingRef> g_http_pending_refs;
uint64_t g_http_generation = 1;
std::mutex g_http_mutex;

uint64_t TrackHttpRef(lua_State* L, int ref) {
	std::lock_guard<std::mutex> lock(g_http_mutex);
	g_http_pending_refs.push_back(HttpPendingRef{L, ref, g_http_generation});
	return g_http_generation;
}

// Common HTTP response handling: dispatches to Lua callback, then cleans up
// the registry ref under mutex. The TOCTOU-safe interaction with
// ShutdownHttpBindings works as follows:
//   - If g_net_alive is false on entry, ShutdownHttpBindings already owns
//     the pending refs → return immediately, let shutdown handle cleanup.
//   - After dispatching to Lua, re-check g_net_alive under the mutex
//     before unlinking the ref. If ShutdownHttpBindings concurrently set
//     g_net_alive=false and moved the pending vector, we bail out
//     without touching the ref (shutdown already released it).
void HandleHttpResponse(lua_State* L,
						int ref,
						uint64_t generation,
						const std::shared_ptr<evpp::httpc::Response>& resp) {
	// Unlink the ref from the pending list under the mutex. This must be
	// done before dispatching to Lua — if the Lua callback calls
	// net.http.get/post, those functions acquire g_http_mutex and would
	// deadlock on this thread if we still held it.
	{
		std::lock_guard<std::mutex> lock(g_http_mutex);
		if (!g_net_alive.load()) return;

		auto it = std::find_if(g_http_pending_refs.begin(),
							   g_http_pending_refs.end(),
							   [L, ref, generation](const HttpPendingRef& pending) {
								   return pending.L == L && pending.ref == ref &&
										  pending.generation == generation;
							   });
		if (it != g_http_pending_refs.end()) {
			g_http_pending_refs.erase(it);
		} else {
			// ShutdownHttpBindings already took ownership of this ref.
			return;
		}
	}

	// Re-check g_net_alive before dispatching to Lua. If ShutdownHttpBindings
	// ran between the mutex release and here, skip the callback (but still
	// unref — the Lua state is still valid since DestroyScript runs after
	// ShutdownNetBindings in Engine::Cleanup).
	if (!g_net_alive.load()) {
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
		return;
	}

	// Dispatch the Lua callback WITHOUT the mutex held, to avoid
	// re-entrant deadlock when the Lua handler calls net.http.get/post.
	if (resp) {
		std::string body(resp->body().data(), resp->body().size());
		call_lua_http_handler(L, ref, resp->http_code(), body);
	} else {
		call_lua_http_handler(L, ref, 0, "");
	}

	// Unref. Safe even if g_net_alive became false during the callback:
	// ShutdownHttpBindings (which sets g_net_alive) runs before
	// DestroyScript, so the Lua state is still valid. And since we
	// removed this ref from g_http_pending_refs above, ShutdownHttpBindings
	// won't double-unref it.
	luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

// ── l_net_http_get(url, on_response) ─────────────────────────────────────
int l_net_http_get(lua_State* L) {
	const char* url = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	auto* loop = Engine::Instance().GetEventLoop();
	if (!loop) {
		return luaL_error(L, "EventLoop not available");
	}

	lua_pushvalue(L, 2);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	uint64_t generation = TrackHttpRef(L, ref);

	double timeout = ConfigManager::Instance().GetServerConfig().http.timeout_sec;
	auto req = std::make_shared<evpp::httpc::GetRequest>(loop, url, evpp::Duration(timeout));

	req->Execute([L, ref, generation](const std::shared_ptr<evpp::httpc::Response>& resp) {
		HandleHttpResponse(L, ref, generation, resp);
	});

	return 0;
}

// ── l_net_http_post(url, body, on_response) ──────────────────────────────
int l_net_http_post(lua_State* L) {
	const char* url = luaL_checkstring(L, 1);
	size_t body_len = 0;
	const char* body = luaL_checklstring(L, 2, &body_len);
	luaL_checktype(L, 3, LUA_TFUNCTION);

	auto* loop = Engine::Instance().GetEventLoop();
	if (!loop) {
		return luaL_error(L, "EventLoop not available");
	}

	lua_pushvalue(L, 3);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	uint64_t generation = TrackHttpRef(L, ref);

	double timeout = ConfigManager::Instance().GetServerConfig().http.timeout_sec;
	auto req = std::make_shared<evpp::httpc::PostRequest>(
		loop, url, std::string(body, body_len), evpp::Duration(timeout));

	req->Execute([L, ref, generation](const std::shared_ptr<evpp::httpc::Response>& resp) {
		HandleHttpResponse(L, ref, generation, resp);
	});

	return 0;
}

const luaL_Reg kHttpFunctions[] = {
	{"get", l_net_http_get},
	{"post", l_net_http_post},
	{nullptr, nullptr},
};

}  // namespace

// Public API

void PushHttpLibrary(lua_State* L) {
	if (!L) return;
	{
		std::lock_guard<std::mutex> lock(g_http_mutex);
		++g_http_generation;
	}
	g_net_alive.store(true);
	luaL_newlib(L, kHttpFunctions);
}

void ShutdownHttpBindings() {
	auto* logger = GetLogger();

	// Prevent any in-flight HTTP callbacks from touching a freed Lua state.
	g_net_alive.store(false);

	// Atomically take ownership of pending refs. Callbacks that already
	// passed the g_net_alive check will re-check under the mutex inside
	// HandleHttpResponse and bail out, leaving cleanup to us.
	std::vector<HttpPendingRef> pending;
	{
		std::lock_guard<std::mutex> lock(g_http_mutex);
		pending.swap(g_http_pending_refs);
	}
	if (!pending.empty()) {
		for (const auto& item : pending) {
			if (item.L && item.ref != LUA_NOREF) {
				luaL_unref(item.L, LUA_REGISTRYINDEX, item.ref);
			}
		}
		ENGINE_LOG_INFO(
			logger, "ScriptBind: released [{}] pending HTTP callback(s)", pending.size());
	}
}

}  // namespace script
}  // namespace engine
