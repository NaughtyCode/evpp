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
    if (this != &other) impl_ = std::move(other.impl_);
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
    if (!impl_ || !impl_->cursor || !error) return false;
    return mongoc_cursor_error(impl_->cursor,
        static_cast<bson_error_t*>(error->RawError()));
}

void MongoCursor::SetBatchSize(uint32_t batch_size) {
    if (impl_ && impl_->cursor) {
        mongoc_cursor_set_batch_size(impl_->cursor, batch_size);
    }
}

void MongoCursor::SetCursor(void* cursor) {
    if (impl_ && impl_->cursor) {
        mongoc_cursor_destroy(impl_->cursor);
    }
    impl_->cursor = static_cast<mongoc_cursor_t*>(cursor);
}

} // namespace mongo
} // namespace engine
