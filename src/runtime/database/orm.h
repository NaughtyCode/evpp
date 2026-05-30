#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/database/cache.h"

namespace engine {

struct DbRequest;
struct DbResponse;
class DatabaseService;

namespace database {

enum class FieldType : uint8_t {
	kString,
	kInt,
	kDouble,
	kBool,
	kObject,
	kArray,
};

struct FieldDef {
	std::string name;
	FieldType type = FieldType::kString;
	std::string default_value;
};

struct IndexDef {
	std::vector<std::string> fields;
	bool unique = false;
};

struct CollectionSchema {
	std::string collection_name;
	std::vector<FieldDef> fields;
	std::vector<IndexDef> indexes;
};

// Query object for Find operations.
using Query = std::unordered_map<std::string, std::string>;

struct FindOptions {
	int32_t limit = 0;
	int32_t skip = 0;
};

// ORM session provides typed CRUD operations.
// All operations go through DatabaseService for actual IO
// and are cached via EntityCache.
class CLOUD_ENGINE_API OrmSession {
public:
	static OrmSession& Instance();

	// Register a collection schema.
	void RegisterSchema(const CollectionSchema& schema);

	// Retrieve a schema by collection name.
	const CollectionSchema* GetSchema(const std::string& collection) const;
	bool IsCollectionRegistered(const std::string& collection) const;

	// Database name used by generated DbRequest objects.
	void SetDefaultDatabase(std::string database);
	std::string GetDefaultDatabase() const;

	// Find a single document by string ID.
	// Returns nullopt if not found.
	std::optional<std::string> FindById(const std::string& collection,
										 const std::string& id);

	// Find documents matching a query.
	std::vector<std::string> Find(const std::string& collection,
								   const Query& query,
								   const FindOptions& options = {});

	// Insert a document (JSON string). Returns true on success.
	bool Insert(const std::string& collection, const std::string& doc_json);

	// Update a document by ID. Returns true on success.
	bool Update(const std::string& collection, const std::string& id,
				const std::string& update_json);

	// Delete a document by ID. Returns true on success.
	bool DeleteById(const std::string& collection, const std::string& id);

	// Cache accessor for the given collection.
	EntityCache<std::string>* GetCache(const std::string& collection);

	// Global cache statistics.
	size_t TotalCacheHits() const;
	size_t TotalCacheMisses() const;
	double GlobalHitRate() const;

	// Clear all caches.
	void ClearAllCaches();

private:
	OrmSession() = default;
	OrmSession(const OrmSession&) = delete;
	OrmSession& operator=(const OrmSession&) = delete;
	OrmSession(OrmSession&&) = delete;
	OrmSession& operator=(OrmSession&&) = delete;

	CollectionSchema* GetMutableSchema(const std::string& collection);
	uint64_t NextRequestId();

	mutable std::mutex mutex_;
	std::string default_database_ = "game";
	std::unordered_map<std::string, CollectionSchema> schemas_;
	std::unordered_map<std::string, std::unique_ptr<EntityCache<std::string>>> caches_;
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> local_store_;
	uint64_t next_request_id_ = 1;
};

}  // namespace database
}  // namespace engine
