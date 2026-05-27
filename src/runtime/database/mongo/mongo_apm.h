#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <functional>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// APM Event Types — read-only accessors for command/SDAM events
// ═══════════════════════════════════════════════════════════════════════

class ENGINE_API MongoApmCommandStartedEvent {
	public:
	explicit MongoApmCommandStartedEvent(const void* raw_event);
	const void* GetCommand() const;	 // returns const bson_t*
	const char* GetDatabaseName() const;
	const char* GetCommandName() const;
	int64_t GetRequestId() const;
	int64_t GetOperationId() const;
	const void* GetHost() const;  // returns const mongoc_host_list_t*
	uint32_t GetServerId() const;
	const void* GetServiceId() const;  // returns const bson_oid_t*
	int64_t GetServerConnectionIdInt64() const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmCommandSucceededEvent {
	public:
	explicit MongoApmCommandSucceededEvent(const void* raw_event);
	int64_t GetDuration() const;
	const void* GetReply() const;  // returns const bson_t*
	const char* GetCommandName() const;
	const char* GetDatabaseName() const;
	int64_t GetRequestId() const;
	int64_t GetOperationId() const;
	const void* GetHost() const;
	uint32_t GetServerId() const;
	const void* GetServiceId() const;
	int64_t GetServerConnectionIdInt64() const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmCommandFailedEvent {
	public:
	explicit MongoApmCommandFailedEvent(const void* raw_event);
	int64_t GetDuration() const;
	const char* GetCommandName() const;
	const char* GetDatabaseName() const;
	void GetError(MongoError* error) const;
	const void* GetReply() const;  // returns const bson_t*
	int64_t GetRequestId() const;
	int64_t GetOperationId() const;
	const void* GetHost() const;
	uint32_t GetServerId() const;
	const void* GetServiceId() const;
	int64_t GetServerConnectionIdInt64() const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmServerChangedEvent {
	public:
	explicit MongoApmServerChangedEvent(const void* raw_event);
	const void* GetHost() const;
	void GetTopologyId(void* oid_out) const;  // bson_oid_t*
	const void* GetPreviousDescription() const;
	const void* GetNewDescription() const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmServerOpeningEvent {
	public:
	explicit MongoApmServerOpeningEvent(const void* raw_event);
	const void* GetHost() const;
	void GetTopologyId(void* oid_out) const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmServerClosedEvent {
	public:
	explicit MongoApmServerClosedEvent(const void* raw_event);
	const void* GetHost() const;
	void GetTopologyId(void* oid_out) const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmTopologyChangedEvent {
	public:
	explicit MongoApmTopologyChangedEvent(const void* raw_event);
	void GetTopologyId(void* oid_out) const;
	const void* GetPreviousDescription() const;
	const void* GetNewDescription() const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmTopologyOpeningEvent {
	public:
	explicit MongoApmTopologyOpeningEvent(const void* raw_event);
	void GetTopologyId(void* oid_out) const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmTopologyClosedEvent {
	public:
	explicit MongoApmTopologyClosedEvent(const void* raw_event);
	void GetTopologyId(void* oid_out) const;
	void* GetContext() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmServerHeartbeatStartedEvent {
	public:
	explicit MongoApmServerHeartbeatStartedEvent(const void* raw_event);
	const void* GetHost() const;
	void* GetContext() const;
	bool GetAwaited() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmServerHeartbeatSucceededEvent {
	public:
	explicit MongoApmServerHeartbeatSucceededEvent(const void* raw_event);
	int64_t GetDuration() const;
	const void* GetReply() const;  // returns const bson_t*
	const void* GetHost() const;
	void* GetContext() const;
	bool GetAwaited() const;

	private:
	const void* event_;
};

class ENGINE_API MongoApmServerHeartbeatFailedEvent {
	public:
	explicit MongoApmServerHeartbeatFailedEvent(const void* raw_event);
	int64_t GetDuration() const;
	void GetError(MongoError* error) const;
	const void* GetHost() const;
	void* GetContext() const;
	bool GetAwaited() const;

	private:
	const void* event_;
};

// ═══════════════════════════════════════════════════════════════════════
// APM Callbacks — configure via C++ std::function or std::move
// ═══════════════════════════════════════════════════════════════════════

using MongoApmCommandStartedCb = std::function<void(const MongoApmCommandStartedEvent&)>;
using MongoApmCommandSucceededCb = std::function<void(const MongoApmCommandSucceededEvent&)>;
using MongoApmCommandFailedCb = std::function<void(const MongoApmCommandFailedEvent&)>;
using MongoApmServerChangedCb = std::function<void(const MongoApmServerChangedEvent&)>;
using MongoApmServerOpeningCb = std::function<void(const MongoApmServerOpeningEvent&)>;
using MongoApmServerClosedCb = std::function<void(const MongoApmServerClosedEvent&)>;
using MongoApmTopologyChangedCb = std::function<void(const MongoApmTopologyChangedEvent&)>;
using MongoApmTopologyOpeningCb = std::function<void(const MongoApmTopologyOpeningEvent&)>;
using MongoApmTopologyClosedCb = std::function<void(const MongoApmTopologyClosedEvent&)>;
using MongoApmServerHeartbeatStartedCb =
	std::function<void(const MongoApmServerHeartbeatStartedEvent&)>;
using MongoApmServerHeartbeatSucceededCb =
	std::function<void(const MongoApmServerHeartbeatSucceededEvent&)>;
using MongoApmServerHeartbeatFailedCb =
	std::function<void(const MongoApmServerHeartbeatFailedEvent&)>;

class ENGINE_API MongoApmCallbacks {
	public:
	MongoApmCallbacks();
	~MongoApmCallbacks();

	MongoApmCallbacks(const MongoApmCallbacks&) = delete;
	MongoApmCallbacks& operator=(const MongoApmCallbacks&) = delete;
	MongoApmCallbacks(MongoApmCallbacks&&) noexcept;
	MongoApmCallbacks& operator=(MongoApmCallbacks&&) noexcept;

	void SetCommandStartedCb(MongoApmCommandStartedCb cb);
	void SetCommandSucceededCb(MongoApmCommandSucceededCb cb);
	void SetCommandFailedCb(MongoApmCommandFailedCb cb);
	void SetServerChangedCb(MongoApmServerChangedCb cb);
	void SetServerOpeningCb(MongoApmServerOpeningCb cb);
	void SetServerClosedCb(MongoApmServerClosedCb cb);
	void SetTopologyChangedCb(MongoApmTopologyChangedCb cb);
	void SetTopologyOpeningCb(MongoApmTopologyOpeningCb cb);
	void SetTopologyClosedCb(MongoApmTopologyClosedCb cb);
	void SetServerHeartbeatStartedCb(MongoApmServerHeartbeatStartedCb cb);
	void SetServerHeartbeatSucceededCb(MongoApmServerHeartbeatSucceededCb cb);
	void SetServerHeartbeatFailedCb(MongoApmServerHeartbeatFailedCb cb);

	void* Raw();  // returns mongoc_apm_callbacks_t*
	void* RawContext();	 // returns context pointer for SetApmCallbacks

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace mongo
}  // namespace engine

#endif
