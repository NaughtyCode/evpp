#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// NetAliveGuard is a cross-thread lifetime guard for RunInLoop callbacks
// that capture lua_State* or Lua registry references.
//
// Usage:
//   1. Declare a static/global instance: static NetAliveGuard g_xxx_alive;
//   2. In every RunInLoop callback that touches Lua state:
//        if (!g_xxx_alive.TryAcquire()) return;
//        // ... Lua operations ...
//        g_xxx_alive.Release();
//   3. During shutdown:
//        g_xxx_alive.Shutdown();
//        // stop servers, cancel pending ops ...
//        g_xxx_alive.WaitDrain();
//        // safe to destroy lua_State and free resources
//
// Thread safety: TryAcquire/Release may be called from any thread
// (typically the main event loop). Shutdown/WaitDrain must be called
// from the shutdown thread, not inside a TryAcquire/Release scope.
class CLOUD_ENGINE_API NetAliveGuard {
public:
	NetAliveGuard() = default;
	~NetAliveGuard() = default;

	NetAliveGuard(const NetAliveGuard&) = delete;
	NetAliveGuard& operator=(const NetAliveGuard&) = delete;

	/* Quick check without acquiring; use for non-critical paths. */
	bool IsAlive() const {
		return alive_.load(std::memory_order_acquire);
	}

	/* Atomically mark the guard as dead. New TryAcquire calls return false.
	 * Already-acquired callbacks continue to run; call WaitDrain to
	 * synchronize with them. */
	void Shutdown() {
		alive_.store(false, std::memory_order_release);
	}

	/* Re-enable the guard after a previous shutdown/re-export cycle.
	 * Call only after WaitDrain() has observed zero pending callbacks. */
	void Reset();

	/* Try to acquire a running slot. Returns true if the guard is still
	 * alive and the slot was acquired. Must be paired with Release().
	 *
	 * Internal protocol (TOCTOU-safe):
	 *   1. Fast-path: check alive_ without the mutex.
	 *   2. Take the mutex and re-check alive_.
	 *   3. If alive, increment pending_count_.
	 *   4. Shutdown must set alive_=false then WaitDrain; WaitDrain takes
	 *      the mutex to observe pending_count_, so any TryAcquire that
	 *      observed alive_=true before Shutdown will have already incremented
	 *      pending_count_ before WaitDrain can observe it. */
	bool TryAcquire();

	/* Release the slot acquired by TryAcquire.
	 * If pending_count_ reaches zero, notifies WaitDrain. */
	void Release();

	/* Block until all acquired callbacks have released.
	 * Must only be called after Shutdown() and not from within a
	 * TryAcquire/Release scope. */
	void WaitDrain();

private:
	std::atomic<bool> alive_{true};
	std::mutex mutex_;
	std::condition_variable cv_;
	int pending_count_ = 0;
};

// PendingRefTracker tracks Lua registry references held by in-flight
// RunInLoop callbacks so they can be safely released during shutdown.
//
// Usage:
//   1. Declare: static PendingRefTracker g_xxx_pending;
//   2. In RunInLoop callback, after TryAcquire:
//        g_xxx_pending.AddRef(msg_ref);
//        // ... Lua operations ...
//        g_xxx_pending.RemoveRef(msg_ref);
//   3. During shutdown, after WaitDrain:
//        g_xxx_pending.UnrefAll(L);
class CLOUD_ENGINE_API PendingRefTracker {
public:
	PendingRefTracker() = default;
	~PendingRefTracker() = default;

	PendingRefTracker(const PendingRefTracker&) = delete;
	PendingRefTracker& operator=(const PendingRefTracker&) = delete;

	/* Add a ref to the tracked set. Safe to call from any thread. */
	void AddRef(int ref);

	/* Remove a ref from the tracked set. Safe to call from any thread. */
	void RemoveRef(int ref);

	/* Release all tracked refs under the mutex. Called during shutdown
	 * after all callbacks have drained. If L is null, clears the set
	 * without calling luaL_unref. */
	void UnrefAll(lua_State* L);

private:
	std::mutex mutex_;
	std::vector<int> pending_refs_;
};

}  // namespace script
}  // namespace engine
