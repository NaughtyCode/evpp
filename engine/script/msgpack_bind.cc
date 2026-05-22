#include "engine/script/msgpack_bind.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"
#include "engine/vm/vm.h"

namespace engine {
namespace script {

namespace {

constexpr int kMaxNesting = 16;

// ============================================================================
// Endian helper
// ============================================================================

inline bool IsLittleEndian() noexcept {
    constexpr int test = 1;
    return reinterpret_cast<const unsigned char*>(&test)[0] != 0;
}

inline void MemRevIfLE(void* ptr, size_t len) noexcept {
    if (!IsLittleEndian()) return;
    auto* p = static_cast<unsigned char*>(ptr);
    auto* e = p + len - 1;
    len /= 2;
    while (len--) {
        unsigned char aux = *p;
        *p = *e;
        *e = aux;
        ++p;
        --e;
    }
}

// ============================================================================
// Encoding buffer
// ============================================================================

struct EncodeBuf {
    std::vector<uint8_t> data;

    void Append(const unsigned char* s, size_t len) {
        data.insert(data.end(), s, s + len);
    }

    void Clear() { data.clear(); }

    size_t Size() const noexcept { return data.size(); }
    const unsigned char* Data() const noexcept { return data.data(); }
};

// ============================================================================
// Decoding cursor
// ============================================================================

enum class CurError { None, Eof, BadFmt };

struct DecodeCursor {
    const unsigned char* p;
    size_t left;
    CurError err = CurError::None;

    DecodeCursor(const unsigned char* s, size_t len) noexcept
        : p(s), left(len) {}

    void Consume(size_t len) noexcept { p += len; left -= len; }

