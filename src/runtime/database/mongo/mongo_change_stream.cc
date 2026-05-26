#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_change_stream.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace mongo {

struct MongoChangeStream::Impl {
    mongoc_change_stream_t* stream = nullptr;
};

MongoChangeStream::MongoChangeStream() : impl_(std::make_unique<Impl>()) {}

MongoChangeStream::~MongoChangeStream() {
    Destroy();
}

MongoChangeStream::MongoChangeStream(MongoChangeStream&& other) noexcept
    : impl_(std::move(other.impl_)) {}

MongoChangeStream& MongoChangeStream::operator=(MongoChangeStream&& other) noexcept {
    if (this != &other) {
        Destroy();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void MongoChangeStream::Destroy() {
    if (impl_ && impl_->stream) {
        mongoc_change_stream_destroy(impl_->stream);
        impl_->stream = nullptr;
    }
}

bool MongoChangeStream::Next(BsonDocument* out) {
    if (!impl_ || !impl_->stream) return false;
    const bson_t* doc = nullptr;
    bool ok = mongoc_change_stream_next(impl_->stream, &doc);
    if (ok && doc && out) {
        bson_destroy(static_cast<bson_t*>(out->RawBson()));
        bson_copy_to(doc, static_cast<bson_t*>(out->RawBson()));
    }
    return ok;
}

const void* MongoChangeStream::GetResumeToken() const {
    if (!impl_ || !impl_->stream) return nullptr;
    return mongoc_change_stream_get_resume_token(impl_->stream);
}

bool MongoChangeStream::ErrorDocument(MongoError* error, const void** doc) const {
    if (!impl_ || !impl_->stream) return false;
    return mongoc_change_stream_error_document(impl_->stream,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr,
        reinterpret_cast<const bson_t**>(doc));
}

void MongoChangeStream::SetRawStream(void* stream) {
    if (!impl_) return;
    if (impl_->stream) mongoc_change_stream_destroy(impl_->stream);
    impl_->stream = static_cast<mongoc_change_stream_t*>(stream);
}

} // namespace mongo
} // namespace engine

#endif
