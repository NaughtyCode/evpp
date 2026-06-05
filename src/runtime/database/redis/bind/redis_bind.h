#pragma once

#include <cstddef>
#include <memory>
#include <optional>

#include "runtime/vm/async_result_dispatcher.h"

namespace engine {

class ScriptVM;

namespace script {

struct RedisLuaBindingContext {
	std::shared_ptr<AsyncResultDispatcher> dispatcher;
	std::optional<size_t> preferred_worker_index;
	size_t dispatch_batch_size = 256;
};

void ExportRedis(ScriptVM& vm, RedisLuaBindingContext context = {});
void ShutdownRedisBindings(ScriptVM& vm);

}  // namespace script
}  // namespace engine

