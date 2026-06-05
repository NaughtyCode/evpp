#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "runtime/core/engine_api.h"

extern "C" {
#include "lua.h"
}

namespace engine {

using AsyncCallbackId = uint64_t;
using AsyncTask = std::function<void()>;
using LuaArgPusher = std::function<int(lua_State*)>;

class CLOUD_ENGINE_API AsyncResultDispatcher {
	public:
	explicit AsyncResultDispatcher(lua_State* L = nullptr);
	~AsyncResultDispatcher();

	AsyncResultDispatcher(const AsyncResultDispatcher&) = delete;
	AsyncResultDispatcher& operator=(const AsyncResultDispatcher&) = delete;

	AsyncCallbackId RegisterLuaCallback(lua_State* L, int function_index);
	bool ReleaseLuaCallback(AsyncCallbackId callback_id);
	bool Enqueue(AsyncTask task);
	bool EnqueueLuaCallback(AsyncCallbackId callback_id, LuaArgPusher push_args);
	size_t Dispatch(size_t max_count);
	void AdoptOwnerThread();
	void ShutdownOnOwnerThread();
	bool IsShutdown() const;
	bool IsOwnerThread() const;

	private:
	bool IsOwnerThreadLocked() const noexcept;

	std::thread::id owner_thread_id_;
	lua_State* L_ = nullptr;

	mutable std::mutex mutex_;
	std::deque<AsyncTask> tasks_;
	std::unordered_map<AsyncCallbackId, int> lua_callbacks_;
	AsyncCallbackId next_callback_id_ = 1;
	bool shutdown_ = false;
};

}  // namespace engine
