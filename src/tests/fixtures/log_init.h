#pragma once

#include <runtime/config/config.h>
#include <runtime/core/log/log.h>

// Static initializer: starts the quill backend and creates a "root" logger
// before main(). Required because many engine/evpp components call
// GetLogger() during construction, but the backend is normally started
// by Engine::Init() which tests may bypass.
namespace {
struct TestLogInit {
    TestLogInit() {
        engine::LogConfig cfg;
        cfg.logger_name = "root";
        cfg.dir = "logs";
        engine::InitLogger(cfg);
    }
};
static TestLogInit s_test_log_init;
}
