#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_bson_vector.h"

#include <limits>
#include <new>
#include <vector>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bson_vector.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kVi8cMeta = "bson.vector_int8_const";

bool IsRangeInside(size_t length, size_t count, size_t offset) {
	return offset <= length && count <= length - offset;
}

int l_vi8c_gc(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8ConstView>(L, 1, kVi8cMeta);
	CLOUDENGINE_MEM_DELETE(v);
	*CheckUserdata<mongo::BsonVectorInt8ConstView>(L, 1, kVi8cMeta) = nullptr;
	return 0;
}

int l_vi8c_new(lua_State* L) {
	auto* v = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorInt8ConstView);
	if (!v) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorInt8ConstView>(L, kVi8cMeta);
	*ud = v;
	return 1;
}

int l_vi8c_init(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8ConstView>(L, 1, kVi8cMeta);
	size_t len;
	const uint8_t* data = reinterpret_cast<const uint8_t*>(luaL_checklstring(L, 2, &len));
	if (len > (std::numeric_limits<uint32_t>::max)()) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, v && v->Init(data, static_cast<uint32_t>(len)));
	return 1;
}

int l_vi8c_from_iter(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8ConstView>(L, 1, kVi8cMeta);
	auto* iter = GetUserdata<mongo::BsonIter>(L, 2, "bson.iter");
	lua_pushboolean(L, v && iter && v->FromIter(*iter));
	return 1;
}

int l_vi8c_length(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8ConstView>(L, 1, kVi8cMeta);
	lua_pushinteger(L, v ? v->Length() : 0);
	return 1;
}

int l_vi8c_read(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8ConstView>(L, 1, kVi8cMeta);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	if (!IsRangeInside(v->Length(), count, offset)) {
		lua_pushnil(L);
		return 1;
	}
	std::vector<int8_t> buf(count);
	if (v->Read(buf.data(), count, offset)) {
		lua_pushlstring(L, reinterpret_cast<const char*>(buf.data()), count);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_vi8c_binary_data_len(lua_State* L) {
	auto count = CheckIntegerArg<size_t>(L, 1);
	lua_pushinteger(L, mongo::BsonVectorInt8ConstView::BinaryDataLength(count));
	return 1;
}

int l_vi8c_destroy(lua_State* L) {
	l_vi8c_gc(L);
	return 0;
}

const luaL_Reg kVi8cLib[] = {
	{"vector_int8_const_new", l_vi8c_new},
	{"vector_int8_const_destroy", l_vi8c_destroy},
	{"vector_int8_const_init", l_vi8c_init},
	{"vector_int8_const_from_iter", l_vi8c_from_iter},
	{"vector_int8_const_length", l_vi8c_length},
	{"vector_int8_const_read", l_vi8c_read},
	{"vector_int8_const_binary_data_len", l_vi8c_binary_data_len},
	{nullptr, nullptr},
};

const char* kVi8Meta = "bson.vector_int8";

int l_vi8_gc(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8View>(L, 1, kVi8Meta);
	CLOUDENGINE_MEM_DELETE(v);
	*CheckUserdata<mongo::BsonVectorInt8View>(L, 1, kVi8Meta) = nullptr;
	return 0;
}

int l_vi8_new(lua_State* L) {
	auto* v = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorInt8View);
	if (!v) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorInt8View>(L, kVi8Meta);
	*ud = v;
	return 1;
}

int l_vi8_init(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8View>(L, 1, kVi8Meta);
	size_t len;
	uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(luaL_checklstring(L, 2, &len)));
	if (len > (std::numeric_limits<uint32_t>::max)()) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, v && v->Init(data, static_cast<uint32_t>(len)));
	return 1;
}

int l_vi8_from_iter(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8View>(L, 1, kVi8Meta);
	auto* iter = GetUserdata<mongo::BsonIter>(L, 2, "bson.iter");
	lua_pushboolean(L, v && iter && v->FromIter(*iter));
	return 1;
}

int l_vi8_length(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8View>(L, 1, kVi8Meta);
	lua_pushinteger(L, v ? v->Length() : 0);
	return 1;
}

