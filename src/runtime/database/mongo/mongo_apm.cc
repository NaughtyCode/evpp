#include "runtime/database/mongo/mongo_apm.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace mongo {

// ── Helper: store std::function as context, invoke from C callback ────────

template <typename EventWrapper, typename RawEvent>
static inline void InvokeCallback(void* ctx, RawEvent* raw_event) {
    if (!ctx) return;
    auto* fn = static_cast<std::function<void(const EventWrapper&)>*>(ctx);
    EventWrapper wrapper(raw_event);
    (*fn)(wrapper);
}

template <typename EventWrapper, typename RawEvent>
static inline void DeleteContext(void* ctx) {
    delete static_cast<std::function<void(const EventWrapper&)>*>(ctx);
}

// Macro to generate the C→C++ shim for each callback type
#define MAKE_APM_CALLBACK(WrapperType, CbType)                                         \
    static void WrapperType##_shim(const CbType* event) {                              \
        auto* ctx_ptr = static_cast<std::function<void(const WrapperType&)>*>(         \
            const_cast<void*>(                                                         \
                mongoc_apm_##WrapperType##_get_context(                                \
                    reinterpret_cast<const mongoc_apm_##WrapperType##_t*>(event))));   \
        if (ctx_ptr) {                                                                 \
            WrapperType wrapper(event);                                                \
            (*ctx_ptr)(wrapper);                                                       \
        }                                                                              \
    }

// ═══════════════════════════════════════════════════════════════════════
// Event type implementations
// ═══════════════════════════════════════════════════════════════════════

MongoApmCommandStartedEvent::MongoApmCommandStartedEvent(const void* raw_event)
    : event_(raw_event) {}

const void* MongoApmCommandStartedEvent::GetCommand() const {
    return mongoc_apm_command_started_get_command(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
const char* MongoApmCommandStartedEvent::GetDatabaseName() const {
    return mongoc_apm_command_started_get_database_name(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
const char* MongoApmCommandStartedEvent::GetCommandName() const {
    return mongoc_apm_command_started_get_command_name(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
int64_t MongoApmCommandStartedEvent::GetRequestId() const {
    return mongoc_apm_command_started_get_request_id(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
int64_t MongoApmCommandStartedEvent::GetOperationId() const {
    return mongoc_apm_command_started_get_operation_id(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
const void* MongoApmCommandStartedEvent::GetHost() const {
    return mongoc_apm_command_started_get_host(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
uint32_t MongoApmCommandStartedEvent::GetServerId() const {
    return mongoc_apm_command_started_get_server_id(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
const void* MongoApmCommandStartedEvent::GetServiceId() const {
    return mongoc_apm_command_started_get_service_id(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
int64_t MongoApmCommandStartedEvent::GetServerConnectionIdInt64() const {
    return mongoc_apm_command_started_get_server_connection_id_int64(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}
void* MongoApmCommandStartedEvent::GetContext() const {
    return mongoc_apm_command_started_get_context(
        static_cast<const mongoc_apm_command_started_t*>(event_));
}

// ── Command Succeeded ──────────────────────────────────────────────────

MongoApmCommandSucceededEvent::MongoApmCommandSucceededEvent(const void* raw_event)
    : event_(raw_event) {}

int64_t MongoApmCommandSucceededEvent::GetDuration() const {
    return mongoc_apm_command_succeeded_get_duration(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
const void* MongoApmCommandSucceededEvent::GetReply() const {
    return mongoc_apm_command_succeeded_get_reply(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
const char* MongoApmCommandSucceededEvent::GetCommandName() const {
    return mongoc_apm_command_succeeded_get_command_name(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
const char* MongoApmCommandSucceededEvent::GetDatabaseName() const {
    return mongoc_apm_command_succeeded_get_database_name(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
int64_t MongoApmCommandSucceededEvent::GetRequestId() const {
    return mongoc_apm_command_succeeded_get_request_id(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
int64_t MongoApmCommandSucceededEvent::GetOperationId() const {
    return mongoc_apm_command_succeeded_get_operation_id(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
const void* MongoApmCommandSucceededEvent::GetHost() const {
    return mongoc_apm_command_succeeded_get_host(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
uint32_t MongoApmCommandSucceededEvent::GetServerId() const {
    return mongoc_apm_command_succeeded_get_server_id(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
const void* MongoApmCommandSucceededEvent::GetServiceId() const {
    return mongoc_apm_command_succeeded_get_service_id(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
int64_t MongoApmCommandSucceededEvent::GetServerConnectionIdInt64() const {
    return mongoc_apm_command_succeeded_get_server_connection_id_int64(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}
void* MongoApmCommandSucceededEvent::GetContext() const {
    return mongoc_apm_command_succeeded_get_context(
        static_cast<const mongoc_apm_command_succeeded_t*>(event_));
}

// ── Command Failed ─────────────────────────────────────────────────────

MongoApmCommandFailedEvent::MongoApmCommandFailedEvent(const void* raw_event)
    : event_(raw_event) {}

int64_t MongoApmCommandFailedEvent::GetDuration() const {
    return mongoc_apm_command_failed_get_duration(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
const char* MongoApmCommandFailedEvent::GetCommandName() const {
    return mongoc_apm_command_failed_get_command_name(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
const char* MongoApmCommandFailedEvent::GetDatabaseName() const {
    return mongoc_apm_command_failed_get_database_name(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
void MongoApmCommandFailedEvent::GetError(MongoError* error) const {
    mongoc_apm_command_failed_get_error(
        static_cast<const mongoc_apm_command_failed_t*>(event_),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}
const void* MongoApmCommandFailedEvent::GetReply() const {
    return mongoc_apm_command_failed_get_reply(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
int64_t MongoApmCommandFailedEvent::GetRequestId() const {
    return mongoc_apm_command_failed_get_request_id(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
int64_t MongoApmCommandFailedEvent::GetOperationId() const {
    return mongoc_apm_command_failed_get_operation_id(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
const void* MongoApmCommandFailedEvent::GetHost() const {
    return mongoc_apm_command_failed_get_host(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
uint32_t MongoApmCommandFailedEvent::GetServerId() const {
    return mongoc_apm_command_failed_get_server_id(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
const void* MongoApmCommandFailedEvent::GetServiceId() const {
    return mongoc_apm_command_failed_get_service_id(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
int64_t MongoApmCommandFailedEvent::GetServerConnectionIdInt64() const {
    return mongoc_apm_command_failed_get_server_connection_id_int64(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}
void* MongoApmCommandFailedEvent::GetContext() const {
    return mongoc_apm_command_failed_get_context(
        static_cast<const mongoc_apm_command_failed_t*>(event_));
}

// ── Server Changed ─────────────────────────────────────────────────────

MongoApmServerChangedEvent::MongoApmServerChangedEvent(const void* raw_event)
    : event_(raw_event) {}

const void* MongoApmServerChangedEvent::GetHost() const {
    return mongoc_apm_server_changed_get_host(
        static_cast<const mongoc_apm_server_changed_t*>(event_));
}
void MongoApmServerChangedEvent::GetTopologyId(void* oid_out) const {
    mongoc_apm_server_changed_get_topology_id(
        static_cast<const mongoc_apm_server_changed_t*>(event_),
        static_cast<bson_oid_t*>(oid_out));
}
const void* MongoApmServerChangedEvent::GetPreviousDescription() const {
    return mongoc_apm_server_changed_get_previous_description(
        static_cast<const mongoc_apm_server_changed_t*>(event_));
}
const void* MongoApmServerChangedEvent::GetNewDescription() const {
    return mongoc_apm_server_changed_get_new_description(
        static_cast<const mongoc_apm_server_changed_t*>(event_));
}
void* MongoApmServerChangedEvent::GetContext() const {
    return mongoc_apm_server_changed_get_context(
        static_cast<const mongoc_apm_server_changed_t*>(event_));
}

// ── Server Opening ─────────────────────────────────────────────────────

MongoApmServerOpeningEvent::MongoApmServerOpeningEvent(const void* raw_event)
    : event_(raw_event) {}

const void* MongoApmServerOpeningEvent::GetHost() const {
    return mongoc_apm_server_opening_get_host(
        static_cast<const mongoc_apm_server_opening_t*>(event_));
}
void MongoApmServerOpeningEvent::GetTopologyId(void* oid_out) const {
    mongoc_apm_server_opening_get_topology_id(
        static_cast<const mongoc_apm_server_opening_t*>(event_),
        static_cast<bson_oid_t*>(oid_out));
}
void* MongoApmServerOpeningEvent::GetContext() const {
    return mongoc_apm_server_opening_get_context(
        static_cast<const mongoc_apm_server_opening_t*>(event_));
}

// ── Server Closed ──────────────────────────────────────────────────────

MongoApmServerClosedEvent::MongoApmServerClosedEvent(const void* raw_event)
    : event_(raw_event) {}

const void* MongoApmServerClosedEvent::GetHost() const {
    return mongoc_apm_server_closed_get_host(
        static_cast<const mongoc_apm_server_closed_t*>(event_));
}
void MongoApmServerClosedEvent::GetTopologyId(void* oid_out) const {
    mongoc_apm_server_closed_get_topology_id(
        static_cast<const mongoc_apm_server_closed_t*>(event_),
        static_cast<bson_oid_t*>(oid_out));
}
void* MongoApmServerClosedEvent::GetContext() const {
    return mongoc_apm_server_closed_get_context(
        static_cast<const mongoc_apm_server_closed_t*>(event_));
}

// ── Topology Changed ───────────────────────────────────────────────────

MongoApmTopologyChangedEvent::MongoApmTopologyChangedEvent(const void* raw_event)
    : event_(raw_event) {}

void MongoApmTopologyChangedEvent::GetTopologyId(void* oid_out) const {
    mongoc_apm_topology_changed_get_topology_id(
        static_cast<const mongoc_apm_topology_changed_t*>(event_),
        static_cast<bson_oid_t*>(oid_out));
}
const void* MongoApmTopologyChangedEvent::GetPreviousDescription() const {
    return mongoc_apm_topology_changed_get_previous_description(
        static_cast<const mongoc_apm_topology_changed_t*>(event_));
}
const void* MongoApmTopologyChangedEvent::GetNewDescription() const {
    return mongoc_apm_topology_changed_get_new_description(
        static_cast<const mongoc_apm_topology_changed_t*>(event_));
}
void* MongoApmTopologyChangedEvent::GetContext() const {
    return mongoc_apm_topology_changed_get_context(
        static_cast<const mongoc_apm_topology_changed_t*>(event_));
}

// ── Topology Opening ───────────────────────────────────────────────────

MongoApmTopologyOpeningEvent::MongoApmTopologyOpeningEvent(const void* raw_event)
    : event_(raw_event) {}

void MongoApmTopologyOpeningEvent::GetTopologyId(void* oid_out) const {
    mongoc_apm_topology_opening_get_topology_id(
        static_cast<const mongoc_apm_topology_opening_t*>(event_),
        static_cast<bson_oid_t*>(oid_out));
}
void* MongoApmTopologyOpeningEvent::GetContext() const {
    return mongoc_apm_topology_opening_get_context(
        static_cast<const mongoc_apm_topology_opening_t*>(event_));
}

// ── Topology Closed ────────────────────────────────────────────────────

MongoApmTopologyClosedEvent::MongoApmTopologyClosedEvent(const void* raw_event)
    : event_(raw_event) {}

void MongoApmTopologyClosedEvent::GetTopologyId(void* oid_out) const {
    mongoc_apm_topology_closed_get_topology_id(
        static_cast<const mongoc_apm_topology_closed_t*>(event_),
        static_cast<bson_oid_t*>(oid_out));
}
void* MongoApmTopologyClosedEvent::GetContext() const {
    return mongoc_apm_topology_closed_get_context(
        static_cast<const mongoc_apm_topology_closed_t*>(event_));
}

// ── Server Heartbeat Started ───────────────────────────────────────────

MongoApmServerHeartbeatStartedEvent::MongoApmServerHeartbeatStartedEvent(const void* raw_event)
    : event_(raw_event) {}

const void* MongoApmServerHeartbeatStartedEvent::GetHost() const {
    return mongoc_apm_server_heartbeat_started_get_host(
        static_cast<const mongoc_apm_server_heartbeat_started_t*>(event_));
}
void* MongoApmServerHeartbeatStartedEvent::GetContext() const {
    return mongoc_apm_server_heartbeat_started_get_context(
        static_cast<const mongoc_apm_server_heartbeat_started_t*>(event_));
}
bool MongoApmServerHeartbeatStartedEvent::GetAwaited() const {
    return mongoc_apm_server_heartbeat_started_get_awaited(
        static_cast<const mongoc_apm_server_heartbeat_started_t*>(event_));
}

// ── Server Heartbeat Succeeded ─────────────────────────────────────────

MongoApmServerHeartbeatSucceededEvent::MongoApmServerHeartbeatSucceededEvent(const void* raw_event)
    : event_(raw_event) {}

int64_t MongoApmServerHeartbeatSucceededEvent::GetDuration() const {
    return mongoc_apm_server_heartbeat_succeeded_get_duration(
        static_cast<const mongoc_apm_server_heartbeat_succeeded_t*>(event_));
}
const void* MongoApmServerHeartbeatSucceededEvent::GetReply() const {
    return mongoc_apm_server_heartbeat_succeeded_get_reply(
        static_cast<const mongoc_apm_server_heartbeat_succeeded_t*>(event_));
}
const void* MongoApmServerHeartbeatSucceededEvent::GetHost() const {
    return mongoc_apm_server_heartbeat_succeeded_get_host(
        static_cast<const mongoc_apm_server_heartbeat_succeeded_t*>(event_));
}
void* MongoApmServerHeartbeatSucceededEvent::GetContext() const {
    return mongoc_apm_server_heartbeat_succeeded_get_context(
        static_cast<const mongoc_apm_server_heartbeat_succeeded_t*>(event_));
}
bool MongoApmServerHeartbeatSucceededEvent::GetAwaited() const {
    return mongoc_apm_server_heartbeat_succeeded_get_awaited(
        static_cast<const mongoc_apm_server_heartbeat_succeeded_t*>(event_));
}

// ── Server Heartbeat Failed ────────────────────────────────────────────

MongoApmServerHeartbeatFailedEvent::MongoApmServerHeartbeatFailedEvent(const void* raw_event)
    : event_(raw_event) {}

int64_t MongoApmServerHeartbeatFailedEvent::GetDuration() const {
    return mongoc_apm_server_heartbeat_failed_get_duration(
        static_cast<const mongoc_apm_server_heartbeat_failed_t*>(event_));
}
void MongoApmServerHeartbeatFailedEvent::GetError(MongoError* error) const {
    mongoc_apm_server_heartbeat_failed_get_error(
        static_cast<const mongoc_apm_server_heartbeat_failed_t*>(event_),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}
const void* MongoApmServerHeartbeatFailedEvent::GetHost() const {
    return mongoc_apm_server_heartbeat_failed_get_host(
        static_cast<const mongoc_apm_server_heartbeat_failed_t*>(event_));
}
void* MongoApmServerHeartbeatFailedEvent::GetContext() const {
    return mongoc_apm_server_heartbeat_failed_get_context(
        static_cast<const mongoc_apm_server_heartbeat_failed_t*>(event_));
}
bool MongoApmServerHeartbeatFailedEvent::GetAwaited() const {
    return mongoc_apm_server_heartbeat_failed_get_awaited(
        static_cast<const mongoc_apm_server_heartbeat_failed_t*>(event_));
}

// ═══════════════════════════════════════════════════════════════════════
// MongoApmCallbacks
// ═══════════════════════════════════════════════════════════════════════

// Each callback is stored as a heap-allocated std::function managed by
// shared_ptr. The C bridge trampoline invokes the function through the
// stored pointer.
//
// The mongo-c-driver does NOT provide a per-callback context pointer,
// so we use a different approach: we set the C callback and store the
// std::function as a member. Each C callback is a static trampoline
// that looks up the function from a global or the callbacks pointer.
//
// Actually, the mongo-c-driver's apm_callbacks_set_* functions just set
// function pointers. There's no way to pass context per callback.
// However, the mongoc_client_set_apm_callbacks takes a context pointer
// that is stored in each event as `GetContext()`. So we CAN use that.
//
// But mongoc apm events don't have a general context pointer per callback
// type — looking at the API, there's no _set_context or per-callback context.
// The context for apm callbacks must be set at the client/pool level.
//
// Looking more carefully: mongoc_apm_command_started_get_context() returns
// the context that was passed to mongoc_client_set_apm_callbacks(). This
// is a SINGLE context for ALL callbacks. This means we need a struct that
// holds ALL the std::function callbacks, and pass a pointer to that struct
// as the context.

struct MongoApmCallbacks::Impl {
    mongoc_apm_callbacks_t* callbacks = nullptr;

    // Store all callbacks in a context struct (heap-allocated, deleted via destroy)
    struct Context {
        MongoApmCommandStartedCb command_started;
        MongoApmCommandSucceededCb command_succeeded;
        MongoApmCommandFailedCb command_failed;
        MongoApmServerChangedCb server_changed;
        MongoApmServerOpeningCb server_opening;
        MongoApmServerClosedCb server_closed;
        MongoApmTopologyChangedCb topology_changed;
        MongoApmTopologyOpeningCb topology_opening;
        MongoApmTopologyClosedCb topology_closed;
        MongoApmServerHeartbeatStartedCb heartbeat_started;
        MongoApmServerHeartbeatSucceededCb heartbeat_succeeded;
        MongoApmServerHeartbeatFailedCb heartbeat_failed;
    };
    std::shared_ptr<Context> ctx;

    Impl() : ctx(std::make_shared<Context>()) {
        callbacks = mongoc_apm_callbacks_new();
    }
    ~Impl() {
        if (callbacks) mongoc_apm_callbacks_destroy(callbacks);
    }
};

// Trampolines — each converts C event → C++ event and dispatches

#define APM_TRAMPOLINE(name, typename)                                              \
    static void apm_##name##_trampoline(const mongoc_apm_##name##_t* event) {      \
        auto* ctx = static_cast<MongoApmCallbacks::Impl::Context*>(                 \
            mongoc_apm_##name##_get_context(event));                                \
        if (ctx && ctx->name) {                                                     \
            MongoApm##typename##Event wrapper(event);                               \
            ctx->name(wrapper);                                                     \
        }                                                                           \
    }

APM_TRAMPOLINE(command_started, CommandStarted)
APM_TRAMPOLINE(command_succeeded, CommandSucceeded)
APM_TRAMPOLINE(command_failed, CommandFailed)
APM_TRAMPOLINE(server_changed, ServerChanged)
APM_TRAMPOLINE(server_opening, ServerOpening)
APM_TRAMPOLINE(server_closed, ServerClosed)
APM_TRAMPOLINE(topology_changed, TopologyChanged)
APM_TRAMPOLINE(topology_opening, TopologyOpening)
APM_TRAMPOLINE(topology_closed, TopologyClosed)
APM_TRAMPOLINE(server_heartbeat_started, ServerHeartbeatStarted)
APM_TRAMPOLINE(server_heartbeat_succeeded, ServerHeartbeatSucceeded)
APM_TRAMPOLINE(server_heartbeat_failed, ServerHeartbeatFailed)

#undef APM_TRAMPOLINE

MongoApmCallbacks::MongoApmCallbacks() : impl_(std::make_unique<Impl>()) {}

MongoApmCallbacks::~MongoApmCallbacks() = default;

MongoApmCallbacks::MongoApmCallbacks(MongoApmCallbacks&&) noexcept = default;
MongoApmCallbacks& MongoApmCallbacks::operator=(MongoApmCallbacks&&) noexcept = default;

// The Context pointer is passed as the "context" to mongoc_client_set_apm_callbacks.
// Each event's get_context() returns this pointer.
void* MongoApmCallbacks::Raw() {
    return impl_ ? impl_->callbacks : nullptr;
}

void* MongoApmCallbacks::RawContext() {
    return impl_ ? impl_->ctx.get() : nullptr;
}

// ── Callback setters ────────────────────────────────────────────────────────

#define APM_SET_CB(name, typename)                                                  \
    void MongoApmCallbacks::Set##typename##Cb(MongoApm##typename##Cb cb) {          \
        if (impl_) {                                                                \
            impl_->ctx->name = std::move(cb);                                       \
            mongoc_apm_set_##name##_cb(impl_->callbacks,                            \
                impl_->ctx->name ? apm_##name##_trampoline : nullptr);              \
        }                                                                           \
    }

APM_SET_CB(command_started, CommandStarted)
APM_SET_CB(command_succeeded, CommandSucceeded)
APM_SET_CB(command_failed, CommandFailed)
APM_SET_CB(server_changed, ServerChanged)
APM_SET_CB(server_opening, ServerOpening)
APM_SET_CB(server_closed, ServerClosed)
APM_SET_CB(topology_changed, TopologyChanged)
APM_SET_CB(topology_opening, TopologyOpening)
APM_SET_CB(topology_closed, TopologyClosed)
APM_SET_CB(server_heartbeat_started, ServerHeartbeatStarted)
APM_SET_CB(server_heartbeat_succeeded, ServerHeartbeatSucceeded)
APM_SET_CB(server_heartbeat_failed, ServerHeartbeatFailed)

#undef APM_SET_CB

} // namespace mongo
} // namespace engine
