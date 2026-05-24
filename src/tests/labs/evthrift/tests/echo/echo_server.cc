
#include "tests/labs/evthrift/thrift_server.h"

#include "runtime/core/log/log.h"

#include "gen-cpp/Echo.h"

using namespace echo;

class EchoHandler : virtual public EchoIf {
public:
    EchoHandler() {}

    void echo(std::string& str, const std::string& s) {
        ENGINE_LOG_INFO(engine::GetLogger(), "EchoHandler::echo:{}", s);
        str = s;
    }

    void execute(Response& _return, const std::string& name, const Request& r) {
        using std::to_string;
        ENGINE_LOG_INFO(engine::GetLogger(), "name={} Request(num1={}, num2={}, c={}, d={}, comment={})", name, to_string(r.num1), to_string(r.num2), r.c, r.d, (r.__isset.comment ? (r.comment) : ("<null>")));
        _return.result = 0;
        _return.x = r.c;
        _return.y = r.d;
        _return.z = r.comment;
    }


    void ping() {
        ENGINE_LOG_INFO(engine::GetLogger(), "EchoHandler::ping ...");
    }

};

int main(int argc, char **argv) {
    evpp::EventLoop loop;
    std::string name("EchoServer");
    std::string addr = "0.0.0.0:9099";

    boost::shared_ptr<EchoHandler> handler(new EchoHandler());
    boost::shared_ptr<evthrift::TProcessor> processor(new EchoProcessor(handler));

    evthrift::ThriftServer server(processor, &loop, addr, name, 4);
    server.start();
    loop.Run();
    return 0;
}


#include "tests/examples/winmain-inl.h"
