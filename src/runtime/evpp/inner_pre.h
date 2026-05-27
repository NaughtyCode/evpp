#pragma once

#include <assert.h>
#include <stdint.h>

#ifdef __cplusplus
#include <functional>
#include <iostream>
#include <memory>
#endif	// end of define __cplusplus

#include "runtime/core/log/log.h"
#include "runtime/evpp/platform_config.h"
#include "runtime/evpp/sockets.h"
#include "runtime/evpp/sys_addrinfo.h"
#include "runtime/evpp/sys_sockets.h"

struct event;
namespace evpp {
int EventAdd(struct event* ev, const struct timeval* timeout);
int EventDel(struct event*);
EVPP_EXPORT int GetActiveEventCount();
}
