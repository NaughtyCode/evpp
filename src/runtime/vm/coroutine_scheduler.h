#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {

// CoroutineScheduler — cooperative Lua coroutine manager
//
// Manages lifecycle of Lua coroutines (create, suspend, resume, cancel).
// Driven by Engine::FrameLoop — each frame, resumes runnable coroutines.
//
// Each coroutine has:
//   - A lua_State* (lua_newthread) that holds its call stack
//   - A state: Suspended (waiting), Runnable (ready to resume), Dead (done)
//   - An optional wake time for timer-based suspension
//
// Thread safety: All methods are called from the main thread only.

class CLOUD_ENGINE_API CoroutineScheduler {
	public:
	static CoroutineScheduler& Instance();

	CoroutineScheduler(const CoroutineScheduler&) = delete;
	CoroutineScheduler& operator=(const CoroutineScheduler&) = delete;

	// Initialize with the main Lua state. Must be called once before use.
	void Init(lua_State* main_L);

	// Create a coroutine from a Lua function on the stack.
	// Pops the function. Returns a handle (>= 1) or 0 on failure.
	int CreateCoroutine(lua_State* L);

	// Resume all runnable coroutines. Called each frame from Engine::FrameLoop.
	// Limits total time spent to max_yield_ms (0 = no limit).
	void Update(int max_yield_ms = 0);

	// Resume a specific coroutine with the given number of results
	// already pushed onto its stack. Called when an async operation completes.
	void ResumeCoroutine(int handle, int num_results);

	// Cancel and clean up a coroutine by handle.
	void CancelCoroutine(int handle);

	// Cancel all coroutines.
	void CancelAll();

	// Suspend the current coroutine with an optional wake time.
	// If wake_at_ms > 0, the coroutine will be marked Runnable after that time.
	static void Suspend(int64_t wake_at_ms = 0);

	// Get the handle of the currently running coroutine, or 0 if none.
	static int CurrentHandle();

	// Number of active (non-dead) coroutines.
	size_t ActiveCount() const;

	// Configure limits.
	void SetMaxCoroutines(uint32_t max) { max_coroutines_ = max; }
	uint32_t GetMaxCoroutines() const { return max_coroutines_; }

	private:
	CoroutineScheduler() = default;
	~CoroutineScheduler() = default;

	enum class State : uint8_t {
		Suspended,  // Waiting for async result or timer
		Runnable,   // Ready to resume
		Dead,       // Completed or errored
	};

	struct CoroState {
		lua_State* thread = nullptr;
		int handle = 0;
		State state = State::Suspended;
		int64_t wake_at_ms = 0;  // 0 = no timer wait
	};

	// Remove dead coroutines from the map.
	void GarbageCollect();

	lua_State* main_L_ = nullptr;
	std::unordered_map<int, CoroState> coroutines_;
	int next_handle_ = 1;
	uint32_t max_coroutines_ = 10000;

};

}  // namespace engine
