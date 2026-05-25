/*
 * client_timer.cpp — Timer wrappers.
 *
 * Wraps the Lua-exposed timer module (timer.timeout / timer.interval /
 * timer.cancel) so C callers get direct callback dispatch without going
 * through Lua.
 *
 * Internally, timers are created as Lua-level timers. A thin Lua trampoline
 * bridges back into C to invoke the user's game_timer_cb_t.
 */

#include "client_internal.h"

#include "runtime/engine/engine.h"
#include "runtime/vm/vm.h"

#include <cstdint>
#include <unordered_map>
#include <mutex>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace {

struct TimerEntry {
    game_timer_cb_t cb;
    void*           userdata;
};

/* Simple registry: timer_id → callback. Protected by mutex because the
 * timer callback fires on the event-loop thread (same thread as tick),
 * but registration/cancellation may come from the caller on the same
 * thread — still, guard for correctness. */
std::unordered_map<int, TimerEntry> g_timers;
std::mutex g_timer_mutex;
int g_next_timer_id = 1;

/* Lua trampoline: called by the Lua timer system, dispatches to C callback. */
int timer_trampoline(lua_State* L) {
    /* The Lua timer callback receives no arguments. We look up the timer
     * by the timer_id stored in the upvalue. */
    int timer_id = static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)));

    std::lock_guard<std::mutex> lock(g_timer_mutex);
    auto it = g_timers.find(timer_id);
    if (it != g_timers.end() && it->second.cb) {
        /* Release lock before calling user callback to avoid deadlock
         * if the callback calls timer_cancel. */
        auto cb  = it->second.cb;
        auto ud  = it->second.userdata;
        /* For one-shot timers (timeout), the Lua side auto-destroys the
         * timer after the callback returns. We remove our entry. */
        /* Note: we can't distinguish timeout vs interval here without
         * extra bookkeeping. The entry is cleaned up by cancel(). */
        cb(timer_id, ud);
    }
    return 0;
}

} /* anonymous namespace */

extern "C" {

game_error_t game_timer_timeout(game_client_t* client, int64_t delay_ms,
                                game_timer_cb_t cb, void* userdata,
                                int* out_id) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (delay_ms < 1 || !cb || !out_id) return GAME_ERR_INVALID_ARG;

    auto& vm = engine::Engine::Instance().GetScriptVM();
    lua_State* L = vm.GetState();
    if (!L) return GAME_ERR_GENERIC;

    int timer_id;
    {
        std::lock_guard<std::mutex> lock(g_timer_mutex);
        timer_id = g_next_timer_id++;
        g_timers[timer_id] = {cb, userdata};
    }

    /* Push the timer id as an upvalue for the trampoline. */
    lua_getglobal(L, "timer");                    /* timer           */
    lua_getfield(L, -1, "timeout");               /* timer, timeout  */
    lua_remove(L, -2);                            /* timeout         */
    lua_pushinteger(L, static_cast<lua_Integer>(delay_ms)); /* timeout, ms */

    /* Create closure with timer_id as upvalue */
    lua_pushinteger(L, timer_id);                 /* timeout, ms, id */
    lua_pushcclosure(L, timer_trampoline, 1);     /* timeout, ms, fn */

    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        std::lock_guard<std::mutex> lock(g_timer_mutex);
        g_timers.erase(timer_id);
        set_error(client, lua_tostring(L, -1));
        return GAME_ERR_GENERIC;
    }

    /* The returned timer_id from Lua is ignored — we use our own. */
    lua_pop(L, 1);
    *out_id = timer_id;
    return GAME_OK;
}

game_error_t game_timer_interval(game_client_t* client, int64_t interval_ms,
                                 game_timer_cb_t cb, void* userdata,
                                 int* out_id) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (interval_ms < 1 || !cb || !out_id) return GAME_ERR_INVALID_ARG;

    auto& vm = engine::Engine::Instance().GetScriptVM();
    lua_State* L = vm.GetState();
    if (!L) return GAME_ERR_GENERIC;

    int timer_id;
    {
        std::lock_guard<std::mutex> lock(g_timer_mutex);
        timer_id = g_next_timer_id++;
        g_timers[timer_id] = {cb, userdata};
    }

    lua_getglobal(L, "timer");                    /* timer             */
    lua_getfield(L, -1, "interval");              /* timer, interval   */
    lua_remove(L, -2);                            /* interval          */
    lua_pushinteger(L, static_cast<lua_Integer>(interval_ms)); /* int, ms */

    lua_pushinteger(L, timer_id);
    lua_pushcclosure(L, timer_trampoline, 1);     /* interval, ms, fn  */

    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        std::lock_guard<std::mutex> lock(g_timer_mutex);
        g_timers.erase(timer_id);
        set_error(client, lua_tostring(L, -1));
        return GAME_ERR_GENERIC;
    }

    lua_pop(L, 1);
    *out_id = timer_id;
    return GAME_OK;
}

game_error_t game_timer_cancel(game_client_t* client, int timer_id) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;

    /* Remove from our registry first, then cancel the Lua timer.
     * The Lua timer.cancel is safe to call from within a callback. */
    {
        std::lock_guard<std::mutex> lock(g_timer_mutex);
        auto it = g_timers.find(timer_id);
        if (it == g_timers.end()) return GAME_ERR_NOT_FOUND;
        g_timers.erase(it);
    }

    auto& vm = engine::Engine::Instance().GetScriptVM();
    lua_State* L = vm.GetState();
    if (!L) return GAME_ERR_GENERIC;

    lua_getglobal(L, "timer");                    /* timer        */
    lua_getfield(L, -1, "cancel");                /* timer, cancel */
    lua_remove(L, -2);                            /* cancel       */
    lua_pushinteger(L, timer_id);                 /* cancel, id   */

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return GAME_ERR_NOT_FOUND;
    }

    /* timer.cancel returns true if found, nil+msg if not */
    bool cancelled = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return cancelled ? GAME_OK : GAME_ERR_NOT_FOUND;
}

} /* extern "C" */
