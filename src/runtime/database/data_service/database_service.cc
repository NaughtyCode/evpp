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

// ============================================================================
// Singleton
// ============================================================================

DatabaseService::DatabaseService() = default;

DatabaseService& DatabaseService::Instance() {
    static DatabaseService instance;
    return instance;
}

DatabaseService::~DatabaseService() = default;

// ============================================================================
// Initialize / Shutdown
// ============================================================================

bool DatabaseService::Initialize(const DbServiceConfig& config,
                                  const mongo::MongoUri& uri) {
    if (running_.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "DatabaseService: already initialized\n");
        return false;
    }

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

    // Create pool
    pool_.reset(mongo::MongoClientPool::New(uri));
    if (!pool_) {
        std::fprintf(stderr, "DatabaseService: failed to create MongoClientPool\n");
        return false;
    }
    pool_->SetMaxSize(static_cast<uint32_t>(config_.connection_pool.max_pool_size));

    // Create and start DBThreads
    int n = config_.thread_pool.thread_count;
    threads_.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto thread = std::make_unique<DBThread>(i, config_);
        if (!thread->Start(*pool_)) {
            std::fprintf(stderr, "DatabaseService: failed to start DBThread[%d]\n", i);
            // Stop already-started threads
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

void DatabaseService::Shutdown() {
    if (!running_.load(std::memory_order_acquire)) return;

    running_.store(false, std::memory_order_release);

    // Stop all threads
    for (auto& t : threads_) {
        t->Stop();
    }

    // Drain remaining responses
    for (auto& t : threads_) {
        while (t->DequeueResponse()) {}
    }

    // Destroy pool after all threads have joined
    if (pool_) {
        pool_->Destroy();
        pool_.reset();
    }

    threads_.clear();

    ENGINE_LOG_INFO(GetLogger(), "DatabaseService: shutdown complete");
}

// ============================================================================
// SendRequest / PollResponse
// ============================================================================

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

// ============================================================================
// Status queries
// ============================================================================

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
