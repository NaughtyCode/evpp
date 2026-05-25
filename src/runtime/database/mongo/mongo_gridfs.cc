#include "runtime/database/mongo/mongo_gridfs.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// MongoGridFsFileOpts
// ═══════════════════════════════════════════════════════════════════════

struct MongoGridFsFileOpts::Impl {
    mongoc_gridfs_file_opt_t opts;
    std::string filename;
    std::string content_type;
};

MongoGridFsFileOpts::MongoGridFsFileOpts() : impl_(std::make_unique<Impl>()) {
    memset(&impl_->opts, 0, sizeof(impl_->opts));
}

MongoGridFsFileOpts::~MongoGridFsFileOpts() = default;
MongoGridFsFileOpts::MongoGridFsFileOpts(MongoGridFsFileOpts&&) noexcept = default;
MongoGridFsFileOpts& MongoGridFsFileOpts::operator=(MongoGridFsFileOpts&&) noexcept = default;

void MongoGridFsFileOpts::SetFilename(const char* f) {
    impl_->filename = f;
    impl_->opts.filename = impl_->filename.c_str();
}

void MongoGridFsFileOpts::SetContentType(const char* ct) {
    impl_->content_type = ct;
    impl_->opts.content_type = impl_->content_type.c_str();
}

void MongoGridFsFileOpts::SetChunkSize(int32_t cs) {
    impl_->opts.chunk_size = cs;
}

void MongoGridFsFileOpts::SetAliases(const BsonDocument& aliases) {
    impl_->opts.aliases = static_cast<const bson_t*>(aliases.RawBson());
}

void MongoGridFsFileOpts::SetMetadata(const BsonDocument& metadata) {
    impl_->opts.metadata = static_cast<const bson_t*>(metadata.RawBson());
}

const char* MongoGridFsFileOpts::GetFilename() const { return impl_->opts.filename; }
const char* MongoGridFsFileOpts::GetContentType() const { return impl_->opts.content_type; }
int32_t MongoGridFsFileOpts::GetChunkSize() const { return impl_->opts.chunk_size; }

void* MongoGridFsFileOpts::Raw() { return &impl_->opts; }

// ═══════════════════════════════════════════════════════════════════════
// MongoGridFsFile
// ═══════════════════════════════════════════════════════════════════════

struct MongoGridFsFile::Impl {
    mongoc_gridfs_file_t* file = nullptr;
};

MongoGridFsFile::MongoGridFsFile() : impl_(std::make_unique<Impl>()) {}
MongoGridFsFile::~MongoGridFsFile() { Destroy(); }

void MongoGridFsFile::Destroy() {
    if (impl_ && impl_->file) {
        mongoc_gridfs_file_destroy(impl_->file);
        impl_->file = nullptr;
    }
}

const char* MongoGridFsFile::GetFilename() const {
    return impl_ && impl_->file ? mongoc_gridfs_file_get_filename(impl_->file) : nullptr;
}

int64_t MongoGridFsFile::GetLength() const {
    return impl_ && impl_->file ? mongoc_gridfs_file_get_length(impl_->file) : 0;
}

int32_t MongoGridFsFile::GetChunkSize() const {
    return impl_ && impl_->file ? mongoc_gridfs_file_get_chunk_size(impl_->file) : 0;
}

int64_t MongoGridFsFile::GetUploadDate() const {
    return impl_ && impl_->file ? mongoc_gridfs_file_get_upload_date(impl_->file) : 0;
}

const char* MongoGridFsFile::GetMd5() const {
    return impl_ && impl_->file ? mongoc_gridfs_file_get_md5(impl_->file) : nullptr;
}

void MongoGridFsFile::GetId(BsonDocument* out) const {
    if (impl_ && impl_->file && out)
        bson_copy_to(mongoc_gridfs_file_get_id(impl_->file),
                     static_cast<bson_t*>(out->RawBson()));
}

void MongoGridFsFile::GetMetadata(BsonDocument* out) const {
    if (impl_ && impl_->file && out) {
        const bson_t* m = mongoc_gridfs_file_get_metadata(impl_->file);
        if (m) bson_copy_to(m, static_cast<bson_t*>(out->RawBson()));
    }
}

