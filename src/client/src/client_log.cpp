/*
 * client_log.cpp — Logging wrappers.
 *
 * Routes C API log calls through the engine's Quill async logger.
 * Thread-safe: Quill itself is lock-free for log submission.
 */

#include "client_internal.h"

#include "runtime/core/log/log_macros.h"

extern "C" {

void game_log_trace(game_client_t* client, const char* msg) {
    (void)client;
    if (!msg) return;
    auto* logger = engine::GetLogger();
    ENGINE_LOG_TRACE(logger, "[capi] {}", msg);
}

void game_log_debug(game_client_t* client, const char* msg) {
    (void)client;
    if (!msg) return;
    auto* logger = engine::GetLogger();
    ENGINE_LOG_DEBUG(logger, "[capi] {}", msg);
}

void game_log_info(game_client_t* client, const char* msg) {
    (void)client;
    if (!msg) return;
    auto* logger = engine::GetLogger();
    ENGINE_LOG_INFO(logger, "[capi] {}", msg);
}

void game_log_warn(game_client_t* client, const char* msg) {
    (void)client;
    if (!msg) return;
    auto* logger = engine::GetLogger();
    ENGINE_LOG_WARN(logger, "[capi] {}", msg);
}

void game_log_error(game_client_t* client, const char* msg) {
    (void)client;
    if (!msg) return;
    auto* logger = engine::GetLogger();
    ENGINE_LOG_ERROR(logger, "[capi] {}", msg);
}

void game_log_fatal(game_client_t* client, const char* msg) {
    (void)client;
    if (!msg) return;
    auto* logger = engine::GetLogger();
    ENGINE_LOG_CRITICAL(logger, "[capi] {}", msg);
}

} /* extern "C" */
