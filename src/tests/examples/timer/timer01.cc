#include <evpp/event_loop.h>

#ifdef _WIN32
#include "tests/examples/winmain-inl.h"
#endif

void Print() {
    std::cout << "Hello, world!\n";
}

int main() {
    evpp::EventLoop loop;
    loop.RunAfter(evpp::Duration(5.0), &Print);
    loop.Run();
    return 0;
}