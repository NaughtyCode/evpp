/*
 * client.cpp — Engine lifecycle, script execution, and utility functions.
 *
 * Links against ServerEngine.dll and wraps its C++ classes:
 *   engine::Engine   — singleton lifecycle (Init/Tick/Cleanup)
 *   engine::ScriptVM — Lua VM (DoString/DoFile/RegisterFunction)
 */

#include "client_internal.h"

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/vm/vm.h"

#include <cstdlib>
#include <cstring>

/* =========================================================================
 * Version
 * ========================================================================= */

extern "C" const char* game_version(void) {
    return CLIENT_VERSION_STRING;
}

/* =========================================================================
 * Engine lifecycle
 * ========================================================================= */

extern "C" game_error_t game_client_create(game_client_t** out_client) {
    if (!out_client) return GAME_ERR_INVALID_ARG;

    auto* e = new (std::nothrow) game_client_t();
    if (!e) return GAME_ERR_OUT_OF_MEMORY;

    *out_client = e;
    return GAME_OK;
}

extern "C" game_error_t game_client_init(game_client_t* client,
                                         const char* config_dir) {
    if (!client) return GAME_ERR_INVALID_ARG;
    if (client->initialized) return GAME_ERR_ALREADY_EXISTS;

    auto& engine = engine::Engine::Instance();

    if (config_dir && *config_dir) {
        engine::ConfigManager::Instance().Load(config_dir);
    }

    /* Library mode: create our own EventLoop so the user can drive it with
     * game_client_tick(). The loop is passed to Engine::Init as external_loop,
     * which means Engine won't start a frame timer — we own the cadence. */
    auto* loop = new (std::nothrow) evpp::EventLoop();
    if (!loop) {
        set_error(client, "EventLoop allocation failed");
        return GAME_ERR_OUT_OF_MEMORY;
    }
    client->owns_loop = true;

    engine::EngineConfig cfg;
    engine.Init(cfg, loop);

    client->initialized = true;
    return GAME_OK;
}

extern "C" game_error_t game_client_tick(game_client_t* client) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;

    auto& engine = engine::Engine::Instance();
    engine.Tick();
    return GAME_OK;
}

extern "C" game_error_t game_client_run(game_client_t* client) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;

    auto& engine = engine::Engine::Instance();
    engine.Run();  /* blocks until Shutdown() */
    return GAME_OK;
}

extern "C" game_error_t game_client_stop(game_client_t* client) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;

    auto& engine = engine::Engine::Instance();
    engine.Shutdown();  /* thread-safe signal to stop the event loop */
    return GAME_OK;
}

extern "C" void game_client_destroy(game_client_t** client) {
    if (!client || !*client) return;

    game_client_t* c = *client;
    if (c->initialized) {
        auto& engine = engine::Engine::Instance();
        engine.Cleanup();

        if (c->owns_loop) {
            auto* loop = engine.GetEventLoop();
            if (loop) {
                delete loop;
            }
        }
        c->initialized = false;
    }

    delete c;
    *client = nullptr;
}

extern "C" bool game_client_is_running(game_client_t* client) {
    if (!client || !client->initialized) return false;
    return engine::Engine::Instance().running();
}

/* =========================================================================
 * Script execution
 * ========================================================================= */

static engine::ScriptVM* get_vm() {
    try {
        return &engine::Engine::Instance().GetScriptVM();
    } catch (...) {
        return nullptr;
    }
}

extern "C" game_error_t game_client_do_string(game_client_t* client,
                                              const char* script,
                                              char* error_out,
                                              int error_size) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!script) return GAME_ERR_INVALID_ARG;

    auto* vm = get_vm();
    if (!vm) {
        set_error(client, "ScriptVM not available");
        return GAME_ERR_SCRIPT;
    }

    std::string err;
    bool ok = vm->DoString(script, "=CAPI", &err);
    if (!ok) {
        set_error(client, err.c_str());
        if (error_out && error_size > 0) {
            std::strncpy(error_out, err.c_str(),
                         static_cast<size_t>(error_size) - 1);
            error_out[error_size - 1] = '\0';
        }
        return GAME_ERR_SCRIPT;
    }
    return GAME_OK;
}

extern "C" game_error_t game_client_do_file(game_client_t* client,
                                            const char* filename,
                                            char* error_out,
                                            int error_size) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!filename) return GAME_ERR_INVALID_ARG;

    auto* vm = get_vm();
    if (!vm) {
        set_error(client, "ScriptVM not available");
        return GAME_ERR_SCRIPT;
    }

    std::string err;
    bool ok = vm->DoFile(filename, &err);
    if (!ok) {
        set_error(client, err.c_str());
        if (error_out && error_size > 0) {
            std::strncpy(error_out, err.c_str(),
                         static_cast<size_t>(error_size) - 1);
            error_out[error_size - 1] = '\0';
        }
        return GAME_ERR_SCRIPT;
    }
    return GAME_OK;
}

extern "C" game_error_t game_client_register_function(
    game_client_t* client, const char* name, game_lua_cfunction_t func)
{
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;
    if (!name || !func) return GAME_ERR_INVALID_ARG;

    auto* vm = get_vm();
    if (!vm) {
        set_error(client, "ScriptVM not available");
        return GAME_ERR_SCRIPT;
    }

    vm->RegisterFunction(name, reinterpret_cast<lua_CFunction>(func));
    return GAME_OK;
}

/* =========================================================================
 * Utility
 * ========================================================================= */

extern "C" int game_client_last_error(game_client_t* client,
                                      char* buf, int buf_size) {
    if (!client || !buf || buf_size <= 0) return 0;

    std::lock_guard<std::mutex> lock(client->error_mutex);
    if (client->last_error.empty()) return 0;

    int n = std::snprintf(buf, static_cast<size_t>(buf_size), "%s",
                          client->last_error.c_str());
    return (n > 0) ? n : 0;
}

extern "C" void* game_client_get_lua_state(game_client_t* client) {
    if (!client || !client->initialized) return nullptr;
    auto* vm = get_vm();
    return vm ? vm->GetState() : nullptr;
}
