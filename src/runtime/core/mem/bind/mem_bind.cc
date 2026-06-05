#include "runtime/core/mem/bind/mem_bind.h"

#ifdef ENGINE_MEM_STATS_ENABLED

#include "runtime/core/mem/mem_stats.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

void PushU64Field(lua_State* L, const char* key, uint64_t val) {
	lua_pushstring(L, key);
	lua_pushinteger(L, static_cast<lua_Integer>(val));
	lua_settable(L, -3);
}

void PushOpCounters(lua_State* L, uint64_t alloc_count, uint64_t free_count,
                    uint64_t alloc_bytes, uint64_t free_bytes) {
	lua_newtable(L);
	PushU64Field(L, "alloc_count", alloc_count);
	PushU64Field(L, "free_count", free_count);
	PushU64Field(L, "alloc_bytes", alloc_bytes);
	PushU64Field(L, "free_bytes", free_bytes);
}

void PushBucket(lua_State* L, uint64_t count, uint64_t bytes) {
	lua_newtable(L);
	PushU64Field(L, "count", count);
	PushU64Field(L, "bytes", bytes);
}

// ── mem.is_enabled() → bool ──────────────────────────────────────────────

int l_mem_is_enabled(lua_State* L) {
	lua_pushboolean(L, 1);
	return 1;
}

// ── mem.get_stats() → table ──────────────────────────────────────────────

int l_mem_get_stats(lua_State* L) {
	auto snap = engine::mem::MemStats::Instance().Snapshot();

	lua_newtable(L);

	// ── totals sub-table ─────────────────────────────────────────────
	lua_pushstring(L, "totals");
	lua_newtable(L);
	PushU64Field(L, "alloc_count", snap.total_alloc_count);
	PushU64Field(L, "free_count", snap.total_free_count);
	PushU64Field(L, "alloc_bytes", snap.total_alloc_bytes);
	PushU64Field(L, "free_bytes", snap.total_free_bytes);
	PushU64Field(L, "bytes_in_use", snap.current_bytes);
	PushU64Field(L, "peak_bytes", snap.peak_bytes);
	PushU64Field(L, "peak_alloc_count", snap.peak_alloc_count);
	lua_settable(L, -3);

	// ── by_operation sub-table ───────────────────────────────────────
	lua_pushstring(L, "by_operation");
	lua_newtable(L);
	for (int i = 0; i < 9; ++i) {
		if (snap.op_alloc_count[i] == 0 && snap.op_free_count[i] == 0) continue;
		const char* name = engine::mem::MemStats::Instance().OpName(
		    static_cast<engine::mem::MemOp>(i));
		// Use internal helper — MemStats isn't fully visible, so pass via pre-computed
		// Actually let me restructure slightly. Just use the snap directly.
		PushOpCounters(L, snap.op_alloc_count[i], snap.op_free_count[i],
		               snap.op_alloc_bytes[i], snap.op_free_bytes[i]);
		lua_setfield(L, -2, name);
	}
	lua_settable(L, -3);

	// ── size_buckets sub-table ───────────────────────────────────────
	lua_pushstring(L, "size_buckets");
	lua_newtable(L);
	const char* bucket_names[] = {"lt_64", "64_256", "256_1k", "1k_4k", "4k_64k", "gt_64k"};
	for (int i = 0; i < 6; ++i) {
		PushBucket(L, snap.bucket_alloc_count[i], snap.bucket_alloc_bytes[i]);
		lua_setfield(L, -2, bucket_names[i]);
	}
	lua_settable(L, -3);

	// ── recent_allocs array ──────────────────────────────────────────
	lua_pushstring(L, "recent_allocs");
	lua_newtable(L);
	int idx = 1;
	for (const auto& entry : snap.recent_allocs) {
		lua_newtable(L);
		if (entry.file) {
			lua_pushstring(L, entry.file);
		} else {
			lua_pushstring(L, "?");
		}
		lua_setfield(L, -2, "file");
		lua_pushinteger(L, entry.line);
		lua_setfield(L, -2, "line");
		lua_pushinteger(L, static_cast<lua_Integer>(entry.size));
		lua_setfield(L, -2, "size");
		lua_pushstring(L, engine::mem::MemStats::Instance().OpName(entry.op));
		lua_setfield(L, -2, "op");

		lua_rawseti(L, -2, idx++);
	}
	lua_settable(L, -3);

	// ── scalar fields ────────────────────────────────────────────────
	PushU64Field(L, "active_alloc_count", snap.active_alloc_count);

	return 1;
}

// ── mem.reset_stats() → nil ──────────────────────────────────────────────

int l_mem_reset_stats(lua_State* L) {
	engine::mem::MemStats::Instance().Reset();
	return 0;
}

// ── mem.dump_stats(filepath) → bool ──────────────────────────────────────

int l_mem_dump_stats(lua_State* L) {
	const char* path = luaL_checkstring(L, 1);
	bool ok = engine::mem::MemStats::Instance().DumpToFile(path);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

const luaL_Reg kMemFunctions[] = {
	{"is_enabled",  l_mem_is_enabled},
	{"get_stats",   l_mem_get_stats},
	{"reset_stats", l_mem_reset_stats},
	{"dump_stats",  l_mem_dump_stats},
	{nullptr, nullptr},
};

}  // namespace

void ExportMem(ScriptVM& vm) {
	vm.RegisterModule("mem", kMemFunctions);
}

}  // namespace script
}  // namespace engine

#endif  // ENGINE_MEM_STATS_ENABLED
