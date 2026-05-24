#include <evpp/event_loop.h>
#include <evpp/dns_resolver.h>
#include <evpp/sockets.h>

#ifdef WIN32
#include "tests/examples/winmain-inl.h"
#endif

#include "runtime/core/log/log.h"

int main(int argc, char* argv[]) {
    std::string host = "www.so.com";
    if (argc > 1) {
        host = argv[1];
    }

    evpp::EventLoop loop;

    auto fn_resolved = [&loop, &host](const std::vector <struct in_addr>& addrs) {
        ENGINE_LOG_INFO(engine::GetLogger(), "Entering fn_resolved");
        for (auto addr : addrs) {
            struct sockaddr_in saddr;
            memset(&saddr, 0, sizeof(saddr));
            saddr.sin_addr = addr;
            ENGINE_LOG_INFO(engine::GetLogger(), "DNS resolved {} ip {}", host, evpp::sock::ToIP(evpp::sock::sockaddr_cast(&saddr)));
        }

        loop.RunAfter(evpp::Duration(0.5), [&loop]() { loop.Stop(); });
    };

    std::shared_ptr<evpp::DNSResolver> dns_resolver(new evpp::DNSResolver(&loop , host, evpp::Duration(1.0), fn_resolved));
    dns_resolver->Start();
    loop.Run();

    return 0;
}
