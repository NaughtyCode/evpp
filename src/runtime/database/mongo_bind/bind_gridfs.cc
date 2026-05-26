#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_gridfs.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>
#include <vector>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_gridfs.h"

namespace engine {
namespace script {
namespace {

// ═══════════════════════════════════════════════════════════════════════════
// MongoGridFsFileOpts
// ═══════════════════════════════════════════════════════════════════════════

const char* kFileOptsMeta = "mongoc.gridfs_file_opts";

int l_file_opts_gc(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoGridFsFileOpts>(L, 1, kFileOptsMeta);
    delete opts;
    *CheckUserdata<mongo::MongoGridFsFileOpts>(L, 1, kFileOptsMeta) = nullptr;
    return 0;
}

int l_file_opts_new(lua_State* L) {
    auto* opts = new (std::nothrow) mongo::MongoGridFsFileOpts();
    if (!opts) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoGridFsFileOpts>(L, kFileOptsMeta);
    *ud = opts;
    return 1;
}

int l_file_opts_destroy(lua_State* L) { l_file_opts_gc(L); return 0; }

int l_file_opts_set_filename(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoGridFsFileOpts>(L, 1, kFileOptsMeta);
    if (opts) opts->SetFilename(luaL_checkstring(L, 2));
    return 0;
}

int l_file_opts_set_content_type(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoGridFsFileOpts>(L, 1, kFileOptsMeta);
    if (opts) opts->SetContentType(luaL_checkstring(L, 2));
    return 0;
}

int l_file_opts_set_chunk_size(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoGridFsFileOpts>(L, 1, kFileOptsMeta);
    if (opts) opts->SetChunkSize(static_cast<uint32_t>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_file_opts_set_aliases(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoGridFsFileOpts>(L, 1, kFileOptsMeta);
    auto* aliases = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (opts && aliases) opts->SetAliases(*aliases);
    return 0;
}

int l_file_opts_set_metadata(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoGridFsFileOpts>(L, 1, kFileOptsMeta);
    auto* metadata = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (opts && metadata) opts->SetMetadata(*metadata);
    return 0;
}

const luaL_Reg kFileOptsLib[] = {
    {"gridfs_file_opts_new", l_file_opts_new},
    {"gridfs_file_opts_destroy", l_file_opts_destroy},
    {"gridfs_file_opts_set_filename", l_file_opts_set_filename},
    {"gridfs_file_opts_set_content_type", l_file_opts_set_content_type},
    {"gridfs_file_opts_set_chunk_size", l_file_opts_set_chunk_size},
    {"gridfs_file_opts_set_aliases", l_file_opts_set_aliases},
    {"gridfs_file_opts_set_metadata", l_file_opts_set_metadata},
    {nullptr, nullptr},
};

// ═══════════════════════════════════════════════════════════════════════════
// MongoGridFsFile
// ═══════════════════════════════════════════════════════════════════════════

const char* kFileMeta = "mongoc.gridfs_file";

int l_file_gc(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    if (file) file->Destroy();
    *CheckUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta) = nullptr;
    return 0;
}

int l_file_destroy(lua_State* L) { l_file_gc(L); return 0; }

int l_file_get_filename(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    const char* s = file ? file->GetFilename() : nullptr;
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_file_get_length(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    lua_pushinteger(L, file ? file->GetLength() : 0);
    return 1;
}

int l_file_get_chunk_size(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    lua_pushinteger(L, file ? file->GetChunkSize() : 0);
    return 1;
}

int l_file_get_upload_date(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    lua_pushinteger(L, file ? file->GetUploadDate() : 0);
    return 1;
}

int l_file_get_id(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    if (!file) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    file->GetId(doc);
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

int l_file_get_metadata(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    if (!file) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    file->GetMetadata(doc);
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

// Simple iovec struct for Readv/Writev
struct SimpleIovec {
    void* iov_base;
    size_t iov_len;
};

int l_file_readv(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    if (!file) { lua_pushnil(L); lua_pushstring(L, "invalid file"); return 2; }
    size_t size = static_cast<size_t>(luaL_checkinteger(L, 2));
    if (size == 0) { lua_pushstring(L, ""); return 1; }
    std::vector<char> buf(size);
    SimpleIovec iov;
    iov.iov_base = buf.data();
    iov.iov_len = size;
    ssize_t nread = file->Readv(&iov, 1, 0, 0);
    if (nread < 0) {
        mongo::MongoError error;
        file->Error(&error);
        lua_pushnil(L);
        lua_pushstring(L, error.Message() ? error.Message() : "read error");
        return 2;
    }
    lua_pushlstring(L, buf.data(), static_cast<size_t>(nread));
    return 1;
}

int l_file_writev(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    if (!file) { lua_pushnil(L); lua_pushstring(L, "invalid file"); return 2; }
    size_t len;
    const char* data = luaL_checklstring(L, 2, &len);
    SimpleIovec iov;
    iov.iov_base = const_cast<char*>(data);
    iov.iov_len = len;
    ssize_t written = file->Writev(&iov, 1, 0);
    if (written < 0) {
        mongo::MongoError error;
        file->Error(&error);
        lua_pushnil(L);
        lua_pushstring(L, error.Message() ? error.Message() : "write error");
        return 2;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(written));
    return 1;
}

int l_file_save(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    lua_pushboolean(L, file && file->Save());
    return 1;
}

int l_file_seek(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    auto pos = static_cast<int64_t>(luaL_checkinteger(L, 2));
    int whence = static_cast<int>(luaL_optinteger(L, 3, 0));
    lua_pushboolean(L, file && file->Seek(pos, whence));
    return 1;
}

int l_file_remove(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    if (!file) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, file->Remove(&error));
    return 1;
}

int l_file_set_filename(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    if (file) file->SetFilename(luaL_checkstring(L, 2));
    return 0;
}

int l_file_set_content_type(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    if (file) file->SetContentType(luaL_checkstring(L, 2));
    return 0;
}

int l_file_set_metadata_doc(lua_State* L) {
    auto* file = GetUserdata<mongo::MongoGridFsFile>(L, 1, kFileMeta);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (file && doc) file->SetMetadata(*doc);
    return 0;
}

const luaL_Reg kFileLib[] = {
    {"gridfs_file_destroy", l_file_destroy},
    {"gridfs_file_get_filename", l_file_get_filename},
    {"gridfs_file_get_length", l_file_get_length},
    {"gridfs_file_get_chunk_size", l_file_get_chunk_size},
    {"gridfs_file_get_upload_date", l_file_get_upload_date},
    {"gridfs_file_get_id", l_file_get_id},
    {"gridfs_file_get_metadata", l_file_get_metadata},
    {"gridfs_file_readv", l_file_readv},
    {"gridfs_file_writev", l_file_writev},
    {"gridfs_file_save", l_file_save},
    {"gridfs_file_seek", l_file_seek},
    {"gridfs_file_remove", l_file_remove},
    {"gridfs_file_set_filename", l_file_set_filename},
    {"gridfs_file_set_content_type", l_file_set_content_type},
    {"gridfs_file_set_metadata", l_file_set_metadata_doc},
    {nullptr, nullptr},
};

// ═══════════════════════════════════════════════════════════════════════════
// MongoGridFsFileList
// ═══════════════════════════════════════════════════════════════════════════

const char* kFileListMeta = "mongoc.gridfs_file_list";

int l_file_list_gc(lua_State* L) {
    auto* list = GetUserdata<mongo::MongoGridFsFileList>(L, 1, kFileListMeta);
    if (list) list->Destroy();
    *CheckUserdata<mongo::MongoGridFsFileList>(L, 1, kFileListMeta) = nullptr;
    return 0;
}

int l_file_list_destroy(lua_State* L) { l_file_list_gc(L); return 0; }

int l_file_list_next(lua_State* L) {
    auto* list = GetUserdata<mongo::MongoGridFsFileList>(L, 1, kFileListMeta);
    if (!list) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    auto* file = list->Next(&error);
    if (!file) {
        // Check if we hit an error or just end of list
        if (const char* msg = error.Message()) {
            lua_pushnil(L);
            lua_pushstring(L, msg);
            return 2;
        }
        lua_pushnil(L);
        return 1;
    }
    auto** ud = NewUserdata<mongo::MongoGridFsFile>(L, kFileMeta);
    *ud = file;
    return 1;
}

const luaL_Reg kFileListLib[] = {
    {"gridfs_file_list_destroy", l_file_list_destroy},
    {"gridfs_file_list_next", l_file_list_next},
    {nullptr, nullptr},
};

// ═══════════════════════════════════════════════════════════════════════════
// MongoGridFs (legacy)
// ═══════════════════════════════════════════════════════════════════════════

const char* kGridFsMeta = "mongoc.gridfs";

int l_gridfs_gc(lua_State* L) {
    auto* gridfs = GetUserdata<mongo::MongoGridFs>(L, 1, kGridFsMeta);
    if (gridfs) gridfs->Destroy();
    *CheckUserdata<mongo::MongoGridFs>(L, 1, kGridFsMeta) = nullptr;
    return 0;
}

int l_gridfs_destroy(lua_State* L) { l_gridfs_gc(L); return 0; }

int l_gridfs_new_file(lua_State* L) {
    auto* gridfs = GetUserdata<mongo::MongoGridFs>(L, 1, kGridFsMeta);
    auto* opts = lua_isnoneornil(L, 2) ? nullptr
                 : GetUserdata<mongo::MongoGridFsFileOpts>(L, 2, kFileOptsMeta);
    if (!gridfs) { lua_pushnil(L); return 1; }
    auto* file = gridfs->NewFile(opts);
    if (!file) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoGridFsFile>(L, kFileMeta);
    *ud = file;
    return 1;
}

int l_gridfs_find_one_by_filename(lua_State* L) {
    auto* gridfs = GetUserdata<mongo::MongoGridFs>(L, 1, kGridFsMeta);
    const char* filename = luaL_checkstring(L, 2);
    if (!gridfs) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    auto* file = gridfs->FindOneByFilename(filename, &error);
    if (!file) {
        lua_pushnil(L);
        if (const char* msg = error.Message()) lua_pushstring(L, msg);
        else lua_pushnil(L);
        return 2;
    }
    auto** ud = NewUserdata<mongo::MongoGridFsFile>(L, kFileMeta);
    *ud = file;
    return 1;
}

int l_gridfs_find_one_with_opts(lua_State* L) {
    auto* gridfs = GetUserdata<mongo::MongoGridFs>(L, 1, kGridFsMeta);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!gridfs || !filter) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    auto* file = gridfs->FindOneWithOpts(*filter, opts, &error);
    if (!file) {
        lua_pushnil(L);
        if (const char* msg = error.Message()) lua_pushstring(L, msg);
        else lua_pushnil(L);
        return 2;
    }
    auto** ud = NewUserdata<mongo::MongoGridFsFile>(L, kFileMeta);
    *ud = file;
    return 1;
}

int l_gridfs_find_with_opts(lua_State* L) {
    auto* gridfs = GetUserdata<mongo::MongoGridFs>(L, 1, kGridFsMeta);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!gridfs || !filter) { lua_pushnil(L); return 1; }
    auto* list = gridfs->FindWithOpts(*filter, opts);
    if (!list) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoGridFsFileList>(L, kFileListMeta);
    *ud = list;
    return 1;
}

int l_gridfs_drop(lua_State* L) {
    auto* gridfs = GetUserdata<mongo::MongoGridFs>(L, 1, kGridFsMeta);
    if (!gridfs) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, gridfs->Drop(&error));
    return 1;
}

int l_gridfs_remove_by_filename(lua_State* L) {
    auto* gridfs = GetUserdata<mongo::MongoGridFs>(L, 1, kGridFsMeta);
    const char* filename = luaL_checkstring(L, 2);
    if (!gridfs) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, gridfs->RemoveByFilename(filename, &error));
    return 1;
}

const luaL_Reg kGridFsLib[] = {
    {"gridfs_destroy", l_gridfs_destroy},
    {"gridfs_new_file", l_gridfs_new_file},
    {"gridfs_find_one_by_filename", l_gridfs_find_one_by_filename},
    {"gridfs_find_one_with_opts", l_gridfs_find_one_with_opts},
    {"gridfs_find_with_opts", l_gridfs_find_with_opts},
    {"gridfs_drop", l_gridfs_drop},
    {"gridfs_remove_by_filename", l_gridfs_remove_by_filename},
    {nullptr, nullptr},
};

// ═══════════════════════════════════════════════════════════════════════════
// MongoGridFsBucket (modern API)
// ═══════════════════════════════════════════════════════════════════════════

const char* kBucketMeta = "mongoc.gridfs_bucket";

int l_bucket_gc(lua_State* L) {
    auto* bucket = GetUserdata<mongo::MongoGridFsBucket>(L, 1, kBucketMeta);
    if (bucket) bucket->Destroy();
    *CheckUserdata<mongo::MongoGridFsBucket>(L, 1, kBucketMeta) = nullptr;
    return 0;
}

int l_bucket_destroy(lua_State* L) { l_bucket_gc(L); return 0; }

int l_bucket_new(lua_State* L) {
    void* raw_db = lua_touserdata(L, 1); // mongoc_database_t*
    auto* opts = lua_isnoneornil(L, 2) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* prefs = lua_isnoneornil(L, 3) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
    if (!raw_db) { lua_pushnil(L); lua_pushstring(L, "invalid database"); return 2; }
    mongo::MongoError error;
    auto* bucket = mongo::MongoGridFsBucket::New(raw_db, opts, prefs, &error);
    if (!bucket) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    auto** ud = NewUserdata<mongo::MongoGridFsBucket>(L, kBucketMeta);
    *ud = bucket;
    return 1;
}

int l_bucket_open_upload_stream(lua_State* L) {
    auto* bucket = GetUserdata<mongo::MongoGridFsBucket>(L, 1, kBucketMeta);
    const char* filename = luaL_checkstring(L, 2);
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!bucket) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    void* file_id_out = nullptr;
    void* stream = bucket->OpenUploadStream(filename, opts, &file_id_out, &error);
    if (!stream) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    lua_pushlightuserdata(L, stream);
    // file_id_out is a bson_value_t*; we may set it on return
    lua_pushlightuserdata(L, file_id_out);
    return 2;
}

int l_bucket_open_download_stream(lua_State* L) {
    auto* bucket = GetUserdata<mongo::MongoGridFsBucket>(L, 1, kBucketMeta);
    void* file_id = lua_touserdata(L, 2); // bson_value_t*
    if (!bucket || !file_id) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    void* stream = bucket->OpenDownloadStream(file_id, &error);
    if (!stream) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    lua_pushlightuserdata(L, stream);
    return 1;
}

int l_bucket_delete_by_id(lua_State* L) {
    auto* bucket = GetUserdata<mongo::MongoGridFsBucket>(L, 1, kBucketMeta);
    void* file_id = lua_touserdata(L, 2);
    if (!bucket) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, bucket->DeleteById(file_id, &error));
    return 1;
}

