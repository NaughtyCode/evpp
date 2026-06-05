#include "runtime/vm/async_result_dispatcher.h"

#include <exception>
#include <utility>

#include "runtime/core/log/log.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {

AsyncResultDispatcher::AsyncResultDispatcher(lua_State* L)
	: owner_thread_id_(std::this_thread::get_id()), L_(L) {}

AsyncResultDispatcher::~AsyncResultDispatcher() {
	if (L_ && IsOwnerThread() && !IsShutdown()) {
		ShutdownOnOwnerThread();
	}
}

bool AsyncResultDispatcher::IsOwnerThread() const noexcept {
	return owner_thread_id_ != std::thread::id{} &&
		   owner_thread_id_ == std::this_thread::get_id();
}

AsyncCallbackId AsyncResultDispatcher::RegisterLuaCallback(lua_State* L,
														   int function_index) {
	if (!L || !IsOwnerThread()) return 0;
	if (!lua_isfunction(L, function_index)) return 0;

	std::lock_guard<std::mutex> lock(mutex_);
	if (shutdown_) return 0;

	lua_pushvalue(L, function_index);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	AsyncCallbackId id = next_callback_id_++;
	lua_callbacks_[id] = ref;
	if (!L_) L_ = L;
	return id;
}

bool AsyncResultDispatcher::ReleaseLuaCallback(AsyncCallbackId callback_id) {
	if (!IsOwnerThread() || !L_) return false;
	int ref = LUA_NOREF;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = lua_callbacks_.find(callback_id);
		if (it == lua_callbacks_.end()) return false;
		ref = it->second;
		lua_callbacks_.erase(it);
	}
	luaL_unref(L_, LUA_REGISTRYINDEX, ref);
	return true;
}

bool AsyncResultDispatcher::Enqueue(AsyncTask task) {
	if (!task) return false;
	std::lock_guard<std::mutex> lock(mutex_);
	if (shutdown_) return false;
	tasks_.push_back(std::move(task));
	return true;
}

bool AsyncResultDispatcher::EnqueueLuaCallback(AsyncCallbackId callback_id,
											   LuaArgPusher push_args) {
	if (!push_args) return false;
	return Enqueue([this, callback_id, push_args = std::move(push_args)]() mutable {
		if (!L_) return;

		int ref = LUA_NOREF;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = lua_callbacks_.find(callback_id);
			if (it == lua_callbacks_.end()) return;
			ref = it->second;
			lua_callbacks_.erase(it);
		}

		const int base = lua_gettop(L_);
		lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
		if (!lua_isfunction(L_, -1)) {
			lua_settop(L_, base);
			luaL_unref(L_, LUA_REGISTRYINDEX, ref);
			return;
		}

		int argc = 0;
		try {
			argc = push_args(L_);
		} catch (const std::exception& e) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "AsyncResultDispatcher: Lua argument pusher failed: {}",
							 e.what());
			lua_settop(L_, base);
			luaL_unref(L_, LUA_REGISTRYINDEX, ref);
			return;
		} catch (...) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "AsyncResultDispatcher: Lua argument pusher failed: unknown");
			lua_settop(L_, base);
			luaL_unref(L_, LUA_REGISTRYINDEX, ref);
			return;
		}

		if (lua_pcall(L_, argc, 0, 0) != LUA_OK) {
			const char* err = lua_tostring(L_, -1);
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "AsyncResultDispatcher: Lua callback error: {}",
							 err ? err : "unknown");
		}
		lua_settop(L_, base);
		luaL_unref(L_, LUA_REGISTRYINDEX, ref);
	});
}

size_t AsyncResultDispatcher::Dispatch(size_t max_count) {
	if (!IsOwnerThread()) return 0;
	size_t dispatched = 0;
	while (max_count == 0 || dispatched < max_count) {
		AsyncTask task;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (tasks_.empty()) break;
			task = std::move(tasks_.front());
			tasks_.pop_front();
		}
		if (task) {
			task();
			++dispatched;
		}
	}
	return dispatched;
}

void AsyncResultDispatcher::ShutdownOnOwnerThread() {
	if (!IsOwnerThread()) return;

	std::deque<AsyncTask> discarded;
	std::unordered_map<AsyncCallbackId, int> callbacks;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		shutdown_ = true;
		discarded.swap(tasks_);
		callbacks.swap(lua_callbacks_);
	}

	if (L_) {
		for (const auto& [id, ref] : callbacks) {
			(void)id;
			luaL_unref(L_, LUA_REGISTRYINDEX, ref);
		}
	}
}

bool AsyncResultDispatcher::IsShutdown() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return shutdown_;
}

}  // namespace engine
