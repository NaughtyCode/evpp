#include "test_common.h"

#include <evpp/sockets.h>
#include "runtime/core/log/log.h"

TEST_UNIT(Teststrerror) {
    ENGINE_LOG_ERROR(engine::GetLogger(), "{}", evpp::strerror(EAGAIN));
}


