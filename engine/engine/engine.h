#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace evpp {
class EventLoop;
}

namespace engine {

class ScriptVM;

class Engine {
public:
    static Engine& Instance();

    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void Init(const std::string& log_dir,
              const std::string& scripts_dir = "resources/script");

    // Run the main loop. Blocks until Shutdown() is called from a signal
    // handler or another thread.
    void Run();

    // Request graceful shutdown. Safe to call from any thread.
    void Shutdown();

    bool running() const { return running_; }
    uint64_t frame_count() const { return frame_count_; }

    ScriptVM& GetScriptVM();

private:
    void FrameLoop();

    std::unique_ptr<evpp::EventLoop> loop_;
    std::chrono::steady_clock::time_point last_frame_time_;
    std::chrono::milliseconds frame_interval_{33};
    bool running_{false};
    uint64_t frame_count_{0};

    std::unique_ptr<ScriptVM> script_vm_;
};

} // namespace engine
