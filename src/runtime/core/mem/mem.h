#pragma once

#include <cstdlib>   // malloc, free, calloc, realloc
#include <new>       // std::nothrow

// ═══════════════════════════════════════════════════════════════════════════
// Memory allocation wrapper macros
// ═══════════════════════════════════════════════════════════════════════════
//
// Replace all raw new / delete / malloc / free usage with these macros
// so a custom memory-management module can be plugged in later.
//
// std::make_unique / std::make_shared remain the preferred smart-pointer
// pattern and are not wrapped – they already abstract allocation internally.
//
// When ENGINE_MEM_STATS_ENABLED is defined, every allocation / deallocation
// is recorded by the MemStats singleton (mem_stats.h), providing per-call-site
// statistics, size-distribution buckets, and JSON export to file.

#ifdef ENGINE_MEM_STATS_ENABLED
#include "runtime/core/mem/mem_stats.h"

// ── Object allocation (new / delete) ────────────────────────────────────

#define MEM_NEW(T, ...)                                                        \
	([]() -> T* {                                                              \
		auto* _p = new T(__VA_ARGS__);                                         \
		engine::mem::MemStats::Instance().RecordAlloc(                          \
		    static_cast<void*>(_p), sizeof(T), engine::mem::MemOp::kNew,       \
		    __FILE__, __LINE__);                                               \
		return _p;                                                             \
	}())

#define MEM_DELETE(ptr)                                                        \
	do {                                                                       \
		auto* _p = (ptr);                                                      \
		engine::mem::MemStats::Instance().RecordFree(                          \
		    static_cast<void*>(_p), engine::mem::MemOp::kDelete);              \
		delete _p;                                                             \
	} while (0)

// ── Array allocation (new[] / delete[]) ─────────────────────────────────

#define MEM_NEW_ARR(T, n)                                                      \
	([&]() -> T* {                                                             \
		size_t _count = (n);                                                   \
		auto* _p = new T[_count];                                              \
		engine::mem::MemStats::Instance().RecordAlloc(                          \
		    static_cast<void*>(_p), sizeof(T) * _count,                        \
		    engine::mem::MemOp::kNewArr, __FILE__, __LINE__);                  \
		return _p;                                                             \
	}())

#define MEM_DELETE_ARR(ptr)                                                    \
	do {                                                                       \
		auto* _p = (ptr);                                                      \
		engine::mem::MemStats::Instance().RecordFree(                          \
		    static_cast<void*>(_p), engine::mem::MemOp::kDeleteArr);           \
		delete[] _p;                                                           \
	} while (0)

// ── C-style allocation (malloc / free / calloc / realloc) ───────────────

#define MEM_MALLOC(size)                                                       \
	([&]() -> void* {                                                          \
		size_t _sz = (size);                                                   \
		void* _p = malloc(_sz);                                                \
		engine::mem::MemStats::Instance().RecordAlloc(                          \
		    _p, _sz, engine::mem::MemOp::kMalloc, __FILE__, __LINE__);          \
		return _p;                                                             \
	}())

#define MEM_FREE(ptr)                                                          \
	do {                                                                       \
		void* _p = (ptr);                                                      \
		engine::mem::MemStats::Instance().RecordFree(                          \
		    _p, engine::mem::MemOp::kFree);                                    \
		free(_p);                                                              \
	} while (0)

#define MEM_CALLOC(n, size)                                                    \
	([&]() -> void* {                                                          \
		size_t _n = (n);                                                       \
		size_t _sz = (size);                                                   \
		void* _p = calloc(_n, _sz);                                            \
		engine::mem::MemStats::Instance().RecordAlloc(                          \
		    _p, _n * _sz, engine::mem::MemOp::kCalloc, __FILE__, __LINE__);    \
		return _p;                                                             \
	}())

#define MEM_REALLOC(ptr, size)                                                 \
	([&]() -> void* {                                                          \
		void* _old = (ptr);                                                    \
		size_t _sz = (size);                                                   \
		void* _p = realloc(_old, _sz);                                         \
		engine::mem::MemStats::Instance().RecordRealloc(                       \
		    _old, _p, _sz, __FILE__, __LINE__);                                \
		return _p;                                                             \
	}())

// ── Nothrow variants ────────────────────────────────────────────────────

#define MEM_NEW_NOTHROW(T, ...)                                                \
	([]() -> T* {                                                              \
		auto* _p = new (std::nothrow) T(__VA_ARGS__);                          \
		engine::mem::MemStats::Instance().RecordAlloc(                          \
		    static_cast<void*>(_p), sizeof(T),                                  \
		    engine::mem::MemOp::kNewNothrow, __FILE__, __LINE__);              \
		return _p;                                                             \
	}())

#define MEM_NEW_ARR_NOTHROW(T, n)                                              \
	([&]() -> T* {                                                             \
		size_t _count = (n);                                                   \
		auto* _p = new (std::nothrow) T[_count];                               \
		engine::mem::MemStats::Instance().RecordAlloc(                          \
		    static_cast<void*>(_p), sizeof(T) * _count,                         \
		    engine::mem::MemOp::kNewArrNothrow, __FILE__, __LINE__);           \
		return _p;                                                             \
	}())

#else  // ENGINE_MEM_STATS_ENABLED not defined — passthrough

// ── Object allocation (new / delete) ────────────────────────────────────

#define MEM_NEW(T, ...)              new T(__VA_ARGS__)
#define MEM_DELETE(ptr)              delete (ptr)

// ── Array allocation (new[] / delete[]) ─────────────────────────────────

#define MEM_NEW_ARR(T, n)            new T[n]
#define MEM_DELETE_ARR(ptr)          delete[] (ptr)

// ── C-style allocation (malloc / free / calloc / realloc) ───────────────

#define MEM_MALLOC(size)             malloc(size)
#define MEM_FREE(ptr)                free(ptr)
#define MEM_CALLOC(n, size)          calloc((n), (size))
#define MEM_REALLOC(ptr, size)       realloc((ptr), (size))

// ── Nothrow variants (return nullptr instead of throwing) ───────────────

#define MEM_NEW_NOTHROW(T, ...)      new (std::nothrow) T(__VA_ARGS__)
#define MEM_NEW_ARR_NOTHROW(T, n)    new (std::nothrow) T[n]

#endif  // ENGINE_MEM_STATS_ENABLED