    bool Need(size_t len) noexcept {
        if (left < len) {
            err = CurError::Eof;
            return false;
        }
        return true;
    }
};

// ============================================================================
// Push an unsigned 64-bit value to Lua.
// Values <= INT64_MAX are pushed as lua_Integer; larger values go via
// lua_Number (double) to avoid signed-overflow / negative integers.
// ============================================================================

void PushUnsigned(lua_State* L, uint64_t n) {
    if (n <= static_cast<uint64_t>(std::numeric_limits<lua_Integer>::max())) {
        lua_pushinteger(L, static_cast<lua_Integer>(n));
    } else {
        lua_pushnumber(L, static_cast<lua_Number>(n));
    }
}

// ============================================================================
// Test whether a lua_Number is exactly representable as int64.
// ============================================================================

inline bool IsInt64Equivalent(lua_Number x) noexcept {
    return !std::isinf(x) && static_cast<int64_t>(x) == x;
}

// ============================================================================
// Low-level MessagePack encoding
// ============================================================================

void EncodeBytes(EncodeBuf& buf, const unsigned char* s, size_t len) {
    if (len < 32) {
        uint8_t hdr = static_cast<uint8_t>(0xa0u | len);
        buf.Append(&hdr, 1);
    } else if (len <= 0xff) {
        uint8_t hdr[2] = {0xd9, static_cast<uint8_t>(len)};
        buf.Append(hdr, 2);
    } else if (len <= 0xffff) {
        uint8_t hdr[3] = {0xda,
                          static_cast<uint8_t>((len >> 8) & 0xff),
                          static_cast<uint8_t>(len & 0xff)};
        buf.Append(hdr, 3);
    } else {
        uint8_t hdr[5] = {0xdb,
                          static_cast<uint8_t>((len >> 24) & 0xff),
                          static_cast<uint8_t>((len >> 16) & 0xff),
                          static_cast<uint8_t>((len >> 8) & 0xff),
                          static_cast<uint8_t>(len & 0xff)};
        buf.Append(hdr, 5);
    }
    buf.Append(s, len);
}

void EncodeDouble(EncodeBuf& buf, double d) {
    static_assert(sizeof(float) == 4 && sizeof(double) == 8,
                  "unexpected float/double size");
    float f = static_cast<float>(d);
    if (d == static_cast<double>(f)) {
        uint8_t b[5];
        b[0] = 0xca;
        std::memcpy(b + 1, &f, 4);
        MemRevIfLE(b + 1, 4);
        buf.Append(b, 5);
    } else {
        uint8_t b[9];
        b[0] = 0xcb;
        std::memcpy(b + 1, &d, 8);
        MemRevIfLE(b + 1, 8);
        buf.Append(b, 9);
    }
}

void EncodeInt(EncodeBuf& buf, int64_t n) {
    if (n >= 0) {
        if (n <= 127) {
            uint8_t b = static_cast<uint8_t>(n & 0x7f);  // positive fixnum
            buf.Append(&b, 1);
        } else if (n <= 0xff) {
            uint8_t b[2] = {0xcc, static_cast<uint8_t>(n)};
            buf.Append(b, 2);
        } else if (n <= 0xffff) {
            uint8_t b[3] = {0xcd,
                            static_cast<uint8_t>((n >> 8) & 0xff),
                            static_cast<uint8_t>(n & 0xff)};
            buf.Append(b, 3);
        } else if (n <= 0xffffffffLL) {
            uint8_t b[5] = {0xce,
                            static_cast<uint8_t>((n >> 24) & 0xff),
                            static_cast<uint8_t>((n >> 16) & 0xff),
                            static_cast<uint8_t>((n >> 8) & 0xff),
                            static_cast<uint8_t>(n & 0xff)};
            buf.Append(b, 5);
        } else {
            uint8_t b[9] = {0xcf,
                            static_cast<uint8_t>((n >> 56) & 0xff),
                            static_cast<uint8_t>((n >> 48) & 0xff),
                            static_cast<uint8_t>((n >> 40) & 0xff),
                            static_cast<uint8_t>((n >> 32) & 0xff),
                            static_cast<uint8_t>((n >> 24) & 0xff),
                            static_cast<uint8_t>((n >> 16) & 0xff),
                            static_cast<uint8_t>((n >> 8) & 0xff),
                            static_cast<uint8_t>(n & 0xff)};
            buf.Append(b, 9);
        }
    } else {
        if (n >= -32) {
            uint8_t b = static_cast<uint8_t>(n);  // negative fixnum
            buf.Append(&b, 1);
        } else if (n >= -128) {
            uint8_t b[2] = {0xd0, static_cast<uint8_t>(n & 0xff)};
            buf.Append(b, 2);
        } else if (n >= -32768) {
            uint8_t b[3] = {0xd1,
                            static_cast<uint8_t>((n >> 8) & 0xff),
                            static_cast<uint8_t>(n & 0xff)};
            buf.Append(b, 3);
        } else if (n >= -2147483648LL) {
            uint8_t b[5] = {0xd2,
                            static_cast<uint8_t>((n >> 24) & 0xff),
                            static_cast<uint8_t>((n >> 16) & 0xff),
                            static_cast<uint8_t>((n >> 8) & 0xff),
                            static_cast<uint8_t>(n & 0xff)};
            buf.Append(b, 5);
        } else {
            uint8_t b[9] = {0xd3,
                            static_cast<uint8_t>((n >> 56) & 0xff),
                            static_cast<uint8_t>((n >> 48) & 0xff),
                            static_cast<uint8_t>((n >> 40) & 0xff),
                            static_cast<uint8_t>((n >> 32) & 0xff),
                            static_cast<uint8_t>((n >> 24) & 0xff),
                            static_cast<uint8_t>((n >> 16) & 0xff),
                            static_cast<uint8_t>((n >> 8) & 0xff),
                            static_cast<uint8_t>(n & 0xff)};
            buf.Append(b, 9);
        }
    }
}

void EncodeArray(EncodeBuf& buf, int64_t n) {
    if (n <= 15) {
        uint8_t b = static_cast<uint8_t>(0x90u | (n & 0xf));
        buf.Append(&b, 1);
    } else if (n <= 65535) {
        uint8_t b[3] = {0xdc,
                        static_cast<uint8_t>((n >> 8) & 0xff),
                        static_cast<uint8_t>(n & 0xff)};
        buf.Append(b, 3);
    } else {
        uint8_t b[5] = {0xdd,
                        static_cast<uint8_t>((n >> 24) & 0xff),
                        static_cast<uint8_t>((n >> 16) & 0xff),
                        static_cast<uint8_t>((n >> 8) & 0xff),
                        static_cast<uint8_t>(n & 0xff)};
        buf.Append(b, 5);
    }
}

void EncodeMap(EncodeBuf& buf, int64_t n) {
    if (n <= 15) {
        uint8_t b = static_cast<uint8_t>(0x80u | (n & 0xf));
        buf.Append(&b, 1);
    } else if (n <= 65535) {
        uint8_t b[3] = {0xde,
                        static_cast<uint8_t>((n >> 8) & 0xff),
                        static_cast<uint8_t>(n & 0xff)};
        buf.Append(b, 3);
    } else {
        uint8_t b[5] = {0xdf,
                        static_cast<uint8_t>((n >> 24) & 0xff),
                        static_cast<uint8_t>((n >> 16) & 0xff),
                        static_cast<uint8_t>((n >> 8) & 0xff),
                        static_cast<uint8_t>(n & 0xff)};
        buf.Append(b, 5);
    }
}

// ============================================================================
// Lua → MessagePack encoding
// ============================================================================

void EncodeLuaType(lua_State* L, EncodeBuf& buf, int level);

void EncodeLuaString(lua_State* L, EncodeBuf& buf) {
    size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    EncodeBytes(buf, reinterpret_cast<const unsigned char*>(s), len);
}

void EncodeLuaBool(lua_State* L, EncodeBuf& buf) {
    uint8_t b = lua_toboolean(L, -1) ? 0xc3u : 0xc2u;
    buf.Append(&b, 1);
}

void EncodeLuaInteger(lua_State* L, EncodeBuf& buf) {
    lua_Integer i = lua_tointeger(L, -1);
    EncodeInt(buf, static_cast<int64_t>(i));
}

void EncodeLuaNumber(lua_State* L, EncodeBuf& buf) {
    lua_Number n = lua_tonumber(L, -1);
    if (IsInt64Equivalent(n)) {
        EncodeLuaInteger(L, buf);
    } else {
        EncodeDouble(buf, static_cast<double>(n));
    }
}

void EncodeLuaNull(EncodeBuf& buf) {
    uint8_t b = 0xc0;
    buf.Append(&b, 1);
}

// Returns true if the table at stack top is a dense 1..N array.
bool TableIsArray(lua_State* L) {
    int stacktop = lua_gettop(L);
    int count = 0;
    lua_Integer max = 0;

    lua_pushnil(L);
    while (lua_next(L, -2)) {
        lua_pop(L, 1);  // pop value, keep key
        lua_Integer n = 0;
        if (!lua_isinteger(L, -1) || (n = lua_tointeger(L, -1)) <= 0) {
            lua_settop(L, stacktop);
            return false;
        }
        if (n > max) max = n;
        ++count;
    }
    lua_settop(L, stacktop);
    return max == count;
}

void EncodeLuaTableAsArray(lua_State* L, EncodeBuf& buf, int level) {
    size_t len = lua_rawlen(L, -1);
    EncodeArray(buf, static_cast<int64_t>(len));
    luaL_checkstack(L, 1, "in function EncodeLuaTableAsArray");
    for (size_t j = 1; j <= len; ++j) {
        lua_pushinteger(L, static_cast<lua_Integer>(j));
        lua_gettable(L, -2);
        EncodeLuaType(L, buf, level + 1);
    }
}

void EncodeLuaTableAsMap(lua_State* L, EncodeBuf& buf, int level) {
    size_t len = 0;

    luaL_checkstack(L, 3, "in function EncodeLuaTableAsMap");
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        lua_pop(L, 1);
        ++len;
    }

    EncodeMap(buf, static_cast<int64_t>(len));
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        lua_pushvalue(L, -2);
        EncodeLuaType(L, buf, level + 1);  // encode key
        EncodeLuaType(L, buf, level + 1);  // encode value
    }
}

void EncodeLuaTable(lua_State* L, EncodeBuf& buf, int level) {
    if (TableIsArray(L))
        EncodeLuaTableAsArray(L, buf, level);
    else
        EncodeLuaTableAsMap(L, buf, level);
}

void EncodeLuaType(lua_State* L, EncodeBuf& buf, int level) {
    int t = lua_type(L, -1);
    if (t == LUA_TTABLE && level == kMaxNesting) t = LUA_TNIL;

    switch (t) {
    case LUA_TSTRING:  EncodeLuaString(L, buf);   break;
    case LUA_TBOOLEAN: EncodeLuaBool(L, buf);     break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, -1))
            EncodeLuaInteger(L, buf);
        else
            EncodeLuaNumber(L, buf);
        break;
    case LUA_TTABLE:   EncodeLuaTable(L, buf, level); break;
    default:           EncodeLuaNull(buf);         break;
    }
    lua_pop(L, 1);
}

// ============================================================================
// MessagePack → Lua decoding
// ============================================================================

void DecodeToLuaType(lua_State* L, DecodeCursor* c);

void DecodeToLuaArray(lua_State* L, DecodeCursor* c, size_t len) {
    lua_createtable(L, static_cast<int>(len), 0);
    luaL_checkstack(L, 1, "in function DecodeToLuaArray");
    for (size_t j = 0; j < len; ++j) {
        lua_pushinteger(L, static_cast<lua_Integer>(j + 1));
        DecodeToLuaType(L, c);
        if (c->err != CurError::None) return;
        lua_settable(L, -3);
    }
}

void DecodeToLuaHash(lua_State* L, DecodeCursor* c, size_t len) {
    lua_createtable(L, 0, static_cast<int>(len));
    for (size_t i = 0; i < len; ++i) {
        DecodeToLuaType(L, c);  // key
        if (c->err != CurError::None) return;
        DecodeToLuaType(L, c);  // value
        if (c->err != CurError::None) return;
        lua_settable(L, -3);
    }
}

void DecodeToLuaType(lua_State* L, DecodeCursor* c) {
    if (!c->Need(1)) return;

    luaL_checkstack(L, 1,
        "too many return values at once; "
        "use unpack_one or unpack_limit instead.");

    switch (c->p[0]) {
    case 0xcc:  // uint 8
        if (!c->Need(2)) return;
        lua_pushinteger(L, static_cast<lua_Integer>(c->p[1]));
        c->Consume(2);
        break;
    case 0xd0:  // int 8
        if (!c->Need(2)) return;
        lua_pushinteger(L, static_cast<signed char>(c->p[1]));
        c->Consume(2);
        break;
    case 0xcd:  // uint 16
        if (!c->Need(3)) return;
        lua_pushinteger(L,
            static_cast<lua_Integer>(
                (static_cast<uint16_t>(c->p[1]) << 8) |
                 static_cast<uint16_t>(c->p[2])));
        c->Consume(3);
        break;
    case 0xd1:  // int 16
        if (!c->Need(3)) return;
        lua_pushinteger(L, static_cast<int16_t>(
            (c->p[1] << 8) | c->p[2]));
        c->Consume(3);
        break;
    case 0xce:  // uint 32
        if (!c->Need(5)) return;
        PushUnsigned(L,
            (static_cast<uint32_t>(c->p[1]) << 24) |
            (static_cast<uint32_t>(c->p[2]) << 16) |
            (static_cast<uint32_t>(c->p[3]) << 8) |
             static_cast<uint32_t>(c->p[4]));
        c->Consume(5);
        break;
    case 0xd2:  // int 32
        if (!c->Need(5)) return;
        lua_pushinteger(L,
            (static_cast<int32_t>(c->p[1]) << 24) |
            (static_cast<int32_t>(c->p[2]) << 16) |
            (static_cast<int32_t>(c->p[3]) << 8) |
             static_cast<int32_t>(c->p[4]));
        c->Consume(5);
        break;
    case 0xcf:  // uint 64
        if (!c->Need(9)) return;
        PushUnsigned(L,
            (static_cast<uint64_t>(c->p[1]) << 56) |
            (static_cast<uint64_t>(c->p[2]) << 48) |
            (static_cast<uint64_t>(c->p[3]) << 40) |
            (static_cast<uint64_t>(c->p[4]) << 32) |
            (static_cast<uint64_t>(c->p[5]) << 24) |
            (static_cast<uint64_t>(c->p[6]) << 16) |
            (static_cast<uint64_t>(c->p[7]) << 8) |
             static_cast<uint64_t>(c->p[8]));
        c->Consume(9);
        break;
    case 0xd3:  // int 64
        if (!c->Need(9)) return;
        lua_pushinteger(L,
            (static_cast<int64_t>(c->p[1]) << 56) |
            (static_cast<int64_t>(c->p[2]) << 48) |
            (static_cast<int64_t>(c->p[3]) << 40) |
            (static_cast<int64_t>(c->p[4]) << 32) |
            (static_cast<int64_t>(c->p[5]) << 24) |
            (static_cast<int64_t>(c->p[6]) << 16) |
            (static_cast<int64_t>(c->p[7]) << 8) |
             static_cast<int64_t>(c->p[8]));
        c->Consume(9);
        break;
    case 0xc0:  // nil
        lua_pushnil(L);
        c->Consume(1);
        break;
    case 0xc3:  // true
        lua_pushboolean(L, 1);
        c->Consume(1);
        break;
    case 0xc2:  // false
        lua_pushboolean(L, 0);
        c->Consume(1);
        break;
    case 0xca:  // float 32
        if (!c->Need(5)) return;
        {
            static_assert(sizeof(float) == 4, "unexpected float size");
            float f;
            std::memcpy(&f, c->p + 1, 4);
            MemRevIfLE(&f, 4);
            lua_pushnumber(L, static_cast<lua_Number>(f));
            c->Consume(5);
        }
        break;
    case 0xcb:  // float 64
        if (!c->Need(9)) return;
        {
            static_assert(sizeof(double) == 8, "unexpected double size");
            double d;
            std::memcpy(&d, c->p + 1, 8);
            MemRevIfLE(&d, 8);
            lua_pushnumber(L, static_cast<lua_Number>(d));
            c->Consume(9);
        }
        break;
    case 0xd9:  // str 8
        if (!c->Need(2)) return;
        {
            size_t l = c->p[1];
            if (!c->Need(2 + l)) return;
            lua_pushlstring(L, reinterpret_cast<const char*>(c->p + 2), l);
            c->Consume(2 + l);
        }
        break;
    case 0xda:  // str 16
        if (!c->Need(3)) return;
        {
            size_t l = (static_cast<size_t>(c->p[1]) << 8) | c->p[2];
            if (!c->Need(3 + l)) return;
            lua_pushlstring(L, reinterpret_cast<const char*>(c->p + 3), l);
            c->Consume(3 + l);
        }
        break;
    case 0xdb:  // str 32
        if (!c->Need(5)) return;
        {
            size_t l = (static_cast<size_t>(c->p[1]) << 24) |
                       (static_cast<size_t>(c->p[2]) << 16) |
                       (static_cast<size_t>(c->p[3]) << 8) |
                        static_cast<size_t>(c->p[4]);
            c->Consume(5);
            if (!c->Need(l)) return;
            lua_pushlstring(L, reinterpret_cast<const char*>(c->p), l);
            c->Consume(l);
        }
        break;
    case 0xdc:  // array 16
        if (!c->Need(3)) return;
        {
            size_t l = (static_cast<size_t>(c->p[1]) << 8) | c->p[2];
            c->Consume(3);
            DecodeToLuaArray(L, c, l);
        }
        break;
    case 0xdd:  // array 32
        if (!c->Need(5)) return;
        {
            size_t l = (static_cast<size_t>(c->p[1]) << 24) |
                       (static_cast<size_t>(c->p[2]) << 16) |
                       (static_cast<size_t>(c->p[3]) << 8) |
                        static_cast<size_t>(c->p[4]);
            c->Consume(5);
            DecodeToLuaArray(L, c, l);
        }
        break;
    case 0xde:  // map 16
        if (!c->Need(3)) return;
        {
            size_t l = (static_cast<size_t>(c->p[1]) << 8) | c->p[2];
            c->Consume(3);
            DecodeToLuaHash(L, c, l);
        }
        break;
    case 0xdf:  // map 32
        if (!c->Need(5)) return;
        {
            size_t l = (static_cast<size_t>(c->p[1]) << 24) |
                       (static_cast<size_t>(c->p[2]) << 16) |
                       (static_cast<size_t>(c->p[3]) << 8) |
                        static_cast<size_t>(c->p[4]);
            c->Consume(5);
            DecodeToLuaHash(L, c, l);
        }
        break;
    default:
        if ((c->p[0] & 0x80) == 0) {
            // positive fixnum
            lua_pushinteger(L, static_cast<lua_Integer>(c->p[0]));
            c->Consume(1);
        } else if ((c->p[0] & 0xe0) == 0xe0) {
            // negative fixnum
            lua_pushinteger(L, static_cast<signed char>(c->p[0]));
            c->Consume(1);
        } else if ((c->p[0] & 0xe0) == 0xa0) {
            // fix raw (fixstr)
            size_t l = c->p[0] & 0x1f;
            if (!c->Need(1 + l)) return;
            lua_pushlstring(L, reinterpret_cast<const char*>(c->p + 1), l);
            c->Consume(1 + l);
        } else if ((c->p[0] & 0xf0) == 0x90) {
            // fix array
            size_t l = c->p[0] & 0xf;
            c->Consume(1);
            DecodeToLuaArray(L, c, l);
        } else if ((c->p[0] & 0xf0) == 0x80) {
            // fix map
            size_t l = c->p[0] & 0xf;
            c->Consume(1);
            DecodeToLuaHash(L, c, l);
        } else {
            c->err = CurError::BadFmt;
        }
    }
}

// ============================================================================
// Common unpack implementation
// ============================================================================

int UnpackFull(lua_State* L, int limit, int offset) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    bool decode_all = (limit == 0 && offset == 0);

    if (offset < 0 || limit < 0) {
        return luaL_error(L,
            "Invalid request to unpack with offset of %d and limit of %d.",
            offset, limit);
    }
    if (static_cast<size_t>(offset) > len) {
        return luaL_error(L,
            "Start offset %d greater than input length %d.",
            offset, static_cast<int>(len));
    }

    if (decode_all) limit = INT_MAX;

    DecodeCursor c(reinterpret_cast<const unsigned char*>(s) + offset,
                   len - static_cast<size_t>(offset));

    int cnt = 0;
    for (; c.left > 0 && cnt < limit; ++cnt) {
        DecodeToLuaType(L, &c);

        if (c.err == CurError::Eof) {
            return luaL_error(L, "Missing bytes in input.");
        }
        if (c.err == CurError::BadFmt) {
            return luaL_error(L, "Bad data format in input.");
        }
    }

    if (!decode_all) {
        int next_offset = static_cast<int>(len - c.left);
        luaL_checkstack(L, 1, "in function UnpackFull");
        lua_pushinteger(L, c.left == 0 ? -1 : next_offset);
        lua_insert(L, 2);
        cnt += 1;
    }

    return cnt;
}

