#pragma once

#ifndef DATABASE_SERVICE_INTERNAL_ACCESS
#error "db_thread.h is internal to the database service module. \
Use database_service.h instead. \
If you are writing database-service-internal code, #define \
DATABASE_SERVICE_INTERNAL_ACCESS before including this header."
#endif

#if defined(ENGINE_MONGODB_ENABLED)

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <quill/Logger.h>

#include <concurrentqueue.h>

#include "runtime/database/data_service/db_request.h"
#include "runtime/database/data_service/db_script_vm.h"
#include "runtime/database/data_service/db_service_config.h"

namespace engine {
namespace mongo {
class MongoClient;
class MongoClientPool;
}

class DBThread {
public:
    DBThread(int index, const DbServiceConfig& config);
    ~DBThread();

    DBThread(const DBThread&) = delete;
    DBThread& operator=(const DBThread&) = delete;

    bool Start(mongo::MongoClientPool& pool);
    void Stop();

    bool EnqueueRequest(DbRequest&& req);
    std::unique_ptr<DbResponse> DequeueResponse();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    bool IsHealthy() const { return healthy_.load(std::memory_order_acquire); }
    int  Index() const { return index_; }
    quill::Logger* GetLogger() const { return logger_; }

    DBScriptVM& GetScriptVM() { return script_vm_; }

private:
    quill::Logger* CreateDbLogger();

    void EventLoop();
    void ProcessRequest(const DbRequest& req);
    void EnqueueResponse(DbResponse&& resp);

    int index_;
    DbServiceConfig config_;

    quill::Logger* logger_ = nullptr;

    mongo::MongoClientPool* pool_ = nullptr;
    mongo::MongoClient* client_ = nullptr;

    DBScriptVM script_vm_;

    moodycamel::ConcurrentQueue<DbRequest>  request_queue_;
    moodycamel::ConcurrentQueue<DbResponse> response_queue_;

    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> healthy_{false};
};

} // namespace engine

#endif // ENGINE_MONGODB_ENABLED
