#include <signal.h>
#include <evpp/event_watcher.h>
#include <evpp/event_loop.h>

#include "runtime/core/log/log.h"

#include "tests/examples/winmain-inl.h"

int main(int argc, char* argv[]) {
    evpp::EventLoop loop;
    std::unique_ptr<evpp::SignalEventWatcher> ev(
        new evpp::SignalEventWatcher(
            SIGINT, &loop, []() { ENGINE_LOG_INFO(engine::GetLogger(), "SIGINT caught.");}));
    ev->Init();
    ev->AsyncWait();
    loop.Run();
    return 0;
}

