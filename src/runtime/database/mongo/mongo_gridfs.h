#pragma once

#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Options for creating a GridFS file.
class ENGINE_API MongoGridFsFileOpts {
public:
    MongoGridFsFileOpts();
    ~MongoGridFsFileOpts();
    MongoGridFsFileOpts(const MongoGridFsFileOpts&) = delete;
    MongoGridFsFileOpts& operator=(const MongoGridFsFileOpts&) = delete;
    MongoGridFsFileOpts(MongoGridFsFileOpts&&) noexcept;
    MongoGridFsFileOpts& operator=(MongoGridFsFileOpts&&) noexcept;

    void SetFilename(const char* filename);
    void SetContentType(const char* content_type);
    void SetChunkSize(int32_t chunk_size);
    void SetAliases(const BsonDocument& aliases);
    void SetMetadata(const BsonDocument& metadata);

    const char* GetFilename() const;
    const char* GetContentType() const;
    int32_t GetChunkSize() const;

    void* Raw();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Legacy GridFS file object.
class ENGINE_API MongoGridFsFile {
public:
    void Destroy();

    const char* GetFilename() const;
    int64_t GetLength() const;
    const char* GetContentType() const;
    int32_t GetChunkSize() const;
    int64_t GetUploadDate() const;
    const char* GetMd5() const;
    void GetId(BsonDocument* out) const;
    void GetMetadata(BsonDocument* out) const;
    void GetAliases(BsonDocument* out) const;

    // Setters
    void SetMd5(const char* md5);
    void SetFilename(const char* filename);
    void SetContentType(const char* content_type);
    void SetAliases(const BsonDocument& aliases);
    void SetMetadata(const BsonDocument& metadata);
    bool SetId(const void* id, MongoError* error);

    // Position and error
    uint64_t Tell();
    bool Error(MongoError* error) const;

    // Read/write operations
    ssize_t Readv(void* iov, size_t iovcnt, size_t min_bytes, int32_t timeout_msec);
    ssize_t Writev(const void* iov, size_t iovcnt, int32_t timeout_msec);
    bool Save();
    bool Seek(int64_t pos, int whence);

    void Remove(MongoError* error);

    void* Raw(); // returns mongoc_gridfs_file_t*

private:
    friend class MongoGridFs;
    friend class MongoGridFsFileList;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoGridFsFile();
    ~MongoGridFsFile();
    MongoGridFsFile(const MongoGridFsFile&) = delete;
    MongoGridFsFile& operator=(const MongoGridFsFile&) = delete;
    MongoGridFsFile(MongoGridFsFile&&) = delete;
    MongoGridFsFile& operator=(MongoGridFsFile&&) = delete;
};

// List of GridFS files (cursor-like).
class ENGINE_API MongoGridFsFileList {
public:
    void Destroy();

    MongoGridFsFile* Next(MongoError* error);
    bool Error(MongoError* error) const;

    void* Raw(); // returns mongoc_gridfs_file_list_t*

private:
    friend class MongoGridFs;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoGridFsFileList();
    ~MongoGridFsFileList();
    MongoGridFsFileList(const MongoGridFsFileList&) = delete;
    MongoGridFsFileList& operator=(const MongoGridFsFileList&) = delete;
    MongoGridFsFileList(MongoGridFsFileList&&) = delete;
    MongoGridFsFileList& operator=(MongoGridFsFileList&&) = delete;
};

// Legacy GridFS interface (wraps mongoc_gridfs_t).
class ENGINE_API MongoGridFs {
public:
    void Destroy();

    MongoGridFsFile* CreateFile(MongoGridFsFileOpts* opts);
    MongoGridFsFile* CreateFileFromStream(void* stream, MongoGridFsFileOpts* opts);
    MongoGridFsFile* FindOneByFilename(const char* filename, MongoError* error);
    MongoGridFsFile* FindOneWithOpts(const BsonDocument& filter, const BsonDocument* opts,
                                      MongoError* error);
    MongoGridFsFileList* FindWithOpts(const BsonDocument& filter, const BsonDocument* opts);

    bool Drop(MongoError* error);
    bool RemoveByFilename(const char* filename, MongoError* error);

    void* GetFilesCollection();  // returns mongoc_collection_t*
    void* GetChunksCollection(); // returns mongoc_collection_t*

    void* Raw(); // returns mongoc_gridfs_t*

private:
    friend class MongoClient;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoGridFs();
    ~MongoGridFs();
    MongoGridFs(const MongoGridFs&) = delete;
    MongoGridFs& operator=(const MongoGridFs&) = delete;
    MongoGridFs(MongoGridFs&&) = delete;
    MongoGridFs& operator=(MongoGridFs&&) = delete;
};

// Modern GridFS Bucket API (wraps mongoc_gridfs_bucket_t).
class ENGINE_API MongoGridFsBucket {
public:
    // Create a new GridFS bucket from a database.
    static MongoGridFsBucket* New(void* raw_database, const BsonDocument* opts,
                                   const MongoReadPrefs* read_prefs, MongoError* error);

    void Destroy();

    // Upload
    void* OpenUploadStream(const char* filename, const BsonDocument* opts,
                           void* file_id_out, MongoError* error);
    void* OpenUploadStreamWithId(const void* file_id, const char* filename,
                                  const BsonDocument* opts, MongoError* error);
    bool UploadFromStream(const char* filename, void* source_stream,
                          const BsonDocument* opts, void* file_id_out, MongoError* error);
    bool UploadFromStreamWithId(const void* file_id, const char* filename,
                                 void* source_stream, const BsonDocument* opts,
                                 MongoError* error);

    // Download
    void* OpenDownloadStream(const void* file_id, MongoError* error);
    bool DownloadToStream(const void* file_id, void* destination, MongoError* error);

    // Delete
    bool DeleteById(const void* file_id, MongoError* error);

    // Find
    MongoCursor* Find(const BsonDocument& filter, const BsonDocument* opts);

    // Stream error
    static bool StreamError(void* stream, MongoError* error);
    static bool AbortUpload(void* stream);

    void* Raw(); // returns mongoc_gridfs_bucket_t*

private:
    friend class MongoDatabase;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoGridFsBucket();
    ~MongoGridFsBucket();
    MongoGridFsBucket(const MongoGridFsBucket&) = delete;
    MongoGridFsBucket& operator=(const MongoGridFsBucket&) = delete;
    MongoGridFsBucket(MongoGridFsBucket&&) = delete;
    MongoGridFsBucket& operator=(MongoGridFsBucket&&) = delete;
};

} // namespace mongo
} // namespace engine
