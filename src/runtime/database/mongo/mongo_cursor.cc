#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_cursor.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace mongo {

struct MongoCursor::Impl {
    mongoc_cursor_t* cursor = nullptr;
};

MongoCursor::MongoCursor() : impl_(std::make_unique<Impl>()) {}

MongoCursor::~MongoCursor() {
    Destroy();
}

MongoCursor::MongoCursor(MongoCursor&& other) noexcept : impl_(std::move(other.impl_)) {}

MongoCursor& MongoCursor::operator=(MongoCursor&& other) noexcept {
    if (this != &other) {
        Destroy();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void MongoCursor::Destroy() {
    if (impl_ && impl_->cursor) {
        mongoc_cursor_destroy(impl_->cursor);
        impl_->cursor = nullptr;
    }
}

bool MongoCursor::Next(BsonDocument* out) {
    if (!impl_ || !impl_->cursor) return false;
    const bson_t* doc = nullptr;
    bool ok = mongoc_cursor_next(impl_->cursor, &doc);
    if (ok && doc && out) {
        bson_destroy(static_cast<bson_t*>(out->RawBson()));
        bson_copy_to(doc, static_cast<bson_t*>(out->RawBson()));
    }
    return ok;
}

bool MongoCursor::HasError(MongoError* error) const {
    if (!impl_ || !impl_->cursor) return false;
    return mongoc_cursor_error(impl_->cursor,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void MongoCursor::SetBatchSize(uint32_t batch_size) {
    if (impl_ && impl_->cursor) {
        mongoc_cursor_set_batch_size(impl_->cursor, batch_size);
    }
}

void MongoCursor::SetCursor(void* cursor) {
    if (!impl_) return;
    if (impl_->cursor) {
        mongoc_cursor_destroy(impl_->cursor);
    }
    impl_->cursor = static_cast<mongoc_cursor_t*>(cursor);
}

const void* MongoCursor::Current() const {
    if (!impl_ || !impl_->cursor) return nullptr;
    return mongoc_cursor_current(impl_->cursor);
}

bool MongoCursor::More() {
    if (!impl_ || !impl_->cursor) return false;
    return mongoc_cursor_more(impl_->cursor);
}

bool MongoCursor::ErrorDocument(MongoError* error, const void** doc) const {
    if (!impl_ || !impl_->cursor) return false;
    return mongoc_cursor_error_document(impl_->cursor,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr,
        reinterpret_cast<const bson_t**>(doc));
}

MongoCursor* MongoCursor::Clone() const {
    if (!impl_ || !impl_->cursor) return nullptr;
    mongoc_cursor_t* cloned = mongoc_cursor_clone(impl_->cursor);
    if (!cloned) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cloned);
    return result;
}

uint32_t MongoCursor::GetBatchSize() const {
    return impl_ && impl_->cursor ? mongoc_cursor_get_batch_size(impl_->cursor) : 0;
}

void MongoCursor::SetLimit(int64_t limit) {
    if (impl_ && impl_->cursor)
        mongoc_cursor_set_limit(impl_->cursor, limit);
}

int64_t MongoCursor::GetLimit() const {
    return impl_ && impl_->cursor ? mongoc_cursor_get_limit(impl_->cursor) : 0;
}

int64_t MongoCursor::GetId() const {
    return impl_ && impl_->cursor ? mongoc_cursor_get_id(impl_->cursor) : 0;
}

uint32_t MongoCursor::GetServerId() const {
    return impl_ && impl_->cursor ? mongoc_cursor_get_server_id(impl_->cursor) : 0;
}

void MongoCursor::SetServerId(uint32_t server_id) {
    if (impl_ && impl_->cursor)
        mongoc_cursor_set_server_id(impl_->cursor, server_id);
}

MongoCursor* MongoCursor::NewFromCommandReplyWithOpts(void* client,
    const BsonDocument& reply, const BsonDocument* opts) {
    // Pass a copy of reply; the C API takes ownership and destroys it.
    mongoc_cursor_t* cursor = mongoc_cursor_new_from_command_reply_with_opts(
        static_cast<mongoc_client_t*>(client),
        bson_copy(static_cast<const bson_t*>(reply.RawBson())),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

void MongoCursor::GetHost(void* host_out) const {
    if (impl_ && impl_->cursor && host_out)
        mongoc_cursor_get_host(impl_->cursor, static_cast<mongoc_host_list_t*>(host_out));
}

void MongoCursor::SetMaxAwaitTimeMs(uint32_t max_await_ms) {
    if (impl_ && impl_->cursor)
        mongoc_cursor_set_max_await_time_ms(impl_->cursor, max_await_ms);
}

uint32_t MongoCursor::GetMaxAwaitTimeMs() const {
    return impl_ && impl_->cursor ? mongoc_cursor_get_max_await_time_ms(impl_->cursor) : 0;
}

} // namespace mongo
} // namespace engine

#endif
