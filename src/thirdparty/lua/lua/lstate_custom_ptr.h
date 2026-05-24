/*
** $Id: lstate_custom_ptr.h $
** Lua State Custom Pointer Array API
** See Copyright Notice in lua.h
*/

#ifndef lstate_custom_ptr_h
#define lstate_custom_ptr_h

#include "lua.h"

/*
** ===========================================================================
** Custom pointer array stored in global_State.
** ===========================================================================
**
** Each lua_State (and all its coroutines) share a global_State which now
** contains a dynamic void* array.  Host applications can use this array to
** attach arbitrary C/C++ objects (physics worlds, network sessions, timer
** heaps, etc.) to a Lua VM without going through the Lua stack or the
** registry table.
**
** This array is NOT garbage-collected.  Pointers stored here are opaque to
** Lua — the host is responsible for their lifetime.  When the Lua state is
** closed every pointer is simply dropped (the array memory is freed); no
** destructor or finalizer is called on the stored values.
**
** Indices are 1‑based, matching the Lua stack convention.  A negative index
** counts backward from the end: -1 is the last element, -2 the second-to-last,
** etc.  Index 0 is always out of range.
**
** The array grows automatically on insert (doubling from an initial capacity
** of 4).  It never shrinks automatically; use lua_resizecustomptrs() to trim
** or release capacity explicitly.
**
** All functions that mutate the array acquire the Lua state lock
** (lua_lock / lua_unlock) so they are safe to call from any thread that
** holds the state, but concurrent callers must still serialise externally
** unless LUAI_THREADSAFE is defined.
*/


/*
** ---------------------------------------------------------------------------
** lua_setcustomptr(L, index, ptr)
** ---------------------------------------------------------------------------
**
** Store 'ptr' at the given 1‑based index.
**
** If 'index' is within [1, len] the existing slot is overwritten.
** If 'index' is exactly len+1 this is equivalent to lua_pushcustomptr() —
**   the array grows by one and 'ptr' is appended.
** If 'index' is greater than len+1 the array is first extended: all
**   intermediate slots between the old length and the new index are
**   filled with NULL, then 'ptr' is placed at 'index'.
**   This may trigger a capacity reallocation.
** If 'index' is negative it is resolved relative to the end as
**   len + index + 1.  For example, -1 overwrites the last element.
** If 'index' is 0, or the resolved position is before the first element,
**   the call is a silent no-op.
**
** Side effects:
**   - May reallocate the internal array (capacity doubles as needed).
**   - May increase the logical length (fills gaps with NULL).
**   - Does NOT change the positions of existing non-NULL entries when
**     overwriting; only an out-of-bounds positive index shifts the length.
*/
LUA_API void (lua_setcustomptr) (lua_State *L, int index, void *ptr);


/*
** ---------------------------------------------------------------------------
** lua_getcustomptr(L, index)
** ---------------------------------------------------------------------------
**
** Return the pointer stored at 'index' (1‑based, negative counts from the
** end).  Returns NULL when the index is out of range or when the slot
** genuinely stores NULL — the caller must track which indices are valid
** if NULL is a legitimate stored value.
**
** Side effects: none (read-only, no allocation).
*/
LUA_API void *(lua_getcustomptr) (lua_State *L, int index);


/*
** ---------------------------------------------------------------------------
** lua_pushcustomptr(L, ptr)
** ---------------------------------------------------------------------------
**
** Append 'ptr' to the end of the array.  Returns the new 1‑based index
** on success, or 0 if a capacity reallocation failed (allocation error).
**
** This is the primary way to add a pointer when the caller does not care
** about the exact position.  The returned index can be saved and later
** used with lua_getcustomptr / lua_setcustomptr / lua_removecustomptr.
**
** Side effects:
**   - len increased by 1.
**   - May reallocate the internal array (capacity doubles when full).
**   - Existing pointers keep their indices.
*/
LUA_API int (lua_pushcustomptr) (lua_State *L, void *ptr);


/*
** ---------------------------------------------------------------------------
** lua_removecustomptr(L, index)
** ---------------------------------------------------------------------------
**
** Remove the pointer at 'index' (1‑based, negative allowed).
** All elements above the removed slot shift down by one position, so
** indices of those elements decrease by 1.
**
** Returns 1 on success, 0 if 'index' is out of range (including 0).
**
** Side effects:
**   - len decreased by 1.
**   - Elements at higher indices move (index shift).
**   - Capacity is NOT reduced — the array never shrinks automatically.
**   - No destructor/finalizer is called on the removed pointer.
*/
LUA_API int (lua_removecustomptr) (lua_State *L, int index);


/*
** ---------------------------------------------------------------------------
** lua_customptrlen(L)
** ---------------------------------------------------------------------------
**
** Return the current number of stored pointers (logical length, not capacity).
**
** Side effects: none.
*/
LUA_API int (lua_customptrlen) (lua_State *L);


/*
** ---------------------------------------------------------------------------
** lua_customptrcap(L)
** ---------------------------------------------------------------------------
**
** Return the current allocated capacity (maximum number of pointers the
** array can hold without a reallocation).
**
** Side effects: none.
*/
LUA_API int (lua_customptrcap) (lua_State *L);


/*
** ---------------------------------------------------------------------------
** lua_resizecustomptrs(L, newcap)
** ---------------------------------------------------------------------------
**
** Resize the internal array to exactly 'newcap' slots.
**
** If 'newcap' is smaller than the current length, trailing elements (those
**   with indices > newcap) are silently discarded — no finalizer is called.
** If 'newcap' is 0 or negative the internal array is freed and both length
**   and capacity are reset to 0.  This is equivalent to a hard clear with
**   memory release.
**
** Returns 1 on success, 0 if a reallocation was attempted and failed
** (the original array is preserved on failure).
**
** Side effects:
**   - May free or reallocate the internal array.
**   - If newcap < len, those entries are lost.
**   - If newcap == cap this is a no-op.
*/
LUA_API int (lua_resizecustomptrs) (lua_State *L, int newcap);


/*
** ---------------------------------------------------------------------------
** lua_clearcustomptrs(L)
** ---------------------------------------------------------------------------
**
** Drop all stored pointers by setting the logical length to 0.
** The internal array is NOT freed — capacity is preserved so subsequent
** Push/Set calls can reuse the already-allocated memory.
**
** Use lua_resizecustomptrs(L, 0) if you also want to release the memory.
**
** Side effects:
**   - len = 0.
**   - Capacity and backing store unchanged.
**   - No destructors/finalizers are called.
*/
LUA_API void (lua_clearcustomptrs) (lua_State *L);


/*
** ---------------------------------------------------------------------------
** lua_findcustomptr(L, ptr)
** ---------------------------------------------------------------------------
**
** Linear search for a pointer value.  Returns its 1‑based index (the first
** match from the front), or -1 if the pointer is not found.
**
** NULL can be searched for; if the array contains NULL-filled gaps
** lua_findcustomptr(L, NULL) will return the index of the first NULL slot.
**
** Side effects: none (read-only).  O(n) in the number of stored pointers.
*/
LUA_API int (lua_findcustomptr) (lua_State *L, void *ptr);

#endif
