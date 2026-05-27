#pragma once

#ifdef _DEBUG
#ifndef H_INTERNAL_STATS
#define H_INTERNAL_STATS
#endif
#endif

#include <atomic>

#include "runtime/evpp/duration.h"

namespace evpp {
namespace http {
namespace stats {

// The sum of these three durations is the actual processing time of a request at the application layer
struct Time {
	Duration
		dispatched_time;  // Time from receiving a request to when it is dispatched to a worker thread and starts executing
	Duration execute_time;	// Time spent executing the request in the worker thread
	Duration
		response_time;	// Time from when the request completes in the worker thread to when the response is sent by the listening thread
};

struct Count {
	std::atomic<uint64_t> recv;	 // Number of requests received
	std::atomic<uint64_t> dispatched;  // Number of requests dispatched to worker threads
	std::atomic<uint64_t> responsed;  // Number of requests responded to clients
	std::atomic<uint64_t> failed;  // Number of failed requests
	std::atomic<uint64_t> slow;	 // Number of slow requests (processing time exceeds a threshold)
};
}
}
}