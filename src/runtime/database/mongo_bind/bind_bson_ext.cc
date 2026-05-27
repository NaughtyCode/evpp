#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bson_ext.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <bson/bson.h>
#include <new>
#include <cstring>
#include <cstdarg>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bson_ext.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace script {
namespace {

// ── Metatable names ─────────────────────────────────────────────────────

const char* kMetaContext        = "bson.context";
const char* kMetaString         = "bson.string";
const char* kMetaJsonReader     = "bson.json_reader";
const char* kMetaJsonDataReader = "bson.json_data_reader";
const char* kMetaReader         = "bson.reader";
const char* kMetaWriter         = "bson.writer";
const char* kMetaJsonOpts       = "bson.json_opts";
const char* kMetaValue          = "bson.value";

// ==========================================================================
//  BsonContext
// ==========================================================================

int l_context_gc(lua_State* L) {
    auto* ctx = GetUserdata<mongo::BsonContext>(L, 1, kMetaContext);
    delete ctx;
    *CheckUserdata<mongo::BsonContext>(L, 1, kMetaContext) = nullptr;
    return 0;
}

int l_context_new(lua_State* L) {
    auto* ctx = new (std::nothrow) mongo::BsonContext(mongo::BsonContext::New());
    if (!ctx) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonContext>(L, kMetaContext);
    *ud = ctx;
    return 1;
}

int l_context_default(lua_State* L) {
    lua_pushlightuserdata(L, const_cast<mongo::BsonContext&>(mongo::BsonContext::Default()).Raw());
    return 1;
}

int l_context_destroy(lua_State* L) { l_context_gc(L); return 0; }

int l_context_get_raw(lua_State* L) {
    auto* ctx = GetUserdata<mongo::BsonContext>(L, 1, kMetaContext);
    lua_pushlightuserdata(L, ctx ? ctx->Raw() : nullptr);
    return 1;
}

const luaL_Reg kContextLib[] = {
    {"context_new",        l_context_new},
    {"context_default",    l_context_default},
    {"context_destroy",    l_context_destroy},
    {"context_get_raw",    l_context_get_raw},
    {nullptr, nullptr},
};

// ==========================================================================
//  BsonString
// ==========================================================================

int l_string_gc(lua_State* L) {
    auto* s = GetUserdata<mongo::BsonString>(L, 1, kMetaString);
    delete s;
    *CheckUserdata<mongo::BsonString>(L, 1, kMetaString) = nullptr;
    return 0;
}

int l_string_new(lua_State* L) {
    auto* s = new (std::nothrow) mongo::BsonString();
    if (!s) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonString>(L, kMetaString);
    *ud = s;
    return 1;
}

int l_string_new_from(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    auto* s = new (std::nothrow) mongo::BsonString(str);
    if (!s) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonString>(L, kMetaString);
    *ud = s;
    return 1;
}

int l_string_destroy(lua_State* L) { l_string_gc(L); return 0; }

int l_string_append(lua_State* L) {
    auto* s = GetUserdata<mongo::BsonString>(L, 1, kMetaString);
    const char* str = luaL_checkstring(L, 2);
    if (s) s->Append(str);
    return 0;
}

int l_string_append_printf(lua_State* L) {
    auto* s = GetUserdata<mongo::BsonString>(L, 1, kMetaString);
    const char* fmt = luaL_checkstring(L, 2);
    if (s) s->Append(fmt);
    return 0;
}

int l_string_get_string(lua_State* L) {
    auto* s = GetUserdata<mongo::BsonString>(L, 1, kMetaString);
    if (!s) { lua_pushnil(L); return 1; }
    const char* str = s->GetString();
    if (str) lua_pushstring(L, str);
    else lua_pushnil(L);
    return 1;
}

int l_string_get_length(lua_State* L) {
    auto* s = GetUserdata<mongo::BsonString>(L, 1, kMetaString);
    lua_pushinteger(L, s ? static_cast<lua_Integer>(s->GetLength()) : 0);
    return 1;
}

int l_string_empty(lua_State* L) {
    auto* s = GetUserdata<mongo::BsonString>(L, 1, kMetaString);
    lua_pushboolean(L, s && s->Empty());
    return 1;
}

int l_string_truncate(lua_State* L) {
    auto* s = GetUserdata<mongo::BsonString>(L, 1, kMetaString);
    auto len = static_cast<size_t>(luaL_checkinteger(L, 2));
    if (s) s->Truncate(len);
    return 0;
}

int l_string_get_raw(lua_State* L) {
    auto* s = GetUserdata<mongo::BsonString>(L, 1, kMetaString);
    lua_pushlightuserdata(L, s ? s->Raw() : nullptr);
    return 1;
}

const luaL_Reg kStringLib[] = {
    {"string_new",            l_string_new},
    {"string_new_from",       l_string_new_from},
    {"string_destroy",        l_string_destroy},
    {"string_append",         l_string_append},
    {"string_append_printf",  l_string_append_printf},
    {"string_get_string",     l_string_get_string},
    {"string_get_length",     l_string_get_length},
    {"string_empty",          l_string_empty},
    {"string_truncate",       l_string_truncate},
    {"string_get_raw",        l_string_get_raw},
    {nullptr, nullptr},
};

// ==========================================================================
//  BsonJsonReader
// ==========================================================================

int l_json_reader_gc(lua_State* L) {
    auto* p = GetUserdata<mongo::BsonJsonReader>(L, 1, kMetaJsonReader);
    if (p) { p->Destroy(); delete p; }
    *CheckUserdata<mongo::BsonJsonReader>(L, 1, kMetaJsonReader) = nullptr;
    return 0;
}

int l_json_reader_new_from_fd(lua_State* L) {
    auto fd = static_cast<int>(luaL_checkinteger(L, 1));
    bool close = lua_toboolean(L, 2) != 0;
    auto reader = mongo::BsonJsonReader::NewFromFd(fd, close);
    auto* p = new (std::nothrow) mongo::BsonJsonReader(std::move(reader));
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonJsonReader>(L, kMetaJsonReader);
    *ud = p;
    return 1;
}

int l_json_reader_new_from_file(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    mongo::MongoError error;
    auto reader = mongo::BsonJsonReader::NewFromFile(path, &error);
    auto* p = new (std::nothrow) mongo::BsonJsonReader(std::move(reader));
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonJsonReader>(L, kMetaJsonReader);
    *ud = p;
    return 1;
}

int l_json_reader_new_from_data(lua_State* L) {
    size_t len;
    const char* data = luaL_checklstring(L, 1, &len);
    auto reader = mongo::BsonJsonReader::NewFromData(
        reinterpret_cast<const uint8_t*>(data), len);
    auto* p = new (std::nothrow) mongo::BsonJsonReader(std::move(reader));
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonJsonReader>(L, kMetaJsonReader);
    *ud = p;
    return 1;
}

int l_json_reader_destroy(lua_State* L) { l_json_reader_gc(L); return 0; }

int l_json_reader_read(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonJsonReader>(L, 1, kMetaJsonReader);
    if (!reader) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    mongo::MongoError error;
    if (!reader->Read(doc, &error)) {
        delete doc;
        lua_pushnil(L);
        lua_pushstring(L, error.Message() ? error.Message() : "read error");
        return 2;
    }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

int l_json_reader_error_desc(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonJsonReader>(L, 1, kMetaJsonReader);
    if (!reader) { lua_pushnil(L); return 1; }
    const char* desc = reader->ErrorDescription();
    if (desc) lua_pushstring(L, desc);
    else lua_pushnil(L);
    return 1;
}

int l_json_reader_get_raw(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonJsonReader>(L, 1, kMetaJsonReader);
    lua_pushlightuserdata(L, reader ? reader->Raw() : nullptr);
    return 1;
}

const luaL_Reg kJsonReaderLib[] = {
    {"json_reader_new_from_fd",   l_json_reader_new_from_fd},
    {"json_reader_new_from_file", l_json_reader_new_from_file},
    {"json_reader_new_from_data", l_json_reader_new_from_data},
    {"json_reader_destroy",       l_json_reader_destroy},
    {"json_reader_read",          l_json_reader_read},
    {"json_reader_error_desc",    l_json_reader_error_desc},
    {"json_reader_get_raw",       l_json_reader_get_raw},
    {nullptr, nullptr},
};

// ==========================================================================
//  BsonJsonDataReader
// ==========================================================================

int l_json_data_reader_gc(lua_State* L) {
    auto* p = GetUserdata<mongo::BsonJsonDataReader>(L, 1, kMetaJsonDataReader);
    if (p) { p->Destroy(); delete p; }
    *CheckUserdata<mongo::BsonJsonDataReader>(L, 1, kMetaJsonDataReader) = nullptr;
    return 0;
}

int l_json_data_reader_new(lua_State* L) {
    auto* p = new (std::nothrow) mongo::BsonJsonDataReader();
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonJsonDataReader>(L, kMetaJsonDataReader);
    *ud = p;
    return 1;
}

int l_json_data_reader_destroy(lua_State* L) { l_json_data_reader_gc(L); return 0; }

int l_json_data_reader_ingest(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonJsonDataReader>(L, 1, kMetaJsonDataReader);
    size_t len;
    const char* data = luaL_checklstring(L, 2, &len);
    lua_pushboolean(L, reader && reader->Ingest(
        reinterpret_cast<const uint8_t*>(data), len));
    return 1;
}

int l_json_data_reader_get_raw(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonJsonDataReader>(L, 1, kMetaJsonDataReader);
    lua_pushlightuserdata(L, reader ? reader->Raw() : nullptr);
    return 1;
}

const luaL_Reg kJsonDataReaderLib[] = {
    {"json_data_reader_new",      l_json_data_reader_new},
    {"json_data_reader_destroy",  l_json_data_reader_destroy},
    {"json_data_reader_ingest",   l_json_data_reader_ingest},
    {"json_data_reader_get_raw",  l_json_data_reader_get_raw},
    {nullptr, nullptr},
};

// ==========================================================================
//  BsonReader
// ==========================================================================

int l_reader_gc(lua_State* L) {
    auto* p = GetUserdata<mongo::BsonReader>(L, 1, kMetaReader);
    if (p) { p->Destroy(); delete p; }
    *CheckUserdata<mongo::BsonReader>(L, 1, kMetaReader) = nullptr;
    return 0;
}

int l_reader_new_from_data(lua_State* L) {
    size_t len;
    const char* data = luaL_checklstring(L, 1, &len);
    auto reader = mongo::BsonReader::NewFromData(
        reinterpret_cast<const uint8_t*>(data), len);
    auto* p = new (std::nothrow) mongo::BsonReader(std::move(reader));
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonReader>(L, kMetaReader);
    *ud = p;
    return 1;
}

int l_reader_new_from_file(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    mongo::MongoError error;
    auto reader = mongo::BsonReader::NewFromFile(path, &error);
    auto* p = new (std::nothrow) mongo::BsonReader(std::move(reader));
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonReader>(L, kMetaReader);
    *ud = p;
    return 1;
}

int l_reader_new_from_fd(lua_State* L) {
    auto fd = static_cast<int>(luaL_checkinteger(L, 1));
    bool close = lua_toboolean(L, 2) != 0;
    auto reader = mongo::BsonReader::NewFromFd(fd, close);
    auto* p = new (std::nothrow) mongo::BsonReader(std::move(reader));
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonReader>(L, kMetaReader);
    *ud = p;
    return 1;
}

int l_reader_destroy(lua_State* L) { l_reader_gc(L); return 0; }

int l_reader_read(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonReader>(L, 1, kMetaReader);
    if (!reader) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    mongo::MongoError error;
    if (!reader->Read(doc, &error)) {
        delete doc;
        lua_pushnil(L);
        lua_pushstring(L, error.Message() ? error.Message() : "read error");
        return 2;
    }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

int l_reader_read_raw(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonReader>(L, 1, kMetaReader);
    if (!reader) { lua_pushnil(L); lua_pushboolean(L, true); return 2; }
    bool eof = false;
    const void* raw = reader->Read(&eof);
    lua_pushlightuserdata(L, const_cast<void*>(raw));
    lua_pushboolean(L, eof);
    return 2;
}

int l_reader_set_data(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonReader>(L, 1, kMetaReader);
    size_t len;
    const char* data = luaL_checklstring(L, 2, &len);
    if (reader) reader->SetData(reinterpret_cast<const uint8_t*>(data), len);
    return 0;
}

int l_reader_tell(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonReader>(L, 1, kMetaReader);
    lua_pushinteger(L, reader ? reader->Tell() : 0);
    return 1;
}

int l_reader_reset(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonReader>(L, 1, kMetaReader);
    if (reader) reader->Reset();
    return 0;
}

int l_reader_get_raw(lua_State* L) {
    auto* reader = GetUserdata<mongo::BsonReader>(L, 1, kMetaReader);
    lua_pushlightuserdata(L, reader ? reader->Raw() : nullptr);
    return 1;
}

const luaL_Reg kReaderLib[] = {
    {"reader_new_from_data", l_reader_new_from_data},
    {"reader_new_from_file", l_reader_new_from_file},
    {"reader_new_from_fd",   l_reader_new_from_fd},
    {"reader_destroy",       l_reader_destroy},
    {"reader_read",          l_reader_read},
    {"reader_read_raw",      l_reader_read_raw},
    {"reader_set_data",      l_reader_set_data},
    {"reader_tell",          l_reader_tell},
    {"reader_reset",         l_reader_reset},
    {"reader_get_raw",       l_reader_get_raw},
    {nullptr, nullptr},
};

// ==========================================================================
//  BsonWriter
// ==========================================================================

int l_writer_gc(lua_State* L) {
    auto* p = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    if (p) { p->Destroy(); delete p; }
    *CheckUserdata<mongo::BsonWriter>(L, 1, kMetaWriter) = nullptr;
    return 0;
}

int l_writer_new(lua_State* L) {
    auto* p = new (std::nothrow) mongo::BsonWriter();
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonWriter>(L, kMetaWriter);
    *ud = p;
    return 1;
}

int l_writer_destroy(lua_State* L) { l_writer_gc(L); return 0; }

int l_writer_begin(lua_State* L) {
    auto* writer = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    lua_pushboolean(L, writer && writer->Begin(nullptr));
    return 1;
}

int l_writer_begin_document(lua_State* L) {
    auto* writer = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, writer && doc && writer->BeginDocument(doc));
    return 1;
}

int l_writer_begin_array(lua_State* L) {
    auto* writer = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    auto* arr = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, writer && arr && writer->BeginArray(arr));
    return 1;
}

int l_writer_end(lua_State* L) {
    auto* writer = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    if (!writer) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    writer->End(doc);
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

int l_writer_rollback(lua_State* L) {
    auto* writer = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    lua_pushboolean(L, writer && writer->Rollback());
    return 1;
}

int l_writer_get_buffer(lua_State* L) {
    auto* writer = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    if (!writer) { lua_pushnil(L); return 1; }
    size_t len = 0;
    const uint8_t* buf = writer->GetBuffer(&len);
    lua_pushlstring(L, reinterpret_cast<const char*>(buf), len);
    return 1;
}

int l_writer_get_length(lua_State* L) {
    auto* writer = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    lua_pushinteger(L, writer ? static_cast<lua_Integer>(writer->GetLength()) : 0);
    return 1;
}

int l_writer_get_raw(lua_State* L) {
    auto* writer = GetUserdata<mongo::BsonWriter>(L, 1, kMetaWriter);
    lua_pushlightuserdata(L, writer ? writer->Raw() : nullptr);
    return 1;
}

const luaL_Reg kWriterLib[] = {
    {"writer_new",           l_writer_new},
    {"writer_destroy",       l_writer_destroy},
    {"writer_begin",         l_writer_begin},
    {"writer_begin_document", l_writer_begin_document},
    {"writer_begin_array",   l_writer_begin_array},
    {"writer_end",           l_writer_end},
    {"writer_rollback",      l_writer_rollback},
    {"writer_get_buffer",    l_writer_get_buffer},
    {"writer_get_length",    l_writer_get_length},
    {"writer_get_raw",       l_writer_get_raw},
    {nullptr, nullptr},
};

// ==========================================================================
//  BsonJsonOpts
// ==========================================================================

int l_json_opts_gc(lua_State* L) {
    auto* p = GetUserdata<mongo::BsonJsonOpts>(L, 1, kMetaJsonOpts);
    delete p;
    *CheckUserdata<mongo::BsonJsonOpts>(L, 1, kMetaJsonOpts) = nullptr;
    return 0;
}

int l_json_opts_new(lua_State* L) {
    auto mode = static_cast<int>(luaL_optinteger(L, 1, 1));  // default canonical
    auto max_len = static_cast<int32_t>(luaL_optinteger(L, 2, -1));
    auto* p = new (std::nothrow) mongo::BsonJsonOpts(
        static_cast<mongo::BsonJsonMode>(mode), max_len);
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonJsonOpts>(L, kMetaJsonOpts);
    *ud = p;
    return 1;
}

int l_json_opts_destroy(lua_State* L) { l_json_opts_gc(L); return 0; }

int l_json_opts_set_outermost_array(lua_State* L) {
    auto* p = GetUserdata<mongo::BsonJsonOpts>(L, 1, kMetaJsonOpts);
    bool val = lua_toboolean(L, 2) != 0;
    if (p) p->SetOutermostArray(val);
    return 0;
}

int l_json_opts_get_raw(lua_State* L) {
    auto* p = GetUserdata<mongo::BsonJsonOpts>(L, 1, kMetaJsonOpts);
    lua_pushlightuserdata(L, p ? p->Raw() : nullptr);
    return 1;
}

const luaL_Reg kJsonOptsLib[] = {
    {"json_opts_new",                l_json_opts_new},
    {"json_opts_destroy",            l_json_opts_destroy},
    {"json_opts_set_outermost_array", l_json_opts_set_outermost_array},
    {"json_opts_get_raw",            l_json_opts_get_raw},
    {nullptr, nullptr},
};

// ==========================================================================
//  BsonValue
// ==========================================================================

int l_value_gc(lua_State* L) {
    auto* p = GetUserdata<mongo::BsonValue>(L, 1, kMetaValue);
    if (p) { p->Destroy(); delete p; }
    *CheckUserdata<mongo::BsonValue>(L, 1, kMetaValue) = nullptr;
    return 0;
}

int l_value_new(lua_State* L) {
    auto* p = new (std::nothrow) mongo::BsonValue();
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonValue>(L, kMetaValue);
    *ud = p;
    return 1;
}

int l_value_new_copy(lua_State* L) {
    auto* other = GetUserdata<mongo::BsonValue>(L, 1, kMetaValue);
    if (!other) { lua_pushnil(L); return 1; }
    auto* p = new (std::nothrow) mongo::BsonValue(*other);
    if (!p) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonValue>(L, kMetaValue);
    *ud = p;
    return 1;
}

int l_value_destroy(lua_State* L) { l_value_gc(L); return 0; }

int l_value_copy(lua_State* L) {
    auto* val = GetUserdata<mongo::BsonValue>(L, 1, kMetaValue);
    auto* other = GetUserdata<mongo::BsonValue>(L, 2, kMetaValue);
    if (val && other) val->Copy(*other);
    return 0;
}

int l_value_get_raw(lua_State* L) {
    auto* val = GetUserdata<mongo::BsonValue>(L, 1, kMetaValue);
    lua_pushlightuserdata(L, val ? val->Raw() : nullptr);
    return 1;
}

const luaL_Reg kValueLib[] = {
    {"value_new",      l_value_new},
    {"value_new_copy", l_value_new_copy},
    {"value_destroy",  l_value_destroy},
    {"value_copy",     l_value_copy},
    {"value_get_raw",  l_value_get_raw},
    {nullptr, nullptr},
};

// ==========================================================================
//  Static extension utilities (free functions)
// ==========================================================================

int l_utf8_validate(lua_State* L) {
    size_t len;
    const char* str = luaL_checklstring(L, 1, &len);
    bool allow_null = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, mongo::BsonUtf8::Validate(str, len, allow_null));
    return 1;
}

int l_utf8_escape_for_json(lua_State* L) {
    size_t len;
    const char* str = luaL_checklstring(L, 1, &len);
    char* escaped = mongo::BsonUtf8::EscapeForJson(str, len);
    if (escaped) {
        lua_pushstring(L, escaped);
        bson_free(escaped);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int l_utf8_get_char(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(mongo::BsonUtf8::GetChar(str)));
    return 1;
}

int l_utf8_next_char(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    const char* next = mongo::BsonUtf8::NextChar(str);
    if (next) {
        lua_pushinteger(L, static_cast<lua_Integer>(next - str));
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int l_clock_get_time_ns(lua_State* L) {
    lua_pushinteger(L, mongo::BsonClock::GetTimeNs());
    return 1;
}

int l_clock_get_date_time(lua_State* L) {
    lua_pushinteger(L, mongo::BsonClock::GetDateTime());
    return 1;
}

int l_str_dup(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    char* dup = mongo::BsonStrUtil::Strdup(str);
    if (dup) {
        lua_pushstring(L, dup);
        bson_free(dup);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int l_str_ndup(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    auto n = static_cast<size_t>(luaL_checkinteger(L, 2));
    char* dup = mongo::BsonStrUtil::Strndup(str, n);
    if (dup) {
        lua_pushstring(L, dup);
        bson_free(dup);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int l_str_ncpy(lua_State* L) {
    size_t src_len;
    const char* src = luaL_checklstring(L, 1, &src_len);
    auto size = static_cast<size_t>(luaL_checkinteger(L, 2));
    if (size == 0) { lua_pushstring(L, ""); return 1; }
    char* dst = static_cast<char*>(bson_malloc(size));
    if (!dst) { lua_pushnil(L); return 1; }
    mongo::BsonStrUtil::Strncpy(dst, src, size);
    lua_pushstring(L, dst);
    bson_free(dst);
    return 1;
}

int l_str_nlen(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    auto maxlen = static_cast<size_t>(luaL_checkinteger(L, 2));
    lua_pushinteger(L, static_cast<lua_Integer>(
        mongo::BsonStrUtil::Strnlen(s, maxlen)));
    return 1;
}

int l_str_casecmp(lua_State* L) {
    const char* a = luaL_checkstring(L, 1);
    const char* b = luaL_checkstring(L, 2);
    lua_pushinteger(L, mongo::BsonStrUtil::Strcasecmp(a, b));
    return 1;
}

int l_keys_uint32_to_string(lua_State* L) {
    auto val = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* strptr = nullptr;
    char buf[16];
    mongo::BsonKeys::Uint32ToString(val, &strptr, buf, sizeof(buf));
    lua_pushstring(L, strptr ? strptr : buf);
    return 1;
}

const luaL_Reg kExtLib[] = {
    {"utf8_validate",         l_utf8_validate},
    {"utf8_escape_for_json",  l_utf8_escape_for_json},
    {"utf8_get_char",         l_utf8_get_char},
    {"utf8_next_char",        l_utf8_next_char},
    {"clock_get_time_ns",     l_clock_get_time_ns},
    {"clock_get_date_time",   l_clock_get_date_time},
    {"str_dup",               l_str_dup},
    {"str_ndup",              l_str_ndup},
    {"str_ncpy",              l_str_ncpy},
    {"str_nlen",              l_str_nlen},
    {"str_casecmp",           l_str_casecmp},
    {"keys_uint32_to_string", l_keys_uint32_to_string},
    {nullptr, nullptr},
};

} // namespace

// ── Public API ───────────────────────────────────────────────────────────

void RegisterBsonContextMeta(lua_State* L) {
    RegisterMetatable(L, kMetaContext, nullptr, l_context_gc);
}
const luaL_Reg* GetBsonContextLib() { return kContextLib; }

void RegisterBsonStringMeta(lua_State* L) {
    RegisterMetatable(L, kMetaString, nullptr, l_string_gc);
}
const luaL_Reg* GetBsonStringLib() { return kStringLib; }

void RegisterBsonJsonReaderMeta(lua_State* L) {
    RegisterMetatable(L, kMetaJsonReader, nullptr, l_json_reader_gc);
}
const luaL_Reg* GetBsonJsonReaderLib() { return kJsonReaderLib; }

void RegisterBsonJsonDataReaderMeta(lua_State* L) {
    RegisterMetatable(L, kMetaJsonDataReader, nullptr, l_json_data_reader_gc);
}
const luaL_Reg* GetBsonJsonDataReaderLib() { return kJsonDataReaderLib; }

void RegisterBsonReaderMeta(lua_State* L) {
    RegisterMetatable(L, kMetaReader, nullptr, l_reader_gc);
}
const luaL_Reg* GetBsonReaderLib() { return kReaderLib; }

void RegisterBsonWriterMeta(lua_State* L) {
    RegisterMetatable(L, kMetaWriter, nullptr, l_writer_gc);
}
const luaL_Reg* GetBsonWriterLib() { return kWriterLib; }

void RegisterBsonJsonOptsMeta(lua_State* L) {
    RegisterMetatable(L, kMetaJsonOpts, nullptr, l_json_opts_gc);
}
const luaL_Reg* GetBsonJsonOptsLib() { return kJsonOptsLib; }

void RegisterBsonValueMeta(lua_State* L) {
    RegisterMetatable(L, kMetaValue, nullptr, l_value_gc);
}
const luaL_Reg* GetBsonValueLib() { return kValueLib; }

const luaL_Reg* GetBsonExtLib() { return kExtLib; }

} // namespace script
} // namespace engine

#endif
