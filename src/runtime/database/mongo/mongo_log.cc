#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_log.h"

#include <cstdarg>
#include <memory>
#include <mutex>

#include "runtime/database/mongo/mongo_bson.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// MongoLog
// ═══════════════════════════════════════════════════════════════════════

namespace {

std::mutex g_log_mutex;
MongoLog::LogFunc g_log_handler;
bool g_handler_set = false;

void LogBridge(mongoc_log_level_t level, const char* domain, const char* message, void*) {
	std::lock_guard<std::mutex> lock(g_log_mutex);
	if (g_log_handler) {
		try {
			g_log_handler(static_cast<MongoLogLevel>(level), domain, message);
		} catch (...) {
			// Do not let exceptions unwind through C stack frames
		}
	}
}

}  // namespace

void MongoLog::SetHandler(LogFunc handler) {
	std::lock_guard<std::mutex> lock(g_log_mutex);
	g_log_handler = std::move(handler);
	if (!g_handler_set) {
		mongoc_log_set_handler(LogBridge, nullptr);
		g_handler_set = true;
	}
}

void MongoLog::Log(MongoLogLevel level, const char* domain, const char* format, ...) {
	va_list args;
	va_start(args, format);
	char* msg = bson_strdupv_printf(format, args);
	va_end(args);
	if (msg) {
		mongoc_log(static_cast<mongoc_log_level_t>(level), domain, "%s", msg);
		bson_free(msg);
	}
}

const char* MongoLog::LevelToString(MongoLogLevel level) {
	return mongoc_log_level_str(static_cast<mongoc_log_level_t>(level));
}

void MongoLog::TraceEnable() {
	mongoc_log_trace_enable();
}
void MongoLog::TraceDisable() {
	mongoc_log_trace_disable();
}

void MongoLog::DefaultHandler(MongoLogLevel level, const char* domain, const char* message) {
	mongoc_log_default_handler(static_cast<mongoc_log_level_t>(level), domain, message, nullptr);
}

// ═══════════════════════════════════════════════════════════════════════
// MongoStructuredLogEntry
// ═══════════════════════════════════════════════════════════════════════

MongoStructuredLogEntry::MongoStructuredLogEntry(const void* raw_entry) : entry_(raw_entry) {
}

void MongoStructuredLogEntry::MessageAsBson(BsonDocument* out) const {
	if (out) {
		bson_t* b = mongoc_structured_log_entry_message_as_bson(
			static_cast<const mongoc_structured_log_entry_t*>(entry_));
		if (b) {
			bson_destroy(static_cast<bson_t*>(out->RawBson()));
			bson_copy_to(b, static_cast<bson_t*>(out->RawBson()));
			bson_destroy(b);
		}
	}
}

MongoStructuredLogLevel MongoStructuredLogEntry::GetLevel() const {
	return static_cast<MongoStructuredLogLevel>(mongoc_structured_log_entry_get_level(
		static_cast<const mongoc_structured_log_entry_t*>(entry_)));
}

MongoStructuredLogComponent MongoStructuredLogEntry::GetComponent() const {
	return static_cast<MongoStructuredLogComponent>(mongoc_structured_log_entry_get_component(
		static_cast<const mongoc_structured_log_entry_t*>(entry_)));
}

const char* MongoStructuredLogEntry::GetMessageString() const {
	return mongoc_structured_log_entry_get_message_string(
		static_cast<const mongoc_structured_log_entry_t*>(entry_));
}

// ═══════════════════════════════════════════════════════════════════════
// MongoStructuredLogOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoStructuredLogOpts::Impl {
	mongoc_structured_log_opts_t* opts = nullptr;
	std::function<void(const MongoStructuredLogEntry&)> handler;
};

// Trampoline from C callback to C++ std::function
static void structured_log_trampoline(const mongoc_structured_log_entry_t* entry, void* user_data) {
	auto* handler = static_cast<std::function<void(const MongoStructuredLogEntry&)>*>(user_data);
	if (handler) {
		try {
			MongoStructuredLogEntry wrapper(entry);
			(*handler)(wrapper);
		} catch (...) {
			// Do not let exceptions unwind through C stack frames
		}
	}
}