// ============================================================================
// Lua C functions — cmsgpack module
// ============================================================================

int l_msgpack_pack(lua_State* L) {
    int nargs = lua_gettop(L);
    if (nargs == 0) {
        return luaL_argerror(L, 0, "MessagePack pack needs input.");
    }

    EncodeBuf buf;
    for (int i = 1; i <= nargs; ++i) {
        lua_pushvalue(L, i);
        EncodeLuaType(L, buf, 0);
        lua_pushlstring(L, reinterpret_cast<const char*>(buf.Data()), buf.Size());
        buf.Clear();
    }
    lua_concat(L, nargs);
    return 1;
}

int l_msgpack_unpack(lua_State* L) {
    return UnpackFull(L, 0, 0);
}

int l_msgpack_unpack_one(lua_State* L) {
    int offset = static_cast<int>(luaL_optinteger(L, 2, 0));
    lua_pop(L, lua_gettop(L) - 1);
    return UnpackFull(L, 1, offset);
}

int l_msgpack_unpack_limit(lua_State* L) {
    int limit = static_cast<int>(luaL_checkinteger(L, 2));
    int offset = static_cast<int>(luaL_optinteger(L, 3, 0));
    lua_pop(L, lua_gettop(L) - 1);
    return UnpackFull(L, limit, offset);
}

