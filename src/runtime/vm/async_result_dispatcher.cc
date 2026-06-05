#include "runtime/vm/async_result_dispatcher.h"

#include <exception>
#include <utility>

#include "runtime/core/log/log.h"
#include "runtime/vm/lua_error_handler.h"

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

bool AsyncResultDispatcher::IsOwnerThreadLocked() const noexcept {
	return owner_thread_id_ != std::thread::id{} &&
		   owner_thread_id_ == std::this_thread::get_id();
}

bool AsyncResultDispatcher::IsOwnerThread() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return IsOwnerThreadLocked();
}

AsyncCallbackId AsyncResultDispatcher::RegisterLuaCallback(lua_State* L,
														   int function_index) {
	if (!L) return 0;
	if (!lua_isfunction(L, function_index)) return 0;

	std::lock_guard<std::mutex> lock(mutex_);
	if (!IsOwnerThreadLocked()) return 0;
	if (L_ && L_ != L) return 0;
	if (shutdown_) return 0;

	lua_pushvalue(L, function_index);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	AsyncCallbackId id = next_callback_id_++;
	lua_callbacks_[id] = ref;
	if (!L_) L_ = L;
	return id;
}

bool AsyncResultDispatcher::ReleaseLuaCallback(AsyncCallbackId callback_id) {
	int ref = LUA_NOREF;
	lua_State* L = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!L_) return false;
		if (!IsOwnerThreadLocked()) return false;
		auto it = lua_callbacks_.find(callback_id);
		if (it == lua_callbacks_.end()) return false;
		ref = it->second;
		lua_callbacks_.erase(it);
		L = L_;
	}
	luaL_unref(L, LUA_REGISTRYINDEX, ref);
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
		int ref = LUA_NOREF;
		lua_State* L = nullptr;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!L_) return;
			auto it = lua_callbacks_.find(callback_id);
			if (it == lua_callbacks_.end()) return;
			ref = it->second;
			lua_callbacks_.erase(it);
			L = L_;
		}

		const int base = lua_gettop(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
		if (!lua_isfunction(L, -1)) {
			lua_settop(L, base);
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			return;
		}

		int argc = 0;
		try {
			argc = push_args(L);
			if (argc < 0 || lua_gettop(L) != base + 1 + argc) {
				auto* logger = GetLogger();
				ENGINE_LOG_ERROR(logger,
								 "AsyncResultDispatcher: Lua argument pusher returned invalid "
								 "argument count [{}]",
								 argc);
				lua_settop(L, base);
				luaL_unref(L, LUA_REGISTRYINDEX, ref);
				return;
			}
		} catch (const std::exception& e) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "AsyncResultDispatcher: Lua argument pusher failed: {}",
							 e.what());
			lua_settop(L, base);
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			return;
		} catch (...) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "AsyncResultDispatcher: Lua argument pusher failed: unknown");
			lua_settop(L, base);
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			return;
		}

		const int msgh = PushLuaErrorHandlerForCall(L, argc);
		if (lua_pcall(L, argc, 0, msgh) != LUA_OK) {
			const char* err = lua_tostring(L, -1);
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "AsyncResultDispatcher: Lua callback error: {}",
							 err ? err : "unknown");
		} else {
			lua_remove(L, msgh);
		}
		lua_settop(L, base);
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
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
			try {
				task();
			} catch (const std::exception& e) {
				auto* logger = GetLogger();
				ENGINE_LOG_ERROR(logger, "AsyncResultDispatcher: task exception: {}", e.what());
			} catch (...) {
				auto* logger = GetLogger();
				ENGINE_LOG_ERROR(logger, "AsyncResultDispatcher: task exception: unknown");
			}
			++dispatched;
		}
	}
	return dispatched;
}

void AsyncResultDispatcher::AdoptOwnerThread() {
	std::lock_guard<std::mutex> lock(mutex_);
	owner_thread_id_ = std::this_thread::get_id();
}

void AsyncResultDispatcher::ShutdownOnOwnerThread() {
	std::deque<AsyncTask> discarded;
	std::unordered_map<AsyncCallbackId, int> callbacks;
	lua_State* L = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!IsOwnerThreadLocked()) return;
		shutdown_ = true;
		discarded.swap(tasks_);
		callbacks.swap(lua_callbacks_);
		L = L_;
	}

	if (L) {
		for (const auto& [id, ref] : callbacks) {
			(void)id;
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
		}
	}
}

bool AsyncResultDispatcher::IsShutdown() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return shutdown_;
}

}  // namespace engine
