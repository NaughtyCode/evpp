#include "runtime/vm/coroutine_scheduler.h"

#include <chrono>
#include <cstdint>

#include "runtime/core/log/log.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {

const char* CoroutineScheduler::kCurrentHandleKey = "__coro_handle";

CoroutineScheduler& CoroutineScheduler::Instance() {
	static CoroutineScheduler sched;
	return sched;
}

void CoroutineScheduler::Init(lua_State* main_L) {
	main_L_ = main_L;
}

int CoroutineScheduler::CreateCoroutine(lua_State* L) {
	if (!main_L_) return 0;

	// Check limit
	if (coroutines_.size() >= static_cast<size_t>(max_coroutines_)) {
		auto* logger = GetLogger();
		ENGINE_LOG_WARN(logger,
						"CoroutineScheduler: max coroutines ({}) reached, rejecting",
						max_coroutines_);
		return 0;
	}

	// Function should be at top of stack
	if (!lua_isfunction(L, -1)) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "CoroutineScheduler: CreateCoroutine called without function on stack");
		lua_pop(L, 1);
		return 0;
	}

	// Create a new Lua thread (coroutine)
	lua_State* thread = lua_newthread(L);
	if (!thread) {
		lua_pop(L, 2);  // pop thread and function
		return 0;
	}

	// Move function to the new thread's stack
	lua_xmove(L, thread, 1);  // function is now on thread's stack

	int handle = next_handle_++;

	CoroState cs;
	cs.thread = thread;
	cs.handle = handle;
	cs.state = State::Runnable;

	coroutines_[handle] = cs;
	return handle;
}

void CoroutineScheduler::Update(int max_yield_ms) {
	if (coroutines_.empty()) return;

	auto start = std::chrono::steady_clock::now();
	auto deadline = start + std::chrono::milliseconds(max_yield_ms);

	// Collect handles to resume (avoid iterator invalidation)
	std::vector<int> runnable;
	runnable.reserve(coroutines_.size());

	auto now = std::chrono::steady_clock::now();
	auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					  now.time_since_epoch()).count();

	for (auto& [handle, cs] : coroutines_) {
		if (cs.state == State::Dead) continue;
		// Check timer wake
		if (cs.state == State::Suspended && cs.wake_at_ms > 0 &&
			now_ms >= cs.wake_at_ms) {
			cs.state = State::Runnable;
		}
		if (cs.state == State::Runnable) {
			runnable.push_back(handle);
		}
	}

	for (int handle : runnable) {
		// Check time budget
		if (max_yield_ms > 0) {
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
							   std::chrono::steady_clock::now() - start).count();
			if (elapsed >= max_yield_ms) break;
		}

		auto it = coroutines_.find(handle);
		if (it == coroutines_.end()) continue;
		auto& cs = it->second;

		// Store current handle in registry so Suspend/CurrentHandle work
		lua_pushinteger(main_L_, handle);
		lua_setfield(main_L_, LUA_REGISTRYINDEX, kCurrentHandleKey);

		int nargs = lua_gettop(cs.thread) > 0 ? 1 : 0;
		int ret = lua_resume(cs.thread, main_L_, nargs);

		// Clear current handle
		lua_pushnil(main_L_);
		lua_setfield(main_L_, LUA_REGISTRYINDEX, kCurrentHandleKey);

		if (ret == LUA_OK) {
			cs.state = State::Dead;
		} else if (ret == LUA_YIELD) {
			cs.state = State::Suspended;
		} else {
			// Error
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "CoroutineScheduler: coroutine [{}] error: {}",
							 handle,
							 lua_tostring(cs.thread, -1));
			lua_pop(cs.thread, 1);
			cs.state = State::Dead;
		}
	}

	GarbageCollect();
}

void CoroutineScheduler::ResumeCoroutine(int handle, int num_results) {
	auto it = coroutines_.find(handle);
	if (it == coroutines_.end()) return;
	auto& cs = it->second;

	if (cs.state != State::Suspended) return;

	// Results are already on the coroutine's stack (pushed by caller)
	cs.state = State::Runnable;
}

void CoroutineScheduler::CancelCoroutine(int handle) {
	auto it = coroutines_.find(handle);
	if (it == coroutines_.end()) return;

	// lua_State is a lua_newthread — Lua GC will collect it when unreferenced
	coroutines_.erase(it);
}

void CoroutineScheduler::CancelAll() {
	coroutines_.clear();
}

void CoroutineScheduler::Suspend(int64_t wake_at_ms) {
	// Sets the wake time for the currently running coroutine.
	// Must be called from within a coroutine context.
	// The coroutine handle is stored in the registry before resume.
	// After setting the wake time, the coroutine should call coroutine.yield().
	if (!Instance().main_L_) return;

	lua_getfield(Instance().main_L_, LUA_REGISTRYINDEX, kCurrentHandleKey);
	int handle = static_cast<int>(lua_tointeger(Instance().main_L_, -1));
	lua_pop(Instance().main_L_, 1);

	if (handle > 0) {
		auto it = Instance().coroutines_.find(handle);
		if (it != Instance().coroutines_.end()) {
			it->second.wake_at_ms = wake_at_ms;
		}
	}
}

int CoroutineScheduler::CurrentHandle() {
	if (!Instance().main_L_) return 0;
	lua_getfield(Instance().main_L_, LUA_REGISTRYINDEX, kCurrentHandleKey);
	int handle = static_cast<int>(lua_tointeger(Instance().main_L_, -1));
	lua_pop(Instance().main_L_, 1);
	return handle;
}

size_t CoroutineScheduler::ActiveCount() const {
	size_t count = 0;
	for (const auto& [handle, cs] : coroutines_) {
		if (cs.state != State::Dead) ++count;
	}
	return count;
}

void CoroutineScheduler::GarbageCollect() {
	for (auto it = coroutines_.begin(); it != coroutines_.end(); ) {
		if (it->second.state == State::Dead) {
			it = coroutines_.erase(it);
		} else {
			++it;
		}
	}
}

}  // namespace engine
