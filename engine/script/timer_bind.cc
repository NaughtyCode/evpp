#include "engine/script/timer_bind.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"
#include "engine/core/timer/timer_core.h"
#include "engine/core/timer/timer_manager.h"
#include "engine/vm/vm.h"

namespace engine {
namespace script {

namespace {

//-----------------------------------------------------------------
// Per-timer Lua callback context
//-----------------------------------------------------------------

struct TimerCtx {
    TimerId  id = kInvalidTimerId;
    lua_State* L = nullptr;
    int      ref = LUA_NOREF;
    bool     repeating = false;
    Duration interval{0};
};

// Map of all Lua-created timers. Safe to access without a mutex because
// TimerManager callbacks fire on the same thread (the evpp event loop)
// and Lua is also single-threaded.
std::unordered_map<TimerId, std::shared_ptr<TimerCtx>> g_timer_ctxs;

//-----------------------------------------------------------------
// Helper: call a Lua function stored in the registry, then optionally
// release the reference.
//-----------------------------------------------------------------

// Max safe ms before ns conversion overflows int64.
constexpr int64_t kMaxTimerMs = INT64_MAX / kNsPerMs;

void call_lua_callback(lua_State* L, int ref) {
    if (!L) return;
    if (ref == LUA_NOREF) return;  // timer was already cancelled
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "[lua timer] callback error: {}",
                         lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

//-----------------------------------------------------------------
// Lua C functions for the "timer" module
//-----------------------------------------------------------------

// timer.timeout(ms, callback) → timer_id
int l_timer_timeout(lua_State* L) {
    int64_t ms = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    if (ms <= 0 || ms > kMaxTimerMs) {
        return luaL_error(L, "timer delay out of range [1, %lld]", (long long)kMaxTimerMs);
    }

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    auto ctx = std::make_shared<TimerCtx>();
    ctx->L = L;
    ctx->ref = ref;
    ctx->repeating = false;

    TimerId id = TimerManager::instance().create_timer(
        [ctx](HrTimerNode* /*timer*/) -> TimerResult {
            call_lua_callback(ctx->L, ctx->ref);
            if (ctx->ref != LUA_NOREF) {
                luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref);
                ctx->ref = LUA_NOREF;
            }
            g_timer_ctxs.erase(ctx->id);
            return TimerResult::kNoRestart;
        }
    );

    if (id == kInvalidTimerId) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "timer: failed to create timer");
    }

    ctx->id = id;
    g_timer_ctxs[id] = ctx;
    TimerManager::instance().start_timer_relative(id, ms_to_time(ms));

    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// timer.interval(ms, callback) → timer_id
int l_timer_interval(lua_State* L) {
    int64_t ms = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    if (ms <= 0 || ms > kMaxTimerMs) {
        return luaL_error(L, "timer delay out of range [1, %lld]", (long long)kMaxTimerMs);
    }

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    auto ctx = std::make_shared<TimerCtx>();
    ctx->L = L;
    ctx->ref = ref;
    ctx->repeating = true;
    ctx->interval = ms_to_time(ms);

    TimerId id = TimerManager::instance().create_timer(
        [ctx](HrTimerNode* timer) -> TimerResult {
            call_lua_callback(ctx->L, ctx->ref);
            if (ctx->ref == LUA_NOREF) {
                return TimerResult::kNoRestart;
            }
            timer->add_expires(ctx->interval);
            return TimerResult::kRestart;
        },
        ClockId::kMonotonic,
        TimerMode::kAbsolute | TimerMode::kRepeating
    );

    if (id == kInvalidTimerId) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "timer: failed to create timer");
    }

    ctx->id = id;
    g_timer_ctxs[id] = ctx;
    TimerManager::instance().start_timer_relative(id, ms_to_time(ms));

    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// timer.cancel(timer_id) → true / nil+errmsg
int l_timer_cancel(lua_State* L) {
    TimerId id = static_cast<TimerId>(luaL_checkinteger(L, 1));

    auto it = g_timer_ctxs.find(id);
    if (it == g_timer_ctxs.end()) {
        lua_pushnil(L);
        lua_pushstring(L, "timer not found");
        return 2;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, it->second->ref);
    it->second->ref = LUA_NOREF;
    // Set ref to LUA_NOREF before destroy_timer so the callback lambda
    // (which checks ref == LUA_NOREF) won't try to erase from g_timer_ctxs.
    TimerManager::instance().destroy_timer(id);
    // Use key-based erase to handle the case where destroy_timer fired the
    // callback synchronously and the callback already erased this entry.
    g_timer_ctxs.erase(id);

    auto* logger = GetLogger();
    ENGINE_LOG_DEBUG(logger, "[lua timer] cancelled timer [{}]", id);

    lua_pushboolean(L, 1);
    return 1;
}

const luaL_Reg kTimerFunctions[] = {
    {"timeout",  l_timer_timeout},
    {"interval", l_timer_interval},
    {"cancel",   l_timer_cancel},
    {nullptr, nullptr},
};

} // namespace

//=================================================================
// Public API
//=================================================================

void ExportTimer(ScriptVM& vm) {
    vm.RegisterModule("timer", kTimerFunctions);

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: timer module exported "
                    "(timer.timeout/interval/cancel)");
}

void ShutdownTimerBindings() {
    auto* logger = GetLogger();

    if (g_timer_ctxs.empty()) {
        ENGINE_LOG_DEBUG(logger, "ScriptBind: no active timer bindings to shut down");
        return;
    }

    // Collect IDs first — destroy_timer may fire callbacks synchronously,
    // which would invalidate iterators if we traversed the map directly.
    size_t count = g_timer_ctxs.size();
    std::vector<TimerId> ids;
    ids.reserve(count);
    for (auto& [id, ctx] : g_timer_ctxs) {
        (void)ctx;
        ids.push_back(id);
    }

    for (TimerId id : ids) {
        auto it = g_timer_ctxs.find(id);
        if (it == g_timer_ctxs.end()) continue;
        auto& ctx = it->second;
        // Set ref to LUA_NOREF first so the callback (which may fire synchronously
        // during destroy_timer) will bail out instead of accessing a dangling L.
        if (ctx->ref != LUA_NOREF && ctx->L) {
            luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref);
            ctx->ref = LUA_NOREF;
        }
        TimerManager::instance().destroy_timer(id);
        g_timer_ctxs.erase(id);
    }

    ENGINE_LOG_INFO(logger, "ScriptBind: shut down [{}] timer binding(s)", count);
}

} // namespace script
} // namespace engine
