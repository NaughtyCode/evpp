#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine {

struct DbConfig {
	std::string backend = "mongodb";
	std::string uri;
	std::string db_name;
	std::string path;
	int pool_size = 4;
};

struct Query {
	std::string filter;
	std::string sort;
	int limit = 0;
	int skip = 0;
};

struct Document {
	std::string json;
};

struct DbResult {
	bool success = false;
	uint32_t error_code = 0;
	std::string error_message;
	std::string data;
	int64_t affected_count = 0;
};

class IDatabaseCursor {
	public:
	virtual ~IDatabaseCursor() = default;
	virtual bool Next(Document* doc) = 0;
	virtual bool More() const = 0;
};

class IDatabaseBackend {
	public:
	virtual ~IDatabaseBackend() = default;
	virtual bool Initialize(const DbConfig& config) = 0;
	virtual void Shutdown() = 0;

	virtual DbResult Find(const std::string& collection,
						  const Query& query) = 0;
	virtual DbResult Insert(const std::string& collection,
							const Document& doc) = 0;
	virtual DbResult Update(const std::string& collection,
							const Query& query,
							const Document& update) = 0;
	virtual DbResult Delete(const std::string& collection,
							const Query& query,
							bool delete_one) = 0;
	virtual DbResult Count(const std::string& collection,
						   const Query& query) = 0;
	virtual DbResult Aggregate(const std::string& collection,
							   const std::vector<Document>& pipeline) = 0;

	virtual std::unique_ptr<IDatabaseCursor> FindCursor(
		const std::string& collection, const Query& query) = 0;
	virtual bool IsHealthy() = 0;
};

// Factory: create the backend specified in config.
std::unique_ptr<IDatabaseBackend> CreateDatabaseBackend(const DbConfig& config);

}  // namespace engine
