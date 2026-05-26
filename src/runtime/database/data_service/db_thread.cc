#if defined(ENGINE_MONGODB_ENABLED)

#define DATABASE_SERVICE_INTERNAL_ACCESS
#include "runtime/database/data_service/db_thread.h"

#include <chrono>

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"
#include "runtime/config/config.h"
#include "runtime/database/data_service/db_script_vm.h"
#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bson_ext.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_forward.h"
#include "runtime/database/mongo_bind/mongo_bind.h"

namespace engine {

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

std::string SerializeCursor(mongo::MongoCursor* cursor, int32_t skip) {
    std::string result = "[";
    bool first = true;
    while (true) {
        mongo::BsonDocument doc;
        if (!cursor->Next(&doc)) break;
        if (skip > 0) { --skip; continue; }
        if (!first) result += ",";
        result += doc.ToJson();
        first = false;
    }
    result += "]";
    return result;
}

} // namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

DBThread::DBThread(int index, const DbServiceConfig& config)
    : index_(index), config_(config) {}

DBThread::~DBThread() {
    Stop();
}

// ============================================================================
// Logger
// ============================================================================

quill::Logger* DBThread::CreateDbLogger() {
    LogConfig mapped;
    mapped.logger_name = "db_vm_" + std::to_string(index_);
    mapped.log_filename = "";
    mapped.dir = config_.log.dir;
    mapped.level = config_.log.level;
    mapped.rotation_size_mb = config_.log.rotation_size_mb;
    mapped.max_backup_files = config_.log.max_backup_files;
    mapped.rotation_frequency = "";
    mapped.rotation_interval = 1;
    mapped.rotation_time_daily = "00:00";
    return CreateLogger(mapped);
}

// ============================================================================
// Lifecycle
// ============================================================================

bool DBThread::Start(mongo::MongoClientPool& pool) {
    logger_ = CreateDbLogger();
    if (!logger_) {
        std::fprintf(stderr, "DBThread[%d]: failed to create logger\n", index_);
        return false;
    }

    pool_ = &pool;
    running_.store(true, std::memory_order_release);

    try {
        thread_ = std::make_unique<std::thread>(&DBThread::EventLoop, this);
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR(logger_, "DBThread[{}]: failed to start thread: {}", index_, e.what());
        running_.store(false, std::memory_order_release);
        return false;
    }

    return true;
}

void DBThread::Stop() {
    if (!running_.load(std::memory_order_acquire)) return;

    running_.store(false, std::memory_order_release);

    // Wakeup sentinel — unblocks EventLoop from sleep or Pop
    DbRequest wakeup;
    wakeup.operation = DbOperation::kNoOp;
    request_queue_.enqueue(std::move(wakeup));

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();
}

// ============================================================================
// SPSC Queue operations
// ============================================================================

bool DBThread::EnqueueRequest(DbRequest&& req) {
    if (!running_.load(std::memory_order_acquire)) return false;

    size_t sz = request_queue_.size_approx();
    if (static_cast<int>(sz) >= config_.thread_pool.request_queue_size) {
        ENGINE_LOG_WARN(logger_, "DBThread[{}]: request queue full (approx={}, max={})",
                        index_, sz, config_.thread_pool.request_queue_size);
        return false;
    }
    return request_queue_.enqueue(std::move(req));
}

std::unique_ptr<DbResponse> DBThread::DequeueResponse() {
    DbResponse resp;
    if (response_queue_.try_dequeue(resp)) {
        return std::make_unique<DbResponse>(std::move(resp));
    }
    return nullptr;
}

void DBThread::EnqueueResponse(DbResponse&& resp) {
    while (response_queue_.size_approx() >=
           static_cast<size_t>(config_.thread_pool.response_queue_size)) {
        DbResponse dropped;
        response_queue_.try_dequeue(dropped);
        ENGINE_LOG_WARN(logger_, "DBThread[{}]: response queue full, dropped response [id={}]",
                        index_, dropped.request_id);
    }
    response_queue_.enqueue(std::move(resp));
}

// ============================================================================
// EventLoop
// ============================================================================