const luaL_Reg kMsgPackFunctions[] = {
    {"pack",         l_msgpack_pack},
    {"unpack",       l_msgpack_unpack},
    {"unpack_one",   l_msgpack_unpack_one},
    {"unpack_limit", l_msgpack_unpack_limit},
    {nullptr, nullptr},
};

// ============================================================================
// Safe wrapper — wraps a function so that errors return (nil, errmsg)
// instead of raising a Lua error.
// ============================================================================

int l_msgpack_safe(lua_State* L) {
    int argc = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));  // push wrapped function
    lua_insert(L, 1);                        // move it before all arguments

    int err = lua_pcall(L, argc, LUA_MULTRET, 0);
    if (err == LUA_OK) {
        return lua_gettop(L);
    }
    // On error the stack has the error message.
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

// ============================================================================
// Module metadata
// ============================================================================

void SetModuleMeta(lua_State* L) {
    lua_pushliteral(L, "cmsgpack");
    lua_setfield(L, -2, "_NAME");
    lua_pushliteral(L, "lua-cmsgpack 0.4.0");
    lua_setfield(L, -2, "_VERSION");
    lua_pushliteral(L, "Copyright (C) 2012, Salvatore Sanfilippo");
    lua_setfield(L, -2, "_COPYRIGHT");
    lua_pushliteral(L, "MessagePack C implementation for Lua");
    lua_setfield(L, -2, "_DESCRIPTION");
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

void ExportMsgPack(ScriptVM& vm) {
    lua_State* L = vm.GetState();
    if (!L) return;

    // --- cmsgpack module ---

    luaL_newlib(L, kMsgPackFunctions);
    SetModuleMeta(L);
    lua_setglobal(L, "cmsgpack");

    // --- cmsgpack_safe module (same functions, each wrapped with pcall) ---

    luaL_newlib(L, kMsgPackFunctions);

    for (const luaL_Reg* r = kMsgPackFunctions; r->name != nullptr; ++r) {
        lua_getfield(L, -1, r->name);           // push original fn
        lua_pushcclosure(L, l_msgpack_safe, 1); // wrap with safe handler
        lua_setfield(L, -2, r->name);           // replace in table
    }

    SetModuleMeta(L);
    lua_setglobal(L, "cmsgpack_safe");

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: msgpack modules exported "
                    "(cmsgpack + cmsgpack_safe: pack/unpack/unpack_one/unpack_limit)");
}

} // namespace script
} // namespace engine
