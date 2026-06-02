#include "runtime/script/net_lifetime.h"

#include <algorithm>

extern "C" {
#include "lauxlib.h"
}

#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace script {

bool NetAliveGuard::TryAcquire() {
	ENGINE_PROFILE_SCOPE("engine.script", "NetGuardAcquire");

	if (!alive_.load(std::memory_order_acquire)) return false;

	std::lock_guard<std::mutex> lock(mutex_);
	if (!alive_.load(std::memory_order_acquire)) return false;
	pending_count_++;
	return true;
}

void NetAliveGuard::Release() {
	ENGINE_PROFILE_SCOPE("engine.script", "NetGuardRelease");

	std::lock_guard<std::mutex> lock(mutex_);
	pending_count_--;
	if (pending_count_ == 0) {
		cv_.notify_all();
	}
}

void NetAliveGuard::WaitDrain() {
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [this] { return pending_count_ == 0; });
}

void NetAliveGuard::Reset() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (pending_count_ == 0) {
		alive_.store(true, std::memory_order_release);
	}
}

void PendingRefTracker::AddRef(int ref) {
	if (ref == LUA_NOREF) return;
	std::lock_guard<std::mutex> lock(mutex_);
	pending_refs_.push_back(ref);
}

void PendingRefTracker::RemoveRef(int ref) {
	if (ref == LUA_NOREF) return;
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = std::find(pending_refs_.begin(), pending_refs_.end(), ref);
	if (it != pending_refs_.end()) {
		pending_refs_.erase(it);
	}
}

void PendingRefTracker::UnrefAll(lua_State* L) {
	std::vector<int> refs;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		refs = std::move(pending_refs_);
	}
	if (L) {
		for (int ref : refs) {
			if (ref != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, ref);
			}
		}
	}
}

}  // namespace script
}  // namespace engine
