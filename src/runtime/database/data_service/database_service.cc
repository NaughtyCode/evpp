#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/data_service/database_service.h"

#include <cstdio>

#define DATABASE_SERVICE_INTERNAL_ACCESS
#include "runtime/database/data_service/db_thread.h"

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"
#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/database/mongo/mongo_uri.h"

namespace engine {

// ══════════════════════════════════════════════════════════════════════════════
// Singleton
// ══════════════════════════════════════════════════════════════════════════════

DatabaseService::DatabaseService() = default;

DatabaseService& DatabaseService::Instance() {
    static DatabaseService instance;
    return instance;
}

DatabaseService::~DatabaseService() = default;

// ══════════════════════════════════════════════════════════════════════════════
// Initialize (MT exclusive, design §4)
// ══════════════════════════════════════════════════════════════════════════════
//
// Copies the URI, injects waitQueueTimeoutMS from config, creates the
// MongoClientPool, then starts N DBThreads.
//
// waitQueueTimeoutMS is injected internally via uri.Copy() + SetOptionAsInt32
// BEFORE pool creation (design §13). This bounds DBThread Pop() blocking time
// and prevents Stop() from hanging during Shutdown.

bool DatabaseService::Initialize(const DbServiceConfig& config,
                                  const mongo::MongoUri& uri) {
    if (running_.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "DatabaseService: already initialized\n");
        return false;
    }

    // ── Validation (design §4: Initialize preconditions) ───────────────
    if (config.thread_pool.thread_count < 1) {
        std::fprintf(stderr, "DatabaseService: thread_count must be >= 1 (got %d)\n",
                     config.thread_pool.thread_count);
        return false;
    }

    if (config.connection_pool.max_pool_size < config.thread_pool.thread_count) {
        std::fprintf(stderr, "DatabaseService: max_pool_size (%d) < thread_count (%d), "
                     "Pop() may timeout\n",
                     config.connection_pool.max_pool_size,
                     config.thread_pool.thread_count);
    }

    config_ = config;

    // ── Prepare URI with waitQueueTimeoutMS (design §13) ───────────────
    //
    // Copy the caller-provided URI and inject waitQueueTimeoutMS from config.
    // This bounds how long each DBThread blocks in pool_->Pop() — without it,
    // Pop() waits indefinitely and Stop() can hang during Shutdown.
    //
    // Must be done BEFORE pool creation because SetOptionAsInt32 has no
    // effect once the pool's internal client_initialized flag is set.
    auto pooled_uri = uri.Copy();
    if (config_.connection_pool.wait_queue_timeout_ms > 0) {
        pooled_uri.SetOptionAsInt32(
            "waitQueueTimeoutMS",
            static_cast<int32_t>(config_.connection_pool.wait_queue_timeout_ms));
    }

    // ── Create MongoClientPool ─────────────────────────────────────────
    // pool_ uses unique_ptr; explicit Destroy() before reset() in Shutdown
    // ensures mongoc_client_pool_destroy() runs before the C++ wrapper is freed.
    // SetMaxSize must be called BEFORE any Pop() — design §3.5 constraint.
    pool_.reset(mongo::MongoClientPool::New(pooled_uri));
    if (!pool_) {
        std::fprintf(stderr, "DatabaseService: failed to create MongoClientPool\n");
        return false;
    }
    pool_->SetMaxSize(static_cast<uint32_t>(config_.connection_pool.max_pool_size));

    // ── Create and start DBThreads ─────────────────────────────────────
    int n = config_.thread_pool.thread_count;
    threads_.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto thread = std::make_unique<DBThread>(i, config_);
        if (!thread->Start(*pool_)) {
            std::fprintf(stderr, "DatabaseService: failed to start DBThread[%d]\n", i);
            // Rollback: stop already-started threads, destroy pool.
            for (int j = 0; j < i; ++j) {
                threads_[j]->Stop();
            }
            threads_.clear();
            pool_->Destroy();
            pool_.reset();
            return false;
        }
        threads_.push_back(std::move(thread));
    }

    running_.store(true, std::memory_order_release);
    ENGINE_LOG_INFO(GetLogger(), "DatabaseService: initialized with {} threads", n);
    return true;
}

