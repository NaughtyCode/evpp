#include "runtime/script/net_lifetime.h"

#include <algorithm>

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

/* ═══════════════════════════════════════════════════════════════════════════
 * NetAliveGuard
 * ═══════════════════════════════════════════════════════════════════════════ */

bool NetAliveGuard::TryAcquire() {
    /* Fast path: check alive_ before taking the mutex.
     * After Shutdown sets alive_=false, almost all TryAcquire calls
     * return here without touching the mutex. */
    if (!alive_.load(std::memory_order_acquire)) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    /* Re-check under the mutex. If Shutdown happened between the fast-path
     * check and acquiring the mutex, alive_ is false now. */
    if (!alive_.load(std::memory_order_acquire)) return false;
    pending_count_++;
    return true;
}

void NetAliveGuard::Release() {
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

/* ═══════════════════════════════════════════════════════════════════════════
 * PendingRefTracker
 * ═══════════════════════════════════════════════════════════════════════════ */

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
