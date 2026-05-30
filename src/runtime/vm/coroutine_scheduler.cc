#include "runtime/vm/coroutine_scheduler.h"

#include <chrono>
#include <cstdint>

#include "runtime/core/log/log.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {

namespace {
thread_local int tls_current_handle = 0;
}

CoroutineScheduler& CoroutineScheduler::Instance() {
	static CoroutineScheduler sched;
	return sched;
}

void CoroutineScheduler::Init(lua_State* main_L) {
	if (main_L_ && main_L_ != main_L) {
		CancelAll();
	}
	main_L_ = main_L;
	tls_current_handle = 0;
}

int CoroutineScheduler::CreateCoroutine(lua_State* L) {
	if (!main_L_) {
		if (L && lua_gettop(L) > 0 && lua_isfunction(L, -1)) lua_pop(L, 1);
		return 0;
	}

	// Check limit
	if (coroutines_.size() >= static_cast<size_t>(max_coroutines_)) {
		auto* logger = GetLogger();
		ENGINE_LOG_WARN(logger,
						"CoroutineScheduler: max coroutines ({}) reached, rejecting",
						max_coroutines_);
		if (lua_gettop(L) > 0 && lua_isfunction(L, -1)) lua_pop(L, 1);
		return 0;
	}

	// Function should be at top of stack
	if (lua_gettop(L) == 0 || !lua_isfunction(L, -1)) {
		if (lua_gettop(L) > 0) lua_pop(L, 1);
		return 0;
	}

	// Create a new Lua thread (coroutine)
	int base_top = lua_gettop(L);
	lua_State* thread = lua_newthread(L);
	if (!thread) {
		lua_settop(L, base_top - 1);
		return 0;
	}

	// Stack after lua_newthread: ..., func, thread_ref.
	// Rotate func to top, then move it to the new thread.
	lua_insert(L, -2);        // ..., thread_ref, func
	lua_xmove(L, thread, 1);  // func moved to thread; thread_ref stays on L
	int thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	int handle = next_handle_++;

	CoroState cs;
	cs.thread = thread;
	cs.thread_ref = thread_ref;
	cs.handle = handle;
	cs.state = State::Runnable;

	coroutines_[handle] = cs;
	return handle;
}

void CoroutineScheduler::Update(int max_yield_ms) {
	if (coroutines_.empty()) return;

	auto start = std::chrono::steady_clock::now();

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

		tls_current_handle = handle;

		int nargs = cs.started ? cs.pending_resume_args : 0;
		cs.started = true;
		cs.pending_resume_args = 0;
		int nresults = 0;
		int ret = lua_resume(cs.thread, main_L_, nargs, &nresults);

		tls_current_handle = 0;

		if (ret == LUA_OK) {
			if (nresults > 0) lua_pop(cs.thread, nresults);
			cs.state = State::Dead;
		} else if (ret == LUA_YIELD) {
			if (nresults > 0) lua_pop(cs.thread, nresults);
			cs.state = State::Suspended;
		} else {
			// Error
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "CoroutineScheduler: coroutine [{}] error: {}",
							 handle,
							 lua_tostring(cs.thread, -1));
			lua_settop(cs.thread, 0);
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
	if (num_results < 0) return;

	// Results are already on the coroutine's stack (pushed by caller)
	int available = lua_gettop(cs.thread);
	if (num_results > available) {
		auto* logger = GetLogger();
		ENGINE_LOG_WARN(logger,
						"CoroutineScheduler: resume [{}] requested [{}] arg(s), only [{}] on stack",
						handle,
						num_results,
						available);
		num_results = available;
	}
	cs.pending_resume_args = num_results;
	cs.state = State::Runnable;
}

void CoroutineScheduler::CancelCoroutine(int handle) {
	auto it = coroutines_.find(handle);
	if (it == coroutines_.end()) return;

	// lua_State is a lua_newthread — Lua GC will collect it when unreferenced
	ReleaseCoroutine(it->second);
	coroutines_.erase(it);
}

void CoroutineScheduler::CancelAll() {
	for (auto& [handle, cs] : coroutines_) {
		(void)handle;
		ReleaseCoroutine(cs);
	}
	coroutines_.clear();
}

void CoroutineScheduler::Suspend(int64_t wake_at_ms) {
	if (tls_current_handle > 0) {
		auto it = Instance().coroutines_.find(tls_current_handle);
		if (it != Instance().coroutines_.end()) {
			it->second.wake_at_ms = wake_at_ms;
		}
	}
}

int CoroutineScheduler::CurrentHandle() {
	return tls_current_handle;
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
			ReleaseCoroutine(it->second);
			it = coroutines_.erase(it);
		} else {
			++it;
		}
	}
}

void CoroutineScheduler::ReleaseCoroutine(CoroState& cs) {
	if (cs.thread) {
		lua_settop(cs.thread, 0);
	}
	if (main_L_ && cs.thread_ref != LUA_NOREF) {
		luaL_unref(main_L_, LUA_REGISTRYINDEX, cs.thread_ref);
		cs.thread_ref = LUA_NOREF;
	}
	cs.thread = nullptr;
	cs.pending_resume_args = 0;
}

}  // namespace engine