int l_bucket_find(lua_State* L) {
    auto* bucket = GetUserdata<mongo::MongoGridFsBucket>(L, 1, kBucketMeta);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!bucket || !filter) { lua_pushnil(L); return 1; }
    auto* cursor = bucket->Find(*filter, opts);
    if (!cursor) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
    *ud = cursor;
    return 1;
}

const luaL_Reg kBucketLib[] = {
    {"gridfs_bucket_new", l_bucket_new},
    {"gridfs_bucket_destroy", l_bucket_destroy},
    {"gridfs_bucket_open_upload_stream", l_bucket_open_upload_stream},
    {"gridfs_bucket_open_download_stream", l_bucket_open_download_stream},
    {"gridfs_bucket_delete_by_id", l_bucket_delete_by_id},
    {"gridfs_bucket_find", l_bucket_find},
    {nullptr, nullptr},
};

} // namespace

// ── Metatable registration functions ───────────────────────────────────────

void RegisterMongoGridFsFileOptsMeta(lua_State* L) {
    RegisterMetatable(L, kFileOptsMeta, nullptr, l_file_opts_gc);
}
void RegisterMongoGridFsFileMeta(lua_State* L) {
    RegisterMetatable(L, kFileMeta, nullptr, l_file_gc);
}
void RegisterMongoGridFsFileListMeta(lua_State* L) {
    RegisterMetatable(L, kFileListMeta, nullptr, l_file_list_gc);
}
void RegisterMongoGridFsMeta(lua_State* L) {
    RegisterMetatable(L, kGridFsMeta, nullptr, l_gridfs_gc);
}
void RegisterMongoGridFsBucketMeta(lua_State* L) {
    RegisterMetatable(L, kBucketMeta, nullptr, l_bucket_gc);
}

// ── Lib getter functions ───────────────────────────────────────────────────

const luaL_Reg* GetMongoGridFsFileOptsLib() { return kFileOptsLib; }
const luaL_Reg* GetMongoGridFsFileLib()    { return kFileLib; }
const luaL_Reg* GetMongoGridFsFileListLib(){ return kFileListLib; }
const luaL_Reg* GetMongoGridFsLib()        { return kGridFsLib; }
const luaL_Reg* GetMongoGridFsBucketLib()  { return kBucketLib; }

} // namespace script
} // namespace engine

#endif