void DBThread::EventLoop() {
    client_ = pool_->Pop();
    if (!client_) {
        ENGINE_LOG_ERROR(logger_, "DBThread[{}]: pool Pop failed (timeout or pool closed)", index_);
        running_.store(false, std::memory_order_release);
        return;
    }

    try {
        // Register custom ptrs
        script_vm_.RegisterSubsystemObjects(this, client_, pool_);

        // Export API bindings
        ExportDbLog(script_vm_, logger_);
        script::ExportMongo(script_vm_);
        script_vm_.DoString(
            "package.loaded.mongoc = mongoc; "
            "package.loaded.bson   = bson");
        ExportDbRuntime(script_vm_);

        // Set import paths
        script_vm_.SetImportPath(
            config_.script.db_scripts_dir + ";" +
            config_.script.runtime_scripts_dir);

        // Load scripts
        if (config_.script.auto_load) {
            script_vm_.DoDirectory(config_.script.runtime_scripts_dir);
            script_vm_.DoDirectory(config_.script.db_scripts_dir);
        }
        script_vm_.InitScript();

        healthy_.store(true, std::memory_order_release);

        // Main loop
        while (running_.load(std::memory_order_acquire)) {
            try {
                DbRequest req;
                if (request_queue_.try_dequeue(req)) {
                    if (req.operation != DbOperation::kNoOp) {
                        ProcessRequest(req);
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(50000));
                }
            } catch (const std::exception& e) {
                ENGINE_LOG_ERROR(logger_, "DBThread[{}]: exception in event loop: {}",
                                 index_, e.what());
                healthy_.store(false, std::memory_order_release);
                running_.store(false, std::memory_order_release);
                break;
            } catch (...) {
                ENGINE_LOG_ERROR(logger_, "DBThread[{}]: unknown exception in event loop",
                                 index_);
                healthy_.store(false, std::memory_order_release);
                running_.store(false, std::memory_order_release);
                break;
            }
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR(logger_, "DBThread[{}]: init phase exception: {}",
                         index_, e.what());
    } catch (...) {
        ENGINE_LOG_ERROR(logger_, "DBThread[{}]: init phase unknown exception",
                         index_);
    }

    // Cleanup
    healthy_.store(false, std::memory_order_release);
    script_vm_.DestroyScript();
    if (client_) {
        pool_->Push(client_);
        client_ = nullptr;
    }
    running_.store(false, std::memory_order_release);
}

// ============================================================================
// ProcessRequest
// ============================================================================

void DBThread::ProcessRequest(const DbRequest& req) {
    DbResponse resp;
    resp.request_id = req.request_id;

    if (req.operation == DbOperation::kNoOp) return;

    // Input validation for CRUD operations
    if (req.operation != DbOperation::kExecuteScript &&
        req.operation != DbOperation::kCommand) {
        if (req.database.empty() || req.collection.empty()) {
            resp.success = false;
            resp.error_message = "database and collection required for CRUD operations";
            EnqueueResponse(std::move(resp));
            return;
        }
    }

    if (req.operation == DbOperation::kExecuteScript) {
        try {
            resp.success = script_vm_.DoString(req.script, "db_request",
                                               &resp.error_message);
        } catch (const std::exception& e) {
            resp.success = false;
            resp.error_message = e.what();
        }
        EnqueueResponse(std::move(resp));
        return;
    }

    mongo::MongoDatabase*   db   = nullptr;
    mongo::MongoCollection* coll = nullptr;

    try {
        bool need_db   = (req.operation == DbOperation::kCommand);
        bool need_coll = (req.operation != DbOperation::kCommand &&
                          req.operation != DbOperation::kExecuteScript);

        if (need_db && !req.database.empty()) {
            db = client_->GetDatabase(req.database.c_str());
        }
        if (need_coll && !req.collection.empty()) {
            coll = client_->GetCollection(req.database.c_str(), req.collection.c_str());
        }

        // Validate bson_data for operations that require it
        bool need_filter = (req.operation == DbOperation::kFind ||
                            req.operation == DbOperation::kFindOne ||
                            req.operation == DbOperation::kUpdateOne ||
                            req.operation == DbOperation::kUpdateMany ||
                            req.operation == DbOperation::kDeleteOne ||
                            req.operation == DbOperation::kDeleteMany ||
                            req.operation == DbOperation::kCount);
        if (need_filter && req.bson_data.empty()) {
            resp.success = false;
            resp.error_message = "bson_data (filter) required for this operation";
            if (coll) coll->Destroy();
            if (db) db->Destroy();
            EnqueueResponse(std::move(resp));
            return;
        }

        switch (req.operation) {
        case DbOperation::kFind: {
            auto filter = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto* cursor = coll->FindWithOpts(filter, nullptr, nullptr);
            if (req.limit > 0) cursor->SetLimit(req.limit);
            resp.result_data = SerializeCursor(cursor, req.skip);
            cursor->Destroy();
            resp.success = true;
            break;
        }
        case DbOperation::kFindOne: {
            auto filter = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto* cursor = coll->FindWithOpts(filter, nullptr, nullptr);
            cursor->SetLimit(1);
            mongo::BsonDocument doc;
            if (cursor->Next(&doc)) resp.result_data = doc.ToJson();
            cursor->Destroy();
            resp.success = true;
            break;
        }
        case DbOperation::kInsertOne: {
            auto doc = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            mongo::BsonDocument reply;
            mongo::MongoError err;
            resp.success = coll->InsertOne(doc, nullptr, &reply, &err);
            if (resp.success) {
                resp.result_data = reply.ToJson();
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        case DbOperation::kInsertMany: {
            // Parse bson_data as a BSON array, then iterate elements.
            // BSON arrays are documents with integer keys ("0", "1", ...).
            std::vector<mongo::BsonDocument> docs;
            std::vector<const mongo::BsonDocument*> doc_ptrs;

            auto arr = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            mongo::BsonIter iter(arr);
            while (iter.Next()) {
                uint32_t len = 0;
                const uint8_t* data = nullptr;
                iter.AsDocument(&len, &data);
                docs.push_back(mongo::BsonDocument(data, len));
                doc_ptrs.push_back(&docs.back());
            }

            if (!doc_ptrs.empty()) {
                mongo::BsonDocument reply;
                mongo::MongoError err;
                resp.success = coll->InsertMany(doc_ptrs.data(), doc_ptrs.size(),
                                                nullptr, &reply, &err);
                if (resp.success) {
                    resp.result_data = "{\"inserted_count\":" +
                                       std::to_string(doc_ptrs.size()) + "}";
                } else {
                    resp.error_code = err.Code();
                    resp.error_message = err.Message();
                }
            } else {
                resp.success = false;
                resp.error_message = "no documents to insert";
            }
            break;
        }
        case DbOperation::kUpdateOne: {
            auto filter = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto update = mongo::BsonDocument::NewFromJson(
                req.bson_data2.c_str(), req.bson_data2.size());
            mongo::BsonDocument reply;
            mongo::MongoError err;
            resp.success = coll->UpdateOne(filter, update, nullptr, &reply, &err);
            if (resp.success) {
                mongo::BsonIter iter;
                if (iter.InitFind(reply, "nModified")) {
                    resp.affected_count = iter.AsInt64();
                }
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        case DbOperation::kUpdateMany: {
            auto filter = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto update = mongo::BsonDocument::NewFromJson(
                req.bson_data2.c_str(), req.bson_data2.size());
            mongo::BsonDocument reply;
            mongo::MongoError err;
            resp.success = coll->UpdateMany(filter, update, nullptr, &reply, &err);
            if (resp.success) {
                mongo::BsonIter iter;
                if (iter.InitFind(reply, "nModified")) {
                    resp.affected_count = iter.AsInt64();
                }
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        case DbOperation::kDeleteOne: {
            auto selector = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            mongo::BsonDocument reply;
            mongo::MongoError err;
            resp.success = coll->DeleteOne(selector, nullptr, &reply, &err);
            if (resp.success) {
                mongo::BsonIter iter;
                if (iter.InitFind(reply, "n")) {
                    resp.affected_count = iter.AsInt64();
                }
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        case DbOperation::kDeleteMany: {
            auto selector = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            mongo::BsonDocument reply;
            mongo::MongoError err;
            resp.success = coll->DeleteMany(selector, nullptr, &reply, &err);
            if (resp.success) {
                mongo::BsonIter iter;
                if (iter.InitFind(reply, "n")) {
                    resp.affected_count = iter.AsInt64();
                }
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        case DbOperation::kCount: {
            auto filter = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            mongo::BsonDocument reply;
            mongo::MongoError err;
            int64_t count = coll->CountDocuments(filter, nullptr, nullptr,
                                                  &reply, &err);
            if (count >= 0) {
                resp.success = true;
                resp.result_data = "{\"count\":" + std::to_string(count) + "}";
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        case DbOperation::kAggregate: {
            auto pipeline = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            auto* cursor = coll->Aggregate(pipeline, nullptr, nullptr);
            resp.result_data = SerializeCursor(cursor, 0);
            cursor->Destroy();
            resp.success = true;
            break;
        }
        case DbOperation::kCommand: {
            auto command = mongo::BsonDocument::NewFromJson(
                req.bson_data.c_str(), req.bson_data.size());
            mongo::BsonDocument reply;
            mongo::MongoError err;
            resp.success = client_->CommandSimple(req.database.c_str(), command,
                                                  nullptr, &reply, &err);
            if (resp.success) {
                resp.result_data = reply.ToJson();
            } else {
                resp.error_code = err.Code();
                resp.error_message = err.Message();
            }
            break;
        }
        default:
            resp.success = false;
            resp.error_message = "unsupported operation";
            break;
        }
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error_message = e.what();
    }

    if (coll) coll->Destroy();
    if (db)   db->Destroy();

    EnqueueResponse(std::move(resp));
}

} // namespace engine

#endif // ENGINE_MONGODB_ENABLED