int l_vi8_read(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8View>(L, 1, kVi8Meta);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	if (!IsRangeInside(v->Length(), count, offset)) {
		lua_pushnil(L);
		return 1;
	}
	std::vector<int8_t> buf(count);
	if (v->Read(buf.data(), count, offset)) {
		lua_pushlstring(L, reinterpret_cast<const char*>(buf.data()), count);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_vi8_write(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8View>(L, 1, kVi8Meta);
	size_t len;
	const int8_t* data = reinterpret_cast<const int8_t*>(luaL_checklstring(L, 2, &len));
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	lua_pushboolean(L, v && v->Write(data, len, offset));
	return 1;
}

int l_vi8_as_const(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorInt8View>(L, 1, kVi8Meta);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	auto* cv = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorInt8ConstView, v->AsConst());
	if (!cv) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorInt8ConstView>(L, kVi8cMeta);
	*ud = cv;
	return 1;
}

int l_vi8_binary_data_len(lua_State* L) {
	auto count = CheckIntegerArg<size_t>(L, 1);
	lua_pushinteger(L, mongo::BsonVectorInt8View::BinaryDataLength(count));
	return 1;
}

int l_vi8_destroy(lua_State* L) {
	l_vi8_gc(L);
	return 0;
}

const luaL_Reg kVi8Lib[] = {
	{"vector_int8_new", l_vi8_new},
	{"vector_int8_destroy", l_vi8_destroy},
	{"vector_int8_init", l_vi8_init},
	{"vector_int8_from_iter", l_vi8_from_iter},
	{"vector_int8_length", l_vi8_length},
	{"vector_int8_read", l_vi8_read},
	{"vector_int8_write", l_vi8_write},
	{"vector_int8_as_const", l_vi8_as_const},
	{"vector_int8_binary_data_len", l_vi8_binary_data_len},
	{nullptr, nullptr},
};

const char* kVf32cMeta = "bson.vector_float32_const";

int l_vf32c_gc(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32ConstView>(L, 1, kVf32cMeta);
	CLOUDENGINE_MEM_DELETE(v);
	*CheckUserdata<mongo::BsonVectorFloat32ConstView>(L, 1, kVf32cMeta) = nullptr;
	return 0;
}

int l_vf32c_new(lua_State* L) {
	auto* v = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorFloat32ConstView);
	if (!v) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorFloat32ConstView>(L, kVf32cMeta);
	*ud = v;
	return 1;
}

int l_vf32c_init(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32ConstView>(L, 1, kVf32cMeta);
	size_t len;
	const uint8_t* data = reinterpret_cast<const uint8_t*>(luaL_checklstring(L, 2, &len));
	if (len > (std::numeric_limits<uint32_t>::max)()) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, v && v->Init(data, static_cast<uint32_t>(len)));
	return 1;
}

int l_vf32c_from_iter(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32ConstView>(L, 1, kVf32cMeta);
	auto* iter = GetUserdata<mongo::BsonIter>(L, 2, "bson.iter");
	lua_pushboolean(L, v && iter && v->FromIter(*iter));
	return 1;
}

int l_vf32c_length(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32ConstView>(L, 1, kVf32cMeta);
	lua_pushinteger(L, v ? v->Length() : 0);
	return 1;
}

int l_vf32c_read(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32ConstView>(L, 1, kVf32cMeta);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	if (!IsRangeInside(v->Length(), count, offset) ||
		count > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		lua_pushnil(L);
		return 1;
	}
	std::vector<float> buf(count);
	if (v->Read(buf.data(), count, offset)) {
		lua_createtable(L, (int) count, 0);
		for (size_t i = 0; i < count; ++i) {
			lua_pushnumber(L, buf[i]);
			lua_rawseti(L, -2, (int) i + 1);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_vf32c_binary_data_len(lua_State* L) {
	auto count = CheckIntegerArg<size_t>(L, 1);
	lua_pushinteger(L, mongo::BsonVectorFloat32ConstView::BinaryDataLength(count));
	return 1;
}

int l_vf32c_destroy(lua_State* L) {
	l_vf32c_gc(L);
	return 0;
}

const luaL_Reg kVf32cLib[] = {
	{"vector_float32_const_new", l_vf32c_new},
	{"vector_float32_const_destroy", l_vf32c_destroy},
	{"vector_float32_const_init", l_vf32c_init},
	{"vector_float32_const_from_iter", l_vf32c_from_iter},
	{"vector_float32_const_length", l_vf32c_length},
	{"vector_float32_const_read", l_vf32c_read},
	{"vector_float32_const_binary_data_len", l_vf32c_binary_data_len},
	{nullptr, nullptr},
};

const char* kVf32Meta = "bson.vector_float32";

int l_vf32_gc(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32View>(L, 1, kVf32Meta);
	CLOUDENGINE_MEM_DELETE(v);
	*CheckUserdata<mongo::BsonVectorFloat32View>(L, 1, kVf32Meta) = nullptr;
	return 0;
}

int l_vf32_new(lua_State* L) {
	auto* v = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorFloat32View);
	if (!v) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorFloat32View>(L, kVf32Meta);
	*ud = v;
	return 1;
}

int l_vf32_init(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32View>(L, 1, kVf32Meta);
	size_t len;
	uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(luaL_checklstring(L, 2, &len)));
	if (len > (std::numeric_limits<uint32_t>::max)()) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, v && v->Init(data, static_cast<uint32_t>(len)));
	return 1;
}

