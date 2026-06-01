/*
 * client.cpp — Engine lifecycle, script execution, and utility functions.
 *
 * Compiles the runtime sources into GameClient and wraps its C++ classes:
 *   engine::Engine   — singleton lifecycle (Init/Tick/Cleanup)
 *   engine::ScriptVM — Lua VM (DoString/DoFile/RegisterFunction)
 */

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#include "client_internal.h"

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/evpp/event_loop.h"
#include "runtime/vm/vm.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>

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

extern "C" game_error_t game_client_set_log_prefix(game_client_t* client,
                                                   const char* prefix) {
    if (!client) return GAME_ERR_INVALID_ARG;
    if (client->initialized) return GAME_ERR_ALREADY_EXISTS;

    client->log_prefix_override = (prefix && *prefix) ? prefix : "";
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_client_init(game_client_t* client,
                                         const char* config_dir) {
    if (!client) return GAME_ERR_INVALID_ARG;
    if (client->initialized) return GAME_ERR_ALREADY_EXISTS;

    auto& engine = engine::Engine::Instance();
    auto& cfg_mgr = engine::ConfigManager::Instance();

    if (config_dir && *config_dir) {
        try {
            if (!cfg_mgr.Load(config_dir)) {
                set_error_f(client, "failed to load config directory: %s", config_dir);
                return GAME_ERR_GENERIC;
            }
        } catch (const std::exception& ex) {
            set_error_f(client, "failed to load config directory: %s", ex.what());
            return GAME_ERR_GENERIC;
        } catch (...) {
            set_error(client, "failed to load config directory");
            return GAME_ERR_GENERIC;
        }
    }

    if (!client->log_prefix_override.empty()) {
        auto runtime_cfg = cfg_mgr.GetRuntimeConfig();
        runtime_cfg.log.log_filename = client->log_prefix_override;
        cfg_mgr.SetRuntimeOverride(runtime_cfg);
    }

    /* WinSock must be initialised before creating the EventLoop because
     * its constructor opens a socket pair for the notification pipe. */
#ifdef _WIN32
    WSADATA wsa_data;
    int wsa_err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_err) {
        set_error_f(client, "WSAStartup failed: %d", wsa_err);
        return GAME_ERR_NETWORK;
    }
#endif

    /* Library mode: create our own EventLoop so the user can drive it with
     * game_client_tick(). The loop is passed to Engine::Init as external_loop,
     * which means Engine won't start a frame timer — we own the cadence. */
    auto* loop = new (std::nothrow) evpp::EventLoop();
    if (!loop) {
        set_error(client, "EventLoop allocation failed");
#ifdef _WIN32
        WSACleanup();
#endif
        return GAME_ERR_OUT_OF_MEMORY;
    }
    client->owns_loop = true;
    client->loop = loop;

    try {
        const auto runtime_cfg = cfg_mgr.GetRuntimeConfig();
        const auto client_cfg = cfg_mgr.GetClientConfig();
        engine.Init(runtime_cfg, client_cfg.scripts_dir, loop);
    } catch (const std::exception& ex) {
        set_error_f(client, "engine init failed: %s", ex.what());
        delete loop;
        client->owns_loop = false;
        client->loop = nullptr;
#ifdef _WIN32
        WSACleanup();
#endif
        return GAME_ERR_GENERIC;
    } catch (...) {
        set_error(client, "engine init failed");
        delete loop;
        client->owns_loop = false;
        client->loop = nullptr;
#ifdef _WIN32
        WSACleanup();
#endif
        return GAME_ERR_GENERIC;
    }

    client->initialized = true;
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_client_tick(game_client_t* client) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;

    auto& engine = engine::Engine::Instance();
    try {
        engine.Tick();
    } catch (const std::exception& ex) {
        set_error_f(client, "engine tick failed: %s", ex.what());
        return GAME_ERR_GENERIC;
    } catch (...) {
        set_error(client, "engine tick failed");
        return GAME_ERR_GENERIC;
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_client_run(game_client_t* client) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;

    auto& engine = engine::Engine::Instance();
    try {
        engine.Run();  /* blocks until Shutdown() */
    } catch (const std::exception& ex) {
        set_error_f(client, "engine run failed: %s", ex.what());
        return GAME_ERR_GENERIC;
    } catch (...) {
        set_error(client, "engine run failed");
        return GAME_ERR_GENERIC;
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" game_error_t game_client_stop(game_client_t* client) {
    if (!client || !client->initialized) return GAME_ERR_INVALID_ARG;

    auto& engine = engine::Engine::Instance();
    engine.Shutdown();  /* thread-safe signal to stop the event loop */
    if (client->owns_loop && client->loop) {
        client->loop->Stop();
    }
    clear_error(client);
    return GAME_OK;
}

extern "C" void game_client_destroy(game_client_t** client) {
    if (!client || !*client) return;

    game_client_t* c = *client;
    if (c->initialized) {
        auto& engine = engine::Engine::Instance();

        /* Save the loop pointer before Cleanup() sets loop_ to nullptr. */
        evpp::EventLoop* loop = c->loop ? c->loop : engine.GetEventLoop();

        try {
            engine.Cleanup();
        } catch (...) {
            /* Destructors and FFI shutdown paths must not throw across C ABI. */
        }

        if (c->owns_loop && loop) {
            delete loop;
        }
        c->loop = nullptr;
#ifdef _WIN32
        WSACleanup();
#endif
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
    clear_error(client);
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
    clear_error(client);
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
    clear_error(client);
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

    const size_t cap = static_cast<size_t>(buf_size);
    const size_t copied = std::min(client->last_error.size(), cap - 1);
    std::memcpy(buf, client->last_error.data(), copied);
    buf[copied] = '\0';
    return static_cast<int>(copied);
}

extern "C" void game_client_clear_error(game_client_t* client) {
    clear_error(client);
}

extern "C" void* game_client_get_lua_state(game_client_t* client) {
    if (!client || !client->initialized) return nullptr;
    auto* vm = get_vm();
    return vm ? vm->GetState() : nullptr;
}