void MongoGridFsFile::SetMd5(const char* md5) {
    if (impl_ && impl_->file)
        mongoc_gridfs_file_set_md5(impl_->file, md5);
}

void MongoGridFsFile::SetFilename(const char* filename) {
    if (impl_ && impl_->file)
        mongoc_gridfs_file_set_filename(impl_->file, filename);
}

void MongoGridFsFile::SetContentType(const char* content_type) {
    if (impl_ && impl_->file)
        mongoc_gridfs_file_set_content_type(impl_->file, content_type);
}

void MongoGridFsFile::SetAliases(const BsonDocument& aliases) {
    if (impl_ && impl_->file)
        mongoc_gridfs_file_set_aliases(impl_->file,
            static_cast<const bson_t*>(aliases.RawBson()));
}

void MongoGridFsFile::SetMetadata(const BsonDocument& metadata) {
    if (impl_ && impl_->file)
        mongoc_gridfs_file_set_metadata(impl_->file,
            static_cast<const bson_t*>(metadata.RawBson()));
}

bool MongoGridFsFile::SetId(const void* id, MongoError* error) {
    return impl_ && impl_->file && mongoc_gridfs_file_set_id(impl_->file,
        static_cast<const bson_value_t*>(id),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

uint64_t MongoGridFsFile::Tell() {
    return impl_ && impl_->file ? mongoc_gridfs_file_tell(impl_->file) : 0;
}

bool MongoGridFsFile::Error(MongoError* error) const {
    return impl_ && impl_->file && mongoc_gridfs_file_error(impl_->file,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

ssize_t MongoGridFsFile::Readv(void* iov, size_t iovcnt, size_t min_bytes, int32_t timeout_msec) {
    return impl_ && impl_->file
        ? mongoc_gridfs_file_readv(impl_->file, static_cast<mongoc_iovec_t*>(iov),
                                   iovcnt, min_bytes, timeout_msec)
        : -1;
}

ssize_t MongoGridFsFile::Writev(const void* iov, size_t iovcnt, int32_t timeout_msec) {
    return impl_ && impl_->file
        ? mongoc_gridfs_file_writev(impl_->file,
                                    static_cast<const mongoc_iovec_t*>(iov),
                                    iovcnt, timeout_msec)
        : -1;
}

bool MongoGridFsFile::Save() {
    return impl_ && impl_->file && mongoc_gridfs_file_save(impl_->file);
}

bool MongoGridFsFile::Seek(int64_t pos, int whence) {
    return impl_ && impl_->file && mongoc_gridfs_file_seek(impl_->file, pos, whence);
}

bool MongoGridFsFile::SetChunkSize(int32_t chunk_size) {
    return impl_ && impl_->file && mongoc_gridfs_file_set_chunk_size(impl_->file, chunk_size);
}

void MongoGridFsFile::Remove(MongoError* error) {
    if (impl_ && impl_->file)
        mongoc_gridfs_file_remove(impl_->file,
            error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoGridFsFile::Raw() { return impl_ ? impl_->file : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoGridFsFileList
// ═══════════════════════════════════════════════════════════════════════

struct MongoGridFsFileList::Impl {
    mongoc_gridfs_file_list_t* list = nullptr;
};

MongoGridFsFileList::MongoGridFsFileList() : impl_(std::make_unique<Impl>()) {}
MongoGridFsFileList::~MongoGridFsFileList() { Destroy(); }

void MongoGridFsFileList::Destroy() {
    if (impl_ && impl_->list) {
        mongoc_gridfs_file_list_destroy(impl_->list);
        impl_->list = nullptr;
    }
}

MongoGridFsFile* MongoGridFsFileList::Next(MongoError* error) {
    if (!impl_ || !impl_->list) return nullptr;
    mongoc_gridfs_file_t* file = mongoc_gridfs_file_list_next(impl_->list,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    if (!file) return nullptr;
    auto* result = new MongoGridFsFile();
    result->impl_->file = file;
    return result;
}

void* MongoGridFsFileList::Raw() { return impl_ ? impl_->list : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoGridFs
// ═══════════════════════════════════════════════════════════════════════

struct MongoGridFs::Impl {
    mongoc_gridfs_t* gridfs = nullptr;
};

MongoGridFs::MongoGridFs() : impl_(std::make_unique<Impl>()) {}
MongoGridFs::~MongoGridFs() { Destroy(); }

void MongoGridFs::Destroy() {
    if (impl_ && impl_->gridfs) {
        mongoc_gridfs_destroy(impl_->gridfs);
        impl_->gridfs = nullptr;
    }
}

MongoGridFsFile* MongoGridFs::CreateFile(MongoGridFsFileOpts* opts) {
    if (!impl_ || !impl_->gridfs) return nullptr;
    mongoc_gridfs_file_t* file = mongoc_gridfs_create_file(
        impl_->gridfs, opts ? static_cast<mongoc_gridfs_file_opt_t*>(opts->Raw()) : nullptr);
    if (!file) return nullptr;
    auto* result = new MongoGridFsFile();
    result->impl_->file = file;
    return result;
}

MongoGridFsFile* MongoGridFs::CreateFileFromStream(void* stream,
                                                     MongoGridFsFileOpts* opts) {
    if (!impl_ || !impl_->gridfs) return nullptr;
    mongoc_gridfs_file_t* file = mongoc_gridfs_create_file_from_stream(
        impl_->gridfs, static_cast<mongoc_stream_t*>(stream),
        opts ? static_cast<mongoc_gridfs_file_opt_t*>(opts->Raw()) : nullptr);
    if (!file) return nullptr;
    auto* result = new MongoGridFsFile();
    result->impl_->file = file;
    return result;
}

MongoGridFsFile* MongoGridFs::FindOneByFilename(const char* filename, MongoError* error) {
    if (!impl_ || !impl_->gridfs) return nullptr;
    mongoc_gridfs_file_t* file = mongoc_gridfs_find_one_by_filename(impl_->gridfs, filename,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    if (!file) return nullptr;
    auto* result = new MongoGridFsFile();
    result->impl_->file = file;
    return result;
}

MongoGridFsFile* MongoGridFs::FindOneWithOpts(const BsonDocument& filter,
                                                const BsonDocument* opts,
                                                MongoError* error) {
    if (!impl_ || !impl_->gridfs) return nullptr;
    mongoc_gridfs_file_t* file = mongoc_gridfs_find_one_with_opts(impl_->gridfs,
        static_cast<const bson_t*>(filter.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
    if (!file) return nullptr;
    auto* result = new MongoGridFsFile();
    result->impl_->file = file;
    return result;
}

MongoGridFsFileList* MongoGridFs::FindWithOpts(const BsonDocument& filter,
                                                const BsonDocument* opts) {
    if (!impl_ || !impl_->gridfs) return nullptr;
    mongoc_gridfs_file_list_t* list = mongoc_gridfs_find_with_opts(impl_->gridfs,
        static_cast<const bson_t*>(filter.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!list) return nullptr;
    auto* result = new MongoGridFsFileList();
    result->impl_->list = list;
    return result;
}

bool MongoGridFs::Drop(MongoError* error) {
    return impl_ && impl_->gridfs && mongoc_gridfs_drop(impl_->gridfs,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoGridFs::RemoveByFilename(const char* filename, MongoError* error) {
    return impl_ && impl_->gridfs && mongoc_gridfs_remove_by_filename(impl_->gridfs, filename,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoGridFs::GetFilesCollection() {
    return impl_ && impl_->gridfs ? mongoc_gridfs_get_files(impl_->gridfs) : nullptr;
}

void* MongoGridFs::GetChunksCollection() {
    return impl_ && impl_->gridfs ? mongoc_gridfs_get_chunks(impl_->gridfs) : nullptr;
}

void* MongoGridFs::Raw() { return impl_ ? impl_->gridfs : nullptr; }

// ═══════════════════════════════════════════════════════════════════════
// MongoGridFsBucket
// ═══════════════════════════════════════════════════════════════════════

struct MongoGridFsBucket::Impl {
    mongoc_gridfs_bucket_t* bucket = nullptr;
};

MongoGridFsBucket::MongoGridFsBucket() : impl_(std::make_unique<Impl>()) {}
MongoGridFsBucket::~MongoGridFsBucket() { Destroy(); }

void MongoGridFsBucket::Destroy() {
    if (impl_ && impl_->bucket) {
        mongoc_gridfs_bucket_destroy(impl_->bucket);
        impl_->bucket = nullptr;
    }
}

void* MongoGridFsBucket::OpenUploadStream(const char* filename, const BsonDocument* opts,
                                            void* file_id_out, MongoError* error) {
    if (!impl_ || !impl_->bucket) return nullptr;
    return mongoc_gridfs_bucket_open_upload_stream(impl_->bucket, filename,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        static_cast<bson_value_t*>(file_id_out),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoGridFsBucket::OpenUploadStreamWithId(const void* file_id, const char* filename,
                                                  const BsonDocument* opts, MongoError* error) {
    if (!impl_ || !impl_->bucket) return nullptr;
    return mongoc_gridfs_bucket_open_upload_stream_with_id(impl_->bucket,
        static_cast<const bson_value_t*>(file_id), filename,
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoGridFsBucket::UploadFromStream(const char* filename, void* source_stream,
                                          const BsonDocument* opts, void* file_id_out,
                                          MongoError* error) {
    return impl_ && impl_->bucket && mongoc_gridfs_bucket_upload_from_stream(impl_->bucket,
        filename, static_cast<mongoc_stream_t*>(source_stream),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        static_cast<bson_value_t*>(file_id_out),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoGridFsBucket::UploadFromStreamWithId(const void* file_id, const char* filename,
                                                 void* source_stream, const BsonDocument* opts,
                                                 MongoError* error) {
    return impl_ && impl_->bucket && mongoc_gridfs_bucket_upload_from_stream_with_id(
        impl_->bucket,
        static_cast<const bson_value_t*>(file_id),
        filename,
        static_cast<mongoc_stream_t*>(source_stream),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

void* MongoGridFsBucket::OpenDownloadStream(const void* file_id, MongoError* error) {
    if (!impl_ || !impl_->bucket) return nullptr;
    return mongoc_gridfs_bucket_open_download_stream(impl_->bucket,
        static_cast<const bson_value_t*>(file_id),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoGridFsBucket::DownloadToStream(const void* file_id, void* destination,
                                           MongoError* error) {
    return impl_ && impl_->bucket && mongoc_gridfs_bucket_download_to_stream(impl_->bucket,
        static_cast<const bson_value_t*>(file_id),
        static_cast<mongoc_stream_t*>(destination),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoGridFsBucket::DeleteById(const void* file_id, MongoError* error) {
    return impl_ && impl_->bucket && mongoc_gridfs_bucket_delete_by_id(impl_->bucket,
        static_cast<const bson_value_t*>(file_id),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

MongoCursor* MongoGridFsBucket::Find(const BsonDocument& filter, const BsonDocument* opts) {
    if (!impl_ || !impl_->bucket) return nullptr;
    mongoc_cursor_t* cursor = mongoc_gridfs_bucket_find(impl_->bucket,
        static_cast<const bson_t*>(filter.RawBson()),
        opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr);
    if (!cursor) return nullptr;
    auto* result = new MongoCursor();
    result->SetCursor(cursor);
    return result;
}

bool MongoGridFsBucket::StreamError(void* stream, MongoError* error) {
    return mongoc_gridfs_bucket_stream_error(static_cast<mongoc_stream_t*>(stream),
        error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoGridFsBucket::AbortUpload(void* stream) {
    return mongoc_gridfs_bucket_abort_upload(static_cast<mongoc_stream_t*>(stream));
}

void* MongoGridFsBucket::Raw() { return impl_ ? impl_->bucket : nullptr; }

} // namespace mongo
} // namespace engine