int l_vf32_from_iter(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32View>(L, 1, kVf32Meta);
	auto* iter = GetUserdata<mongo::BsonIter>(L, 2, "bson.iter");
	lua_pushboolean(L, v && iter && v->FromIter(*iter));
	return 1;
}

int l_vf32_length(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32View>(L, 1, kVf32Meta);
	lua_pushinteger(L, v ? v->Length() : 0);
	return 1;
}

int l_vf32_read(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32View>(L, 1, kVf32Meta);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	if (!IsRangeInside(v->Length(), count, offset) ||
		count > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		lua_pushnil(L);
		return 1;
	}
	std::vector<float> buf(count);
	if (v->Read(buf.data(), count, offset)) {
		lua_createtable(L, (int) count, 0);
		for (size_t i = 0; i < count; ++i) {
			lua_pushnumber(L, buf[i]);
			lua_rawseti(L, -2, (int) i + 1);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_vf32_write(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32View>(L, 1, kVf32Meta);
	if (!lua_istable(L, 2) || !v) {
		lua_pushboolean(L, false);
		return 1;
	}
	auto count = static_cast<size_t>(CheckLengthArg<int>(L, 2));
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	std::vector<float> buf(count);
	for (size_t i = 0; i < count; ++i) {
		lua_rawgeti(L, 2, (int) i + 1);
		buf[i] = static_cast<float>(luaL_checknumber(L, -1));
		lua_pop(L, 1);
	}
	lua_pushboolean(L, v->Write(buf.data(), count, offset));
	return 1;
}

int l_vf32_as_const(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorFloat32View>(L, 1, kVf32Meta);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	auto* cv = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorFloat32ConstView, v->AsConst());
	if (!cv) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorFloat32ConstView>(L, kVf32cMeta);
	*ud = cv;
	return 1;
}

int l_vf32_binary_data_len(lua_State* L) {
	auto count = CheckIntegerArg<size_t>(L, 1);
	lua_pushinteger(L, mongo::BsonVectorFloat32View::BinaryDataLength(count));
	return 1;
}

int l_vf32_destroy(lua_State* L) {
	l_vf32_gc(L);
	return 0;
}

const luaL_Reg kVf32Lib[] = {
	{"vector_float32_new", l_vf32_new},
	{"vector_float32_destroy", l_vf32_destroy},
	{"vector_float32_init", l_vf32_init},
	{"vector_float32_from_iter", l_vf32_from_iter},
	{"vector_float32_length", l_vf32_length},
	{"vector_float32_read", l_vf32_read},
	{"vector_float32_write", l_vf32_write},
	{"vector_float32_as_const", l_vf32_as_const},
	{"vector_float32_binary_data_len", l_vf32_binary_data_len},
	{nullptr, nullptr},
};

const char* kVpbMeta = "bson.vector_packedbit_const";

int l_vpb_gc(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta);
	CLOUDENGINE_MEM_DELETE(v);
	*CheckUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta) = nullptr;
	return 0;
}

int l_vpb_new(lua_State* L) {
	auto* v = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorPackedBitConstView);
	if (!v) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorPackedBitConstView>(L, kVpbMeta);
	*ud = v;
	return 1;
}

int l_vpb_init(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta);
	size_t len;
	const uint8_t* data = reinterpret_cast<const uint8_t*>(luaL_checklstring(L, 2, &len));
	if (len > (std::numeric_limits<uint32_t>::max)()) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, v && v->Init(data, static_cast<uint32_t>(len)));
	return 1;
}

int l_vpb_from_iter(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta);
	auto* iter = GetUserdata<mongo::BsonIter>(L, 2, "bson.iter");
	lua_pushboolean(L, v && iter && v->FromIter(*iter));
	return 1;
}

int l_vpb_length(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta);
	lua_pushinteger(L, v ? v->Length() : 0);
	return 1;
}

int l_vpb_length_bytes(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta);
	lua_pushinteger(L, v ? v->LengthBytes() : 0);
	return 1;
}

int l_vpb_padding(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta);
	lua_pushinteger(L, v ? v->Padding() : 0);
	return 1;
}

int l_vpb_read_packed(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	if (!IsRangeInside(v->LengthBytes(), count, offset)) {
		lua_pushnil(L);
		return 1;
	}
	std::vector<uint8_t> buf(count);
	if (v->ReadPacked(buf.data(), count, offset)) {
		lua_pushlstring(L, reinterpret_cast<const char*>(buf.data()), count);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_vpb_unpack_bool(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitConstView>(L, 1, kVpbMeta);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	if (!IsRangeInside(v->Length(), count, offset) ||
		count > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		lua_pushnil(L);
		return 1;
	}
	auto* buf = CLOUDENGINE_MEM_NEW_ARR_NOTHROW(bool, count);
	if (!buf) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	if (v->UnpackBool(buf, count, offset)) {
		lua_createtable(L, (int) count, 0);
		for (size_t i = 0; i < count; ++i) {
			lua_pushboolean(L, buf[i]);
			lua_rawseti(L, -2, (int) i + 1);
		}
	} else {
		lua_pushnil(L);
	}
	CLOUDENGINE_MEM_DELETE_ARR(buf);
	return 1;
}

int l_vpb_binary_data_len(lua_State* L) {
	auto count = CheckIntegerArg<size_t>(L, 1);
	lua_pushinteger(L, mongo::BsonVectorPackedBitConstView::BinaryDataLength(count));
	return 1;
}

int l_vpb_destroy(lua_State* L) {
	l_vpb_gc(L);
	return 0;
}

const luaL_Reg kVpbLib[] = {
	{"vector_packedbit_const_new", l_vpb_new},
	{"vector_packedbit_const_destroy", l_vpb_destroy},
	{"vector_packedbit_const_init", l_vpb_init},
	{"vector_packedbit_const_from_iter", l_vpb_from_iter},
	{"vector_packedbit_const_length", l_vpb_length},
	{"vector_packedbit_const_length_bytes", l_vpb_length_bytes},
	{"vector_packedbit_const_padding", l_vpb_padding},
	{"vector_packedbit_const_read_packed", l_vpb_read_packed},
	{"vector_packedbit_const_unpack_bool", l_vpb_unpack_bool},
	{"vector_packedbit_const_binary_data_len", l_vpb_binary_data_len},
	{nullptr, nullptr},
};

const char* kVpbwMeta = "bson.vector_packedbit";

int l_vpbw_gc(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	CLOUDENGINE_MEM_DELETE(v);
	*CheckUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta) = nullptr;
	return 0;
}

int l_vpbw_new(lua_State* L) {
	auto* v = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorPackedBitView);
	if (!v) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorPackedBitView>(L, kVpbwMeta);
	*ud = v;
	return 1;
}

int l_vpbw_init(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	size_t len;
	uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(luaL_checklstring(L, 2, &len)));
	if (len > (std::numeric_limits<uint32_t>::max)()) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, v && v->Init(data, static_cast<uint32_t>(len)));
	return 1;
}

int l_vpbw_from_iter(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	auto* iter = GetUserdata<mongo::BsonIter>(L, 2, "bson.iter");
	lua_pushboolean(L, v && iter && v->FromIter(*iter));
	return 1;
}

int l_vpbw_length(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	lua_pushinteger(L, v ? v->Length() : 0);
	return 1;
}

int l_vpbw_length_bytes(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	lua_pushinteger(L, v ? v->LengthBytes() : 0);
	return 1;
}

int l_vpbw_padding(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	lua_pushinteger(L, v ? v->Padding() : 0);
	return 1;
}

int l_vpbw_read_packed(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	if (!IsRangeInside(v->LengthBytes(), count, offset)) {
		lua_pushnil(L);
		return 1;
	}
	std::vector<uint8_t> buf(count);
	if (v->ReadPacked(buf.data(), count, offset)) {
		lua_pushlstring(L, reinterpret_cast<const char*>(buf.data()), count);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_vpbw_unpack_bool(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	if (!IsRangeInside(v->Length(), count, offset) ||
		count > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		lua_pushnil(L);
		return 1;
	}
	auto* buf = CLOUDENGINE_MEM_NEW_ARR_NOTHROW(bool, count);
	if (!buf) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	if (v->UnpackBool(buf, count, offset)) {
		lua_createtable(L, (int) count, 0);
		for (size_t i = 0; i < count; ++i) {
			lua_pushboolean(L, buf[i]);
			lua_rawseti(L, -2, (int) i + 1);
		}
	} else {
		lua_pushnil(L);
	}
	CLOUDENGINE_MEM_DELETE_ARR(buf);
	return 1;
}

int l_vpbw_write_packed(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	size_t len;
	const uint8_t* data = reinterpret_cast<const uint8_t*>(luaL_checklstring(L, 2, &len));
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	lua_pushboolean(L, v && v->WritePacked(data, len, offset));
	return 1;
}

int l_vpbw_pack_bool(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	if (!lua_istable(L, 2) || !v) {
		lua_pushboolean(L, false);
		return 1;
	}
	auto count = static_cast<size_t>(CheckLengthArg<int>(L, 2));
	auto offset = OptIntegerArg<size_t>(L, 3, 0);
	auto* buf = CLOUDENGINE_MEM_NEW_ARR_NOTHROW(bool, count);
	if (!buf) {
		lua_pushboolean(L, false);
		return 1;
	}
	for (size_t i = 0; i < count; ++i) {
		lua_rawgeti(L, 2, (int) i + 1);
		buf[i] = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
	}
	lua_pushboolean(L, v->PackBool(buf, count, offset));
	CLOUDENGINE_MEM_DELETE_ARR(buf);
	return 1;
}

int l_vpbw_as_const(lua_State* L) {
	auto* v = GetUserdata<mongo::BsonVectorPackedBitView>(L, 1, kVpbwMeta);
	if (!v) {
		lua_pushnil(L);
		return 1;
	}
	auto* cv = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonVectorPackedBitConstView, v->AsConst());
	if (!cv) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonVectorPackedBitConstView>(L, kVpbMeta);
	*ud = cv;
	return 1;
}

int l_vpbw_binary_data_len(lua_State* L) {
	auto count = CheckIntegerArg<size_t>(L, 1);
	lua_pushinteger(L, mongo::BsonVectorPackedBitView::BinaryDataLength(count));
	return 1;
}

int l_vpbw_destroy(lua_State* L) {
	l_vpbw_gc(L);
	return 0;
}

const luaL_Reg kVpbwLib[] = {
	{"vector_packedbit_new", l_vpbw_new},
	{"vector_packedbit_destroy", l_vpbw_destroy},
	{"vector_packedbit_init", l_vpbw_init},
	{"vector_packedbit_from_iter", l_vpbw_from_iter},
	{"vector_packedbit_length", l_vpbw_length},
	{"vector_packedbit_length_bytes", l_vpbw_length_bytes},
	{"vector_packedbit_padding", l_vpbw_padding},
	{"vector_packedbit_read_packed", l_vpbw_read_packed},
	{"vector_packedbit_unpack_bool", l_vpbw_unpack_bool},
	{"vector_packedbit_write_packed", l_vpbw_write_packed},
	{"vector_packedbit_pack_bool", l_vpbw_pack_bool},
	{"vector_packedbit_as_const", l_vpbw_as_const},
	{"vector_packedbit_binary_data_len", l_vpbw_binary_data_len},
	{nullptr, nullptr},
};

}  // namespace

void RegisterBsonVectorInt8ConstViewMeta(lua_State* L) {
	RegisterMetatable(L, kVi8cMeta, nullptr, l_vi8c_gc);
}
void RegisterBsonVectorInt8ViewMeta(lua_State* L) {
	RegisterMetatable(L, kVi8Meta, nullptr, l_vi8_gc);
}
void RegisterBsonVectorFloat32ConstViewMeta(lua_State* L) {
	RegisterMetatable(L, kVf32cMeta, nullptr, l_vf32c_gc);
}
void RegisterBsonVectorFloat32ViewMeta(lua_State* L) {
	RegisterMetatable(L, kVf32Meta, nullptr, l_vf32_gc);
}
void RegisterBsonVectorPackedBitConstViewMeta(lua_State* L) {
	RegisterMetatable(L, kVpbMeta, nullptr, l_vpb_gc);
}
void RegisterBsonVectorPackedBitViewMeta(lua_State* L) {
	RegisterMetatable(L, kVpbwMeta, nullptr, l_vpbw_gc);
}

const luaL_Reg* GetBsonVectorInt8ConstViewLib() {
	return kVi8cLib;
}
const luaL_Reg* GetBsonVectorInt8ViewLib() {
	return kVi8Lib;
}
const luaL_Reg* GetBsonVectorFloat32ConstViewLib() {
	return kVf32cLib;
}
const luaL_Reg* GetBsonVectorFloat32ViewLib() {
	return kVf32Lib;
}
const luaL_Reg* GetBsonVectorPackedBitConstViewLib() {
	return kVpbLib;
}
const luaL_Reg* GetBsonVectorPackedBitViewLib() {
	return kVpbwLib;
}

}  // namespace script
}  // namespace engine

#endif
