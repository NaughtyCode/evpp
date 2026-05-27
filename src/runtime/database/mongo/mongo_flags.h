#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Mirrors mongoc_insert_flags_t
enum class MongoInsertFlags : uint32_t {
	kNone = 0,
	kContinueOnError = 1 << 0,
	kNoValidate = 1U << 31,
};

// Mirrors mongoc_update_flags_t
enum class MongoUpdateFlags : uint32_t {
	kNone = 0,
	kUpsert = 1 << 0,
	kMultiUpdate = 1 << 1,
	kNoValidate = 1U << 31,
};

// Mirrors mongoc_remove_flags_t
enum class MongoRemoveFlags : uint32_t {
	kNone = 0,
	kSingleRemove = 1 << 0,
};

// Mirrors mongoc_query_flags_t
enum class MongoQueryFlags : uint32_t {
	kNone = 0,
	kTailableCursor = 1 << 1,
	kSecondaryOk = 1 << 2,
	kOplogReplay = 1 << 3,
	kNoCursorTimeout = 1 << 4,
	kAwaitData = 1 << 5,
	kExhaust = 1 << 6,
	kPartial = 1 << 7,
};

// Mirrors mongoc_opcode_t
enum class MongoOpcode : int {
	kReply = 1,
	kUpdate = 2001,
	kInsert = 2002,
	kQuery = 2004,
	kGetMore = 2005,
	kDelete = 2006,
	kKillCursors = 2007,
	kCompressed = 2012,
	kMsg = 2013,
};

}  // namespace mongo
}  // namespace engine

#endif
