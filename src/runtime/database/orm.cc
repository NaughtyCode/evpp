#include "runtime/database/orm.h"

#include "runtime/core/log/log.h"
#include "runtime/database/data_service/database_service.h"
#include "runtime/database/data_service/db_request.h"

using engine::DatabaseService;

namespace engine {
namespace database {

OrmSession& OrmSession::Instance() {
	static OrmSession instance;
	return instance;
}

void OrmSession::RegisterSchema(const CollectionSchema& schema) {
	schemas_[schema.collection_name] = schema;
	if (caches_.find(schema.collection_name) == caches_.end()) {
		caches_[schema.collection_name] =
			std::make_unique<EntityCache<std::string>>(10000);
	}
}

const CollectionSchema* OrmSession::GetSchema(const std::string& collection) const {
	auto it = schemas_.find(collection);
	if (it == schemas_.end()) return nullptr;
	return &it->second;
}

CollectionSchema* OrmSession::GetMutableSchema(const std::string& collection) {
	auto it = schemas_.find(collection);
	if (it == schemas_.end()) return nullptr;
	return &it->second;
}

std::optional<std::string> OrmSession::FindById(const std::string& collection,
												  const std::string& id) {
	// Check cache first
	auto* cache = GetCache(collection);
	if (cache) {
		auto cached = cache->Get(id);
		if (cached) return cached;
	}

	// Build query
	DbRequest req;
	req.request_id = 0;  // synchronous-ish: poll
	req.operation = DbOperation::kFindOne;
	req.database = "game";  // configurable?
	req.collection = collection;
	req.bson_data = "{\"_id\":\"" + id + "\"}";

	DatabaseService::Instance().SendRequest(std::move(req));

	// Poll for response (simplified — real impl would use callbacks)
	// For now, this is a best-effort DB fetch; caching is the primary gain.
	auto* logger = GetLogger();
	ENGINE_LOG_DEBUG(logger, "ORM FindById: collection=[{}] id=[{}]", collection, id);

	return std::nullopt;  // async: result comes via SPSC later
}

std::vector<std::string> OrmSession::Find(const std::string& collection,
										   const Query& query,
										   const FindOptions& options) {
	std::vector<std::string> results;

	// Build filter JSON from query map
	std::string filter = "{";
	bool first = true;
	for (const auto& [key, value] : query) {
		if (!first) filter += ",";
		filter += "\"" + key + "\":\"" + value + "\"";
		first = false;
	}
	filter += "}";

	DbRequest req;
	req.request_id = 0;
	req.operation = DbOperation::kFind;
	req.database = "game";
	req.collection = collection;
	req.bson_data = std::move(filter);
	req.limit = options.limit;
	req.skip = options.skip;

	DatabaseService::Instance().SendRequest(std::move(req));

	return results;
}

bool OrmSession::Insert(const std::string& collection, const std::string& doc_json) {
	DbRequest req;
	req.request_id = 0;
	req.operation = DbOperation::kInsertOne;
	req.database = "game";
	req.collection = collection;
	req.bson_data = doc_json;

	DatabaseService::Instance().SendRequest(std::move(req));
	return true;  // fire-and-forget; result comes via SPSC
}

bool OrmSession::Update(const std::string& collection, const std::string& id,
						 const std::string& update_json) {
	DbRequest req;
	req.request_id = 0;
	req.operation = DbOperation::kUpdateOne;
	req.database = "game";
	req.collection = collection;
	req.bson_data = "{\"_id\":\"" + id + "\"}";
	req.bson_data2 = update_json;

	DatabaseService::Instance().SendRequest(std::move(req));

	// Invalidate cache
	auto* cache = GetCache(collection);
	if (cache) cache->Invalidate(id);

	return true;
}

bool OrmSession::DeleteById(const std::string& collection, const std::string& id) {
	DbRequest req;
	req.request_id = 0;
	req.operation = DbOperation::kDeleteOne;
	req.database = "game";
	req.collection = collection;
	req.bson_data = "{\"_id\":\"" + id + "\"}";

	DatabaseService::Instance().SendRequest(std::move(req));

	// Invalidate cache
	auto* cache = GetCache(collection);
	if (cache) cache->Invalidate(id);

	return true;
}

EntityCache<std::string>* OrmSession::GetCache(const std::string& collection) {
	auto it = caches_.find(collection);
	if (it == caches_.end()) return nullptr;
	return it->second.get();
}

size_t OrmSession::TotalCacheHits() const {
	size_t total = 0;
	for (const auto& [name, cache] : caches_) {
		total += cache->HitCount();
	}
	return total;
}

size_t OrmSession::TotalCacheMisses() const {
	size_t total = 0;
	for (const auto& [name, cache] : caches_) {
		total += cache->MissCount();
	}
	return total;
}

double OrmSession::GlobalHitRate() const {
	size_t hits = TotalCacheHits();
	size_t misses = TotalCacheMisses();
	size_t total = hits + misses;
	return total > 0 ? static_cast<double>(hits) / static_cast<double>(total) : 0.0;
}

void OrmSession::ClearAllCaches() {
	for (auto& [name, cache] : caches_) {
		cache->Clear();
	}
}

}  // namespace database
}  // namespace engine
