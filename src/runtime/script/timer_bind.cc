#include "runtime/script/timer_bind.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "runtime/core/log/log.h"
#include "runtime/core/timer/timer_core.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

namespace {

//-----------------------------------------------------------------
// Per-timer Lua callback context
//-----------------------------------------------------------------

struct TimerBindState;  // forward decl for TimerCtx::owner

struct TimerCtx {
    TimerId  id = kInvalidTimerId;
    lua_State* L = nullptr;
    int      ref = LUA_NOREF;
    bool     repeating = false;
    Duration interval{0};
    TimerBindState* owner = nullptr;  // back-pointer to the per-VM state map
};

//-----------------------------------------------------------------
// Per-VM timer state
//-----------------------------------------------------------------

struct TimerBindState {
    std::unordered_map<TimerId, std::shared_ptr<TimerCtx>> ctxs;
};

// Each ScriptVM gets its own TimerBindState. The pointer is stored in the
// Lua registry under "__TimerBindState", mirroring the __ScriptImporter
// pattern.  The state is created in ExportTimer and destroyed in
// ShutdownTimerBindings.
TimerBindState* GetTimerState(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__TimerBindState");
    auto* state = static_cast<TimerBindState*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return state;
}

//-----------------------------------------------------------------
// Helper: call a Lua function stored in the registry.
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

    auto* state = GetTimerState(L);
    if (!state) {
        return luaL_error(L, "timer: system not initialized");
    }

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    auto ctx = std::make_shared<TimerCtx>();
    ctx->L = L;
    ctx->ref = ref;
    ctx->repeating = false;
    ctx->owner = state;

    TimerId id = TimerManager::instance().create_timer(
        [ctx](HrTimerNode* /*timer*/) -> TimerResult {
            // Keep ctx alive on the stack: call_lua_callback may trigger
            // timer:cancel() which destroys the HrTimerNode (and this
            // lambda's capture storage).  The local keep prevents the
            // shared_ptr<Ctx> refcount from hitting zero until we return.
            // Read keep->ref *after* the callback — the callback may call
            // timer:cancel() which sets ref to LUA_NOREF and calls
            // luaL_unref.  A stale snapshot would double-unref.
            auto keep = ctx;
            lua_State* L = keep->L;
            TimerBindState* owner = keep->owner;
            TimerId id = keep->id;

            call_lua_callback(L, keep->ref);

            if (keep->ref != LUA_NOREF && L) {
                luaL_unref(L, LUA_REGISTRYINDEX, keep->ref);
            }
            if (owner) {
                owner->ctxs.erase(id);
            }
            return TimerResult::kNoRestart;
        }
    );

    if (id == kInvalidTimerId) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "timer: failed to create timer");
    }

    ctx->id = id;
    state->ctxs[id] = ctx;
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

    auto* state = GetTimerState(L);
    if (!state) {
        return luaL_error(L, "timer: system not initialized");
    }

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    auto ctx = std::make_shared<TimerCtx>();
    ctx->L = L;
    ctx->ref = ref;
    ctx->repeating = true;
    ctx->interval = ms_to_time(ms);
    ctx->owner = state;

    TimerId id = TimerManager::instance().create_timer(
        [ctx](HrTimerNode* timer) -> TimerResult {
            // Stack-local keep prevents Ctx from being freed if the Lua
            // callback self-cancels (which destroys this lambda's capture
            // storage).  We read keep->ref after the call to detect
            // cancellation and avoid kRestart (which would UAF on timer).
            auto keep = ctx;
            lua_State* L = keep->L;
            Duration interval = keep->interval;

            call_lua_callback(L, keep->ref);

            if (keep->ref == LUA_NOREF) {
                return TimerResult::kNoRestart;
            }
            timer->add_expires(interval);
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
    state->ctxs[id] = ctx;
    TimerManager::instance().start_timer_relative(id, ms_to_time(ms));

    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// timer.cancel(timer_id) → true / nil+errmsg
int l_timer_cancel(lua_State* L) {
    TimerId id = static_cast<TimerId>(luaL_checkinteger(L, 1));

    auto* state = GetTimerState(L);
    if (!state) {
        lua_pushnil(L);
        lua_pushstring(L, "timer system not initialized");
        return 2;
    }

    auto it = state->ctxs.find(id);
    if (it == state->ctxs.end()) {
        lua_pushnil(L);
        lua_pushstring(L, "timer not found");
        return 2;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, it->second->ref);
    it->second->ref = LUA_NOREF;
    // Set ref to LUA_NOREF before destroy_timer so the callback lambda
    // (which checks ref == LUA_NOREF) won't try to erase from state->ctxs.
    TimerManager::instance().destroy_timer(id);
    // Use key-based erase to handle the case where destroy_timer fired the
    // callback synchronously and the callback already erased this entry.
    state->ctxs.erase(id);

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
    lua_State* L = vm.GetState();
    if (!L) return;

    // Create per-VM timer state and store in the Lua registry.
    auto* state = new TimerBindState();
    lua_pushlightuserdata(L, state);
    lua_setfield(L, LUA_REGISTRYINDEX, "__TimerBindState");

    vm.RegisterModule("timer", kTimerFunctions);

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: timer module exported "
                    "(timer.timeout/interval/cancel)");
}

void ShutdownTimerBindings(ScriptVM& vm) {
    lua_State* L = vm.GetState();
    if (!L) return;

    auto* state = GetTimerState(L);
    if (!state) return;

    auto* logger = GetLogger();

    if (state->ctxs.empty()) {
        ENGINE_LOG_DEBUG(logger, "ScriptBind: no active timer bindings to shut down");
        delete state;
        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "__TimerBindState");
        return;
    }

    // Collect IDs first — destroy_timer may fire callbacks synchronously,
    // which would invalidate iterators if we traversed the map directly.
    // Loop until no more timers remain: a Lua callback invoked during
    // shutdown could create new timers, which would otherwise leak.
    size_t count = 0;
    constexpr int kMaxShutdownPasses = 8;  // safety bound against infinite re-creation
    for (int pass = 0; pass < kMaxShutdownPasses && !state->ctxs.empty(); ++pass) {
        std::vector<TimerId> ids;
        ids.reserve(state->ctxs.size());
        for (auto& [id, ctx] : state->ctxs) {
            (void)ctx;
            ids.push_back(id);
        }
        count += ids.size();

        for (TimerId id : ids) {
            auto it = state->ctxs.find(id);
            if (it == state->ctxs.end()) continue;
            auto& ctx = it->second;
            if (ctx->ref != LUA_NOREF && ctx->L) {
                luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref);
                ctx->ref = LUA_NOREF;
            }
            ctx->owner = nullptr;
            TimerManager::instance().destroy_timer(id);
            state->ctxs.erase(id);
        }
    }

    ENGINE_LOG_INFO(logger, "ScriptBind: shut down [{}] timer binding(s)", count);

    // Release the per-VM state.
    delete state;
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "__TimerBindState");
}

} // namespace script
} // namespace engine
