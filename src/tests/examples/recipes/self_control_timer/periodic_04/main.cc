#include "tests/examples/recipes/self_control_timer/periodic_04/invoke_timer.h"
#include "tests/examples/recipes/self_control_timer/periodic_04/event_watcher.h"
#include "tests/examples/winmain-inl.h"

#include <event2/event.h>

void Print() {
    std::cout << __FUNCTION__ << " hello world." << std::endl;
}

int main() {
    struct event_base* base = event_base_new();
    auto timer = recipes::InvokeTimer::Create(base, 1000.0, &Print, true);
    timer->Start();
    timer.reset();
    event_base_dispatch(base);
    event_base_free(base);
    return 0;
}