#include "runtime/database/orm.h"

#include <cstring>
#include <sstream>
#include <utility>

#include "runtime/core/log/log.h"
#include "runtime/database/data_service/database_service.h"
#include "runtime/database/data_service/db_request.h"
#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bson_ext.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_oid.h"

using engine::DatabaseService;

namespace engine {
namespace database {

namespace {

std::string JsonEscape(const std::string& value) {
	std::string out;
	out.reserve(value.size() + 8);
	for (char ch : value) {
		switch (ch) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			out += ch;
			break;
		}
	}
	return out;
}

std::string JsonObjectFromMap(const Query& values) {
	std::string json = "{";
	bool first = true;
	for (const auto& [key, value] : values) {
		if (!first) json += ",";
		json += "\"" + JsonEscape(key) + "\":\"" + JsonEscape(value) + "\"";
		first = false;
	}
	json += "}";
	return json;
}

std::optional<std::string> BsonValueToString(const mongo::BsonIter& iter) {
	switch (static_cast<mongo::BsonType>(iter.Type())) {
	case mongo::BsonType::kUtf8: {
		uint32_t len = 0;
		const char* value = iter.AsUtf8(&len);
		if (!value) return std::nullopt;
		return std::string(value, len);
	}
	case mongo::BsonType::kInt32:
		return std::to_string(iter.AsInt32());
	case mongo::BsonType::kInt64:
		return std::to_string(iter.AsInt64());
	case mongo::BsonType::kDouble: {
		std::ostringstream oss;
		oss << iter.AsDouble();
		return oss.str();
	}
	case mongo::BsonType::kBool:
		return iter.AsBool() ? std::string("true") : std::string("false");
	case mongo::BsonType::kOid:
		return iter.AsOid().ToString();
	default:
		return std::nullopt;
	}
}

bool ParseFlatDocument(const mongo::BsonDocument& doc, Query* out) {
	if (!out) return false;

	mongo::BsonIter iter(doc);
	while (iter.Next()) {
		const char* key = iter.Key();
		if (!key) continue;

		auto value = BsonValueToString(iter);
		if (value) {
			(*out)[key] = std::move(*value);
		}
	}
	return true;
}

bool ParseFlatJsonObject(const std::string& json,
						 Query* out,
						 std::string* error_message = nullptr) {
	if (json.empty()) {
		if (error_message) *error_message = "empty JSON document";
		return false;
	}

	mongo::BsonDocument doc;
	mongo::MongoError error;
	if (!doc.InitFromJson(json.c_str(), static_cast<int64_t>(json.size()), &error)) {
		if (error_message) *error_message = error.Message();
		return false;
	}
	return ParseFlatDocument(doc, out);
}

std::optional<std::string> ExtractDocumentId(const std::string& doc_json) {
	Query fields;
	if (!ParseFlatJsonObject(doc_json, &fields)) return std::nullopt;

	auto it = fields.find("id");
	if (it != fields.end() && !it->second.empty()) return it->second;

	it = fields.find("_id");
	if (it != fields.end() && !it->second.empty()) return it->second;

	return std::nullopt;
}

bool DocumentMatchesQuery(const std::string& doc_json, const Query& query) {
	if (query.empty()) return true;

	Query fields;
	if (!ParseFlatJsonObject(doc_json, &fields)) return false;

	for (const auto& [key, value] : query) {
		auto it = fields.find(key);
		if (it == fields.end() || it->second != value) return false;
	}
	return true;
}

bool ExtractUpdateFields(const std::string& update_json,
						 Query* set_fields,
						 std::vector<std::string>* unset_fields,
						 std::string* error_message = nullptr) {
	if (update_json.empty()) {
		if (error_message) *error_message = "empty update document";
		return false;
	}

	mongo::BsonDocument update_doc;
	mongo::MongoError error;
	if (!update_doc.InitFromJson(update_json.c_str(), static_cast<int64_t>(update_json.size()), &error)) {
		if (error_message) *error_message = error.Message();
		return false;
	}

	bool saw_operator = false;
	mongo::BsonIter iter(update_doc);
	while (iter.Next()) {
		const char* key = iter.Key();
		if (!key || key[0] != '$') continue;

		saw_operator = true;
		if ((std::strcmp(key, "$set") == 0 || std::strcmp(key, "$unset") == 0) &&
			iter.Type() == static_cast<int>(mongo::BsonType::kDocument)) {
			uint32_t len = 0;
			const uint8_t* data = nullptr;
			iter.AsDocument(&len, &data);
			mongo::BsonDocument sub_doc(data, len);

			if (std::strcmp(key, "$set") == 0) {
				ParseFlatDocument(sub_doc, set_fields);
			} else if (unset_fields) {
				mongo::BsonIter unset_iter(sub_doc);
				while (unset_iter.Next()) {
					if (const char* unset_key = unset_iter.Key()) {
						unset_fields->push_back(unset_key);
					}
				}
			}
		}
	}

	if (saw_operator) {
		return (set_fields && !set_fields->empty()) ||
			   (unset_fields && !unset_fields->empty());
	}

	return ParseFlatDocument(update_doc, set_fields) && set_fields && !set_fields->empty();
}

std::string BuildIdFilterJson(const std::string& id) {
	const std::string escaped = JsonEscape(id);
	return "{\"$or\":[{\"_id\":\"" + escaped + "\"},{\"id\":\"" + escaped + "\"}]}";
}

std::string NormalizeMongoUpdateJson(const std::string& update_json) {
	mongo::BsonDocument update_doc;
	mongo::MongoError error;
	if (!update_doc.InitFromJson(update_json.c_str(), static_cast<int64_t>(update_json.size()), &error)) {
		return update_json;
	}

	mongo::BsonIter iter(update_doc);
	if (iter.Next()) {
		const char* key = iter.Key();
		if (key && key[0] == '$') return update_json;
	}
	return "{\"$set\":" + update_json + "}";
}

void SendBestEffort(DbRequest&& req) {
	DatabaseService::Instance().SendRequest(std::move(req));
}

}  // namespace

OrmSession& OrmSession::Instance() {
	static OrmSession instance;
	return instance;
}

void OrmSession::RegisterSchema(const CollectionSchema& schema) {
	if (schema.collection_name.empty()) {
		ENGINE_LOG_WARN(GetLogger(), "ORM: attempted to register empty collection schema");
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	schemas_[schema.collection_name] = schema;
	if (caches_.find(schema.collection_name) == caches_.end()) {
		caches_[schema.collection_name] =
			std::make_unique<EntityCache<std::string>>(10000);
	}
	local_store_.try_emplace(schema.collection_name);
}

const CollectionSchema* OrmSession::GetSchema(const std::string& collection) const {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = schemas_.find(collection);
	if (it == schemas_.end()) return nullptr;
	return &it->second;
}

bool OrmSession::IsCollectionRegistered(const std::string& collection) const {
	std::lock_guard<std::mutex> lock(mutex_);
	return schemas_.find(collection) != schemas_.end();
}

void OrmSession::SetDefaultDatabase(std::string database) {
	if (database.empty()) {
		ENGINE_LOG_WARN(GetLogger(), "ORM: ignored empty default database name");
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	default_database_ = std::move(database);
}

std::string OrmSession::GetDefaultDatabase() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return default_database_;
}

CollectionSchema* OrmSession::GetMutableSchema(const std::string& collection) {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = schemas_.find(collection);
	if (it == schemas_.end()) return nullptr;
	return &it->second;
}

uint64_t OrmSession::NextRequestId() {
	return next_request_id_++;
}

std::optional<std::string> OrmSession::FindById(const std::string& collection,
												 const std::string& id) {
	DbRequest req;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (schemas_.find(collection) == schemas_.end()) return std::nullopt;

		auto cache_it = caches_.find(collection);
		if (cache_it != caches_.end()) {
			auto cached = cache_it->second->Get(id);
			if (cached) return cached;
		}

		auto store_it = local_store_.find(collection);
		if (store_it != local_store_.end()) {
			auto doc_it = store_it->second.find(id);
			if (doc_it != store_it->second.end()) {
				if (cache_it != caches_.end()) cache_it->second->Put(id, doc_it->second);
				return doc_it->second;
			}
		}

		req.request_id = NextRequestId();
		req.operation = DbOperation::kFindOne;
		req.database = default_database_;
		req.collection = collection;
		req.bson_data = BuildIdFilterJson(id);
	}

	SendBestEffort(std::move(req));
	ENGINE_LOG_DEBUG(GetLogger(), "ORM FindById miss: collection=[{}] id=[{}]", collection, id);
	return std::nullopt;
}

std::vector<std::string> OrmSession::Find(const std::string& collection,
										  const Query& query,
										  const FindOptions& options) {
	std::vector<std::string> results;
	DbRequest req;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (schemas_.find(collection) == schemas_.end()) return results;

		int32_t skipped = 0;
		auto store_it = local_store_.find(collection);
		if (store_it != local_store_.end()) {
			for (const auto& [id, doc] : store_it->second) {
				if (!DocumentMatchesQuery(doc, query)) continue;
				if (options.skip > 0 && skipped < options.skip) {
					++skipped;
					continue;
				}
				results.push_back(doc);
				if (options.limit > 0 &&
					static_cast<int32_t>(results.size()) >= options.limit) {
					break;
				}
			}
		}

		req.request_id = NextRequestId();
		req.operation = DbOperation::kFind;
		req.database = default_database_;
		req.collection = collection;
		req.bson_data = JsonObjectFromMap(query);
		req.limit = options.limit;
		req.skip = options.skip;
	}

	SendBestEffort(std::move(req));
	return results;
}

bool OrmSession::Insert(const std::string& collection, const std::string& doc_json) {
	DbRequest req;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (schemas_.find(collection) == schemas_.end()) return false;

		Query fields;
		if (!ParseFlatJsonObject(doc_json, &fields)) return false;

		auto id = ExtractDocumentId(doc_json);
		if (id) {
			local_store_[collection][*id] = doc_json;
			auto cache_it = caches_.find(collection);
			if (cache_it != caches_.end()) cache_it->second->Put(*id, doc_json);
		}

		req.request_id = NextRequestId();
		req.operation = DbOperation::kInsertOne;
		req.database = default_database_;
		req.collection = collection;
		req.bson_data = doc_json;
	}

	SendBestEffort(std::move(req));
	return true;
}

bool OrmSession::Update(const std::string& collection,
						const std::string& id,
						const std::string& update_json) {
	DbRequest req;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (schemas_.find(collection) == schemas_.end()) return false;

		auto store_it = local_store_.find(collection);
		if (store_it == local_store_.end()) return false;

		auto doc_it = store_it->second.find(id);
		if (doc_it == store_it->second.end()) return false;

		Query set_fields;
		std::vector<std::string> unset_fields;
		if (!ExtractUpdateFields(update_json, &set_fields, &unset_fields)) return false;

		Query current_fields;
		if (!ParseFlatJsonObject(doc_it->second, &current_fields)) return false;
		for (auto& [key, value] : set_fields) {
			current_fields[key] = std::move(value);
		}
		for (const auto& key : unset_fields) {
			current_fields.erase(key);
		}
		doc_it->second = JsonObjectFromMap(current_fields);

		auto cache_it = caches_.find(collection);
		if (cache_it != caches_.end()) cache_it->second->Put(id, doc_it->second);

		req.request_id = NextRequestId();
		req.operation = DbOperation::kUpdateOne;
		req.database = default_database_;
		req.collection = collection;
		req.bson_data = BuildIdFilterJson(id);
		req.bson_data2 = NormalizeMongoUpdateJson(update_json);
	}

	SendBestEffort(std::move(req));
	return true;
}

