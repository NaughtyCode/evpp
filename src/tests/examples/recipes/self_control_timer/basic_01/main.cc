#include "tests/examples/recipes/self_control_timer/basic_01/invoke_timer.h"
#include "tests/examples/recipes/self_control_timer/basic_01/event_watcher.h"
#include "tests/examples/winmain-inl.h"

#include <event2/event.h>

void Print() {
    std::cout << __FUNCTION__ << " hello world." << std::endl;
}

int main() {
    struct event_base* base = event_base_new();
    auto timer = new recipes::InvokeTimer(base, 1000.0, &Print);
    timer->Start();
    event_base_dispatch(base);
    delete timer;
    event_base_free(base);
    return 0;
}