#include <stdio.h>
#include <stdlib.h>

#include <evpp/event_loop_thread.h>

#include <evpp/httpc/request.h>
#include <evpp/httpc/conn.h>
#include <evpp/httpc/response.h>

#include "runtime/core/log/log.h"

#include "tests/examples/winmain-inl.h"

static bool responsed = false;
static void HandleHTTPResponse(const std::shared_ptr<evpp::httpc::Response>& response, evpp::httpc::GetRequest* request) {
    ENGINE_LOG_INFO(engine::GetLogger(), "http_code={} [{}]", response->http_code(), response->body().ToString());
    std::string header = response->FindHeader("Connection");
    ENGINE_LOG_INFO(engine::GetLogger(), "HTTP HEADER Connection={}", header);
    responsed = true;
    assert(request == response->request());
    delete request; // The request MUST BE deleted in EventLoop thread.
}

int main(int argc, char* argv[]) {
    engine::InitLogger({});

    evpp::EventLoopThread t;
    t.Start(true);
    evpp::httpc::GetRequest* r = new evpp::httpc::GetRequest(t.loop(), "http://www.so.com/status.html", evpp::Duration(2.0));
    ENGINE_LOG_INFO(engine::GetLogger(), "Do http request");
    r->Execute(std::bind(&HandleHTTPResponse, std::placeholders::_1, r));

    while (!responsed) {
        usleep(1);
    }

    t.Stop(true);
    ENGINE_LOG_INFO(engine::GetLogger(), "EventLoopThread stopped.");
    return 0;
}
