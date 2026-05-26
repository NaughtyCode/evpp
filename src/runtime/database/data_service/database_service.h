#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

#include <atomic>
#include <memory>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/database/data_service/db_request.h"
#include "runtime/database/data_service/db_service_config.h"

namespace engine {
namespace mongo {
class MongoClientPool;
class MongoUri;
}

class DBThread;

class ENGINE_API DatabaseService {
public:
    static DatabaseService& Instance();

    ~DatabaseService();

    DatabaseService(const DatabaseService&) = delete;
    DatabaseService& operator=(const DatabaseService&) = delete;

    bool Initialize(const DbServiceConfig& config, const mongo::MongoUri& uri);
    void Shutdown();

    bool SendRequest(DbRequest&& request);
    std::unique_ptr<DbResponse> PollResponse();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    bool IsHealthy() const;
    int  GetThreadCount() const;
    const DbServiceConfig& GetConfig() const { return config_; }

private:
    DatabaseService();

    int NextThreadIndex();

    std::atomic<uint64_t> next_thread_{0};

    // pool_ must be declared BEFORE threads_ so it is destroyed AFTER threads_
    // in reverse declaration order (C++ member destruction is LIFO).
    std::unique_ptr<mongo::MongoClientPool> pool_;
    std::vector<std::unique_ptr<DBThread>> threads_;

    DbServiceConfig config_;
    std::atomic<bool> running_{false};

    int poll_cursor_ = 0;
};

} // namespace engine

#endif // ENGINE_MONGODB_ENABLED