// ══════════════════════════════════════════════════════════════════════════════
// Shutdown (MT exclusive, design §4)
// ══════════════════════════════════════════════════════════════════════════════
//
// Order matters (matching design §10):
//   1. Set running_ = false (prevents new SendRequest).
//   2. Stop all DBThreads (running_=false, kNoOp wakeup, join).
//   3. Drain remaining responses from all queues.
//   4. Destroy pool (non-thread-safe — must happen after all joins).
//   5. Clear thread vector.

void DatabaseService::Shutdown() {
    if (!running_.load(std::memory_order_acquire)) return;

    running_.store(false, std::memory_order_release);

    // Stop all threads — each Stop() sets running_=false on the thread,
    // enqueues a kNoOp wakeup, and joins.
    for (auto& t : threads_) {
        t->Stop();
    }

    // Drain remaining responses. After all threads are joined, no new
    // responses can be produced. Any queued responses are discarded —
    // the module provides at-most-once semantics.
    for (auto& t : threads_) {
        while (t->DequeueResponse()) {}
    }

    // Destroy pool. pool_->Destroy() calls mongoc_client_pool_destroy()
    // which is NOT thread-safe — all threads must be joined first.
    if (pool_) {
        pool_->Destroy();
        pool_.reset();
    }

    threads_.clear();

    ENGINE_LOG_INFO(GetLogger(), "DatabaseService: shutdown complete");
}

// ══════════════════════════════════════════════════════════════════════════════
// SendRequest — round-robin dispatch (design §11)
// ══════════════════════════════════════════════════════════════════════════════
//
// Each request is routed to exactly one DBThread via atomic round-robin.
// The request is moved into the thread's SPSC queue. If the queue is full
// (back-pressure), the request is rejected and the caller should retry.
//
// Round-robin provides uniform load distribution for the common case of
// homogeneous requests. Future extension: hash-based routing by database/
// collection for cache locality.

int DatabaseService::NextThreadIndex() {
    return next_thread_.fetch_add(1, std::memory_order_relaxed) %
           static_cast<int>(threads_.size());
}

bool DatabaseService::SendRequest(DbRequest&& request) {
    if (!running_.load(std::memory_order_acquire)) return false;
    if (threads_.empty()) return false;

    int idx = NextThreadIndex();
    return threads_[idx]->EnqueueRequest(std::move(request));
}

// ══════════════════════════════════════════════════════════════════════════════
// PollResponse — round-robin scan of all response queues (design §4.1)
// ══════════════════════════════════════════════════════════════════════════════
//
// Scans threads in round-robin order starting from poll_cursor_, returns
// the first non-empty response. After a successful dequeue, poll_cursor_
// advances to the next thread (fairness — no thread is starved).
//
// MT is the sole consumer of all response queues — SPSC per thread,
// no mutex needed.

std::unique_ptr<DbResponse> DatabaseService::PollResponse() {
    int n = static_cast<int>(threads_.size());
    if (n == 0) return nullptr;

    for (int i = 0; i < n; ++i) {
        int idx = (poll_cursor_ + i) % n;
        auto resp = threads_[idx]->DequeueResponse();
        if (resp) {
            poll_cursor_ = (idx + 1) % n;
            return resp;
        }
    }
    return nullptr;
}

// ══════════════════════════════════════════════════════════════════════════════
// Status queries
// ══════════════════════════════════════════════════════════════════════════════

bool DatabaseService::IsHealthy() const {
    for (const auto& t : threads_) {
        if (!t->IsHealthy()) return false;
    }
    return true;
}

int DatabaseService::GetThreadCount() const {
    return static_cast<int>(threads_.size());
}

} // namespace engine

#endif // ENGINE_MONGODB_ENABLED
