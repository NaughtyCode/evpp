#include "client.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic_bool g_stop_requested{false};

void HandleSignal(int) {
    g_stop_requested.store(true, std::memory_order_relaxed);
}

std::string LastError(game_client_t* client) {
    char buffer[4096];
    const int n = game_client_last_error(client, buffer, static_cast<int>(sizeof(buffer)));
    return n > 0 ? std::string(buffer, static_cast<size_t>(n)) : std::string();
}

bool StartsWith(const std::string& value, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return value.size() >= n && value.compare(0, n, prefix) == 0;
}

int ParseIntArg(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

void PrintUsage(const char* exe) {
    std::cout
        << "Usage: " << exe << " [options]\n"
        << "Options:\n"
        << "  --config_dir=<dir>    Config directory. Default: resources/config\n"
        << "  --script=<file>       Optional Lua file to run after client init\n"
        << "  --duration_ms=<ms>    Stop after this duration. 0 means run until signal\n"
        << "  --tick_ms=<ms>        Stop polling interval. Default: 16\n"
        << "  --help                Show this help\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_dir = "resources/config";
    std::string script_file;
    int duration_ms = 0;
    int tick_ms = 16;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
        if (StartsWith(arg, "--config_dir=")) {
            config_dir = arg.substr(std::strlen("--config_dir="));
        } else if (StartsWith(arg, "--script=")) {
            script_file = arg.substr(std::strlen("--script="));
        } else if (StartsWith(arg, "--duration_ms=")) {
            duration_ms = ParseIntArg(arg.substr(std::strlen("--duration_ms=")), duration_ms);
        } else if (StartsWith(arg, "--tick_ms=")) {
            tick_ms = ParseIntArg(arg.substr(std::strlen("--tick_ms=")), tick_ms);
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            PrintUsage(argv[0]);
            return 2;
        }
    }

    if (tick_ms < 1) {
        tick_ms = 1;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    game_client_t* client = nullptr;
    game_error_t rc = game_client_create(&client);
    if (rc != GAME_OK || !client) {
        std::cerr << "game_client_create failed: " << rc << "\n";
        return 1;
    }

    rc = game_client_init(client, config_dir.c_str());
    if (rc != GAME_OK) {
        std::cerr << "game_client_init failed: " << rc << " " << LastError(client) << "\n";
        game_client_destroy(&client);
        return 1;
    }

    if (!script_file.empty()) {
        char error[4096] = {};
        rc = game_client_do_file(client, script_file.c_str(), error, static_cast<int>(sizeof(error)));
        if (rc != GAME_OK) {
            std::cerr << "game_client_do_file failed: " << rc << " " << error << "\n";
            game_client_destroy(&client);
            return 1;
        }
    }

    std::thread stop_thread([client, duration_ms, tick_ms]() {
        const auto start = std::chrono::steady_clock::now();
        while (!g_stop_requested.load(std::memory_order_relaxed)) {
            if (duration_ms > 0) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsed >= duration_ms) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms));
        }
        game_client_stop(client);
    });

    rc = game_client_run(client);
    g_stop_requested.store(true, std::memory_order_relaxed);
    if (stop_thread.joinable()) {
        stop_thread.join();
    }

    if (rc != GAME_OK) {
        std::cerr << "game_client_run failed: " << rc << " " << LastError(client) << "\n";
        game_client_destroy(&client);
        return 1;
    }

    game_client_destroy(&client);
    return 0;
}
