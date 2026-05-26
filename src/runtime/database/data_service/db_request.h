#pragma once

#include <cstdint>
#include <string>

namespace engine {

enum class DbOperation : uint8_t {
    kNoOp = 0,
    kFind,
    kFindOne,
    kInsertOne,
    kInsertMany,
    kUpdateOne,
    kUpdateMany,
    kDeleteOne,
    kDeleteMany,
    kCount,
    kAggregate,
    kCommand,
    kExecuteScript,
};

struct DbRequest {
    uint64_t    request_id = 0;
    DbOperation operation = DbOperation::kNoOp;
    std::string database;
    std::string collection;
    std::string bson_data;
    std::string bson_data2;
    std::string script;
    int32_t     limit = 0;
    int32_t     skip = 0;
};

struct DbResponse {
    uint64_t    request_id = 0;
    bool        success = false;
    uint32_t    error_code = 0;
    std::string error_message;
    std::string result_data;
    int64_t     affected_count = 0;
};

} // namespace engine