bool OrmSession::DeleteById(const std::string& collection, const std::string& id) {
	DbRequest req;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (schemas_.find(collection) == schemas_.end()) return false;

		auto store_it = local_store_.find(collection);
		if (store_it == local_store_.end()) return false;

		auto doc_it = store_it->second.find(id);
		if (doc_it == store_it->second.end()) return false;

		store_it->second.erase(doc_it);
		auto cache_it = caches_.find(collection);
		if (cache_it != caches_.end()) cache_it->second->Invalidate(id);

		req.request_id = NextRequestId();
		req.operation = DbOperation::kDeleteOne;
		req.database = default_database_;
		req.collection = collection;
		req.bson_data = BuildIdFilterJson(id);
	}

	SendBestEffort(std::move(req));
	return true;
}

EntityCache<std::string>* OrmSession::GetCache(const std::string& collection) {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = caches_.find(collection);
	if (it == caches_.end()) return nullptr;
	return it->second.get();
}

size_t OrmSession::TotalCacheHits() const {
	size_t total = 0;
	std::lock_guard<std::mutex> lock(mutex_);
	for (const auto& [name, cache] : caches_) {
		total += cache->HitCount();
	}
	return total;
}

size_t OrmSession::TotalCacheMisses() const {
	size_t total = 0;
	std::lock_guard<std::mutex> lock(mutex_);
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
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [name, cache] : caches_) {
		cache->Clear();
	}
}

}  // namespace database
}  // namespace engine
