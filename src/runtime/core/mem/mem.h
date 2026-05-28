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