MongoStructuredLogOpts::MongoStructuredLogOpts() : impl_(std::make_unique<Impl>()) {
	impl_->opts = mongoc_structured_log_opts_new();
}

MongoStructuredLogOpts::~MongoStructuredLogOpts() {
	if (impl_ && impl_->opts) mongoc_structured_log_opts_destroy(impl_->opts);
}

// Move deleted — SetHandler stores &impl_->handler in the C library;
// moving would leave a dangling pointer in the C callback user_data.

void MongoStructuredLogOpts::SetHandler(LogFunc handler) {
	if (!impl_) return;
	impl_->handler = std::move(handler);
	if (impl_->handler) {
		mongoc_structured_log_opts_set_handler(
			impl_->opts, structured_log_trampoline, &impl_->handler);
	} else {
		mongoc_structured_log_opts_set_handler(impl_->opts, nullptr, nullptr);
	}
}

bool MongoStructuredLogOpts::SetMaxLevelForComponent(MongoStructuredLogComponent component,
													 MongoStructuredLogLevel level) {
	return impl_ && impl_->opts &&
		   mongoc_structured_log_opts_set_max_level_for_component(
			   impl_->opts,
			   static_cast<mongoc_structured_log_component_t>(component),
			   static_cast<mongoc_structured_log_level_t>(level));
}

bool MongoStructuredLogOpts::SetMaxLevelForAllComponents(MongoStructuredLogLevel level) {
	return impl_ && impl_->opts &&
		   mongoc_structured_log_opts_set_max_level_for_all_components(
			   impl_->opts, static_cast<mongoc_structured_log_level_t>(level));
}

bool MongoStructuredLogOpts::SetMaxLevelsFromEnv() {
	return impl_ && impl_->opts && mongoc_structured_log_opts_set_max_levels_from_env(impl_->opts);
}

MongoStructuredLogLevel MongoStructuredLogOpts::GetMaxLevelForComponent(
	MongoStructuredLogComponent component) const {
	if (!impl_ || !impl_->opts) return MongoStructuredLogLevel::kTrace;
	return static_cast<MongoStructuredLogLevel>(
		mongoc_structured_log_opts_get_max_level_for_component(
			impl_->opts, static_cast<mongoc_structured_log_component_t>(component)));
}

size_t MongoStructuredLogOpts::GetMaxDocumentLength() const {
	return impl_ && impl_->opts ? mongoc_structured_log_opts_get_max_document_length(impl_->opts)
								: 0;
}

bool MongoStructuredLogOpts::SetMaxDocumentLength(size_t max_len) {
	return impl_ && impl_->opts &&
		   mongoc_structured_log_opts_set_max_document_length(impl_->opts, max_len);
}

bool MongoStructuredLogOpts::SetMaxDocumentLengthFromEnv() {
	return impl_ && impl_->opts &&
		   mongoc_structured_log_opts_set_max_document_length_from_env(impl_->opts);
}

const char* MongoStructuredLogOpts::GetLevelName(MongoStructuredLogLevel level) {
	return mongoc_structured_log_get_level_name(static_cast<mongoc_structured_log_level_t>(level));
}

bool MongoStructuredLogOpts::GetNamedLevel(const char* name, MongoStructuredLogLevel* out) {
	mongoc_structured_log_level_t raw;
	bool ok = mongoc_structured_log_get_named_level(name, &raw);
	if (ok && out) *out = static_cast<MongoStructuredLogLevel>(raw);
	return ok;
}

const char* MongoStructuredLogOpts::GetComponentName(MongoStructuredLogComponent component) {
	return mongoc_structured_log_get_component_name(
		static_cast<mongoc_structured_log_component_t>(component));
}

bool MongoStructuredLogOpts::GetNamedComponent(const char* name, MongoStructuredLogComponent* out) {
	mongoc_structured_log_component_t raw;
	bool ok = mongoc_structured_log_get_named_component(name, &raw);
	if (ok && out) *out = static_cast<MongoStructuredLogComponent>(raw);
	return ok;
}

const void* MongoStructuredLogOpts::Raw() const {
	return impl_ ? impl_->opts : nullptr;
}
void* MongoStructuredLogOpts::Raw() {
	return impl_ ? impl_->opts : nullptr;
}

}  // namespace mongo
}  // namespace engine

#endif
