#pragma once

#include <cstdint>

#include "runtime/core/engine_api.h"

extern "C" {
#include "lua.h"
}

namespace engine {

//=============================================================================
// VMCustomPtrStore — C++ wrapper around the Lua custom-pointer array API.
//
// Each lua_State owns a dynamic void* array stored in global_State. This
// class provides type-safe, bounds-checked access to that array, making it
// easy to attach arbitrary C++ objects (physics worlds, network sessions,
// timer managers, etc.) to a VM without going through the Lua stack or
// registry.
//
// The underlying array lives in the Lua global_State and shares its
// lifetime.  When the lua_State is closed the array is freed regardless of
// whether this wrapper object still exists.  Callers are responsible for
// ensuring that stored pointers outlive the VM, or are cleaned up before
// the VM is destroyed.
//
// VMCustomPtrStore is a lightweight view — it holds only a lua_State* and
// delegates every operation to the C API.  Multiple VMCustomPtrStore
// instances wrapping the same lua_State see the same array.
//
// Thread safety: the underlying C API acquires the Lua state lock for each
// mutation, so individual calls are safe.  Callers must still serialise
// compound operations (e.g. check-then-modify) themselves.
//
// Usage:
//   VMCustomPtrStore store(vm.GetState());
//   int idx = store.Push(myObject);          // append
//   store.Set(idx, otherObject);             // overwrite
//   auto* obj = store.GetAs<MyClass>(1);     // typed read
//   store.Remove(2);                         // remove & shift
//   store.Clear();                           // drop all
//=============================================================================

class ENGINE_API VMCustomPtrStore {
public:
    //-------------------------------------------------------------------------
    // Construction
    //-------------------------------------------------------------------------

    /// Wrap an existing lua_State.  The state must outlive this object.
    /// No allocation is performed — the array inside global_State is
    /// accessed directly.
    explicit VMCustomPtrStore(lua_State* L) : L_(L) {}

    //=========================================================================
    // Element access
    //=========================================================================

    //-------------------------------------------------------------------------
    // Set(index, ptr)
    //-------------------------------------------------------------------------
    //
    /// Store a pointer at the given 1‑based index.
    ///
    /// Behaviour by index value:
    ///   [1, Count()]      — overwrite existing slot.
    ///   Count() + 1        — append (same as Push()).
    ///   > Count() + 1      — extend the array: intermediate slots are
    ///                         zero-filled with nullptr, then ptr is placed.
    ///                         May trigger a capacity reallocation.
    ///   < 0                — resolved relative to the end: -1 = last element,
    ///                         -2 = second-to-last, etc.
    ///   0                  — silent no-op (0 is never a valid index).
    ///
    /// Side effects:
    ///   - May grow the logical length (fills gaps with nullptr).
    ///   - May reallocate internal storage (capacity doubles as needed).
    ///   - Existing pointers at other indices are undisturbed unless the
    ///     array is reallocated (their values are preserved; C++ references
    ///     to array elements are invalidated on realloc).
    void Set(int index, void* ptr);

    //-------------------------------------------------------------------------
    // Get(index)
    //-------------------------------------------------------------------------
    //
    /// Return the pointer at a 1‑based index.  Negative indices count from
    /// the end (-1 = last).  Returns nullptr when the index is out of range
    /// OR when the slot genuinely stores nullptr.  Callers that store nullptr
    /// must track valid indices themselves.
    ///
    /// Side effects: none (read-only).
    void* Get(int index) const;

    //-------------------------------------------------------------------------
    // GetAs<T>(index)
    //-------------------------------------------------------------------------
    //
    /// Typed variant of Get().  Returns static_cast<T*>(Get(index)).
    /// The caller is responsible for ensuring the stored pointer actually
    /// points to a T object; no RTTI check is performed.
    ///
    /// Side effects: none.
    template <typename T>
    T* GetAs(int index) const {
        return static_cast<T*>(Get(index));
    }

    //-------------------------------------------------------------------------
    // operator[](index)
    //-------------------------------------------------------------------------
    //
    /// 0‑based array access.  Equivalent to Get(index + 1).
    /// Bounds are NOT checked — the caller must ensure 0 <= idx < Count().
    /// Provided for convenience when iterating:
    ///
    ///   for (int i = 0; i < store.Count(); ++i) {
    ///       auto* obj = store[i];
    ///   }
    ///
    /// Side effects: none (read-only).  Undefined behaviour if out of range.
    void* operator[](int index) const;

    //=========================================================================
    // Mutation
    //=========================================================================

    //-------------------------------------------------------------------------
    // Push(ptr)
    //-------------------------------------------------------------------------
    //
    /// Append a pointer to the end of the array.  Returns the new 1‑based
    /// index on success.  Returns 0 if the internal capacity reallocation
    /// failed (the array is unchanged in that case).
    ///
    /// Side effects:
    ///   - Count() increases by 1.
    ///   - May reallocate internal storage.
    ///   - Existing indices are stable.
    int Push(void* ptr);

    //-------------------------------------------------------------------------
    // Remove(index)
    //-------------------------------------------------------------------------
    //
    /// Remove the pointer at the given 1‑based index.  All elements above
    /// the removed position shift down, so indices of those elements
    /// decrease by 1.  Negative indices are resolved from the end.
    ///
    /// Returns true on success, false if the index is out of range
    /// (including 0).
    ///
    /// Side effects:
    ///   - Count() decreases by 1.
    ///   - Indices of trailing elements are invalidated (they shift).
    ///   - Capacity is NOT reduced — memory is retained for reuse.
    ///   - No destructor or finalizer is called on the removed pointer;
    ///     the caller must manage the pointed-to object's lifetime.
    bool Remove(int index);

    //-------------------------------------------------------------------------
    // Clear()
    //-------------------------------------------------------------------------
    //
    /// Drop all stored pointers by resetting the logical length to 0.
    /// The internal array is NOT freed — capacity is preserved so
    /// subsequent Push/Set calls reuse the already-allocated memory.
    /// Call Reserve(0) if you also want to release the backing store.
    ///
    /// Side effects:
    ///   - Count() becomes 0.
    ///   - Capacity() is unchanged.
    ///   - No destructors are called on any stored pointer.
    void Clear();

    //-------------------------------------------------------------------------
    // Reserve(n)
    //-------------------------------------------------------------------------
    //
    /// Ensure the internal array has capacity for at least 'n' pointers.
    /// If n <= Capacity() this is a no-op.  If the reallocation fails the
    /// array is unchanged.
    ///
    /// Passing n == 0 frees the backing store (Count() is also reset to 0).
    ///
    /// Returns true on success, false on allocation failure.
    ///
    /// Side effects:
    ///   - May reallocate or free the internal array.
    ///   - If n < Count(), trailing elements are discarded.
    ///   - If n == 0, both Count() and Capacity() become 0.
    bool Reserve(int n);

    //=========================================================================
    // Query
    //=========================================================================

    /// Current number of stored pointers.  Side effects: none.
    int Count() const;

    /// Current allocated capacity (max elements before realloc).  Side effects: none.
    int Capacity() const;

    /// True when Count() == 0.  Side effects: none.
    bool Empty() const;

    //-------------------------------------------------------------------------
    // Find(ptr)
    //-------------------------------------------------------------------------
    //
    /// Linear search for a pointer value.  Returns the 1‑based index of the
    /// first match from the front, or -1 if not found.
    ///
    /// nullptr is a searchable value — if the array has nullptr-filled gaps,
    /// Find(nullptr) returns the index of the first such gap.
    ///
    /// Side effects: none.  O(n) in Count().
    int Find(void* ptr) const;

    //-------------------------------------------------------------------------
    // Contains(ptr)
    //-------------------------------------------------------------------------
    //
    /// True if 'ptr' exists in the array.  Equivalent to Find(ptr) >= 0.
    /// Side effects: none.  O(n) in Count().
    bool Contains(void* ptr) const;

    //=========================================================================
    // Bulk operations
    //=========================================================================

    //-------------------------------------------------------------------------
    // CopyTo(dst, max_count)
    //-------------------------------------------------------------------------
    //
    /// Copy at most max_count pointers from the array into the caller-
    /// supplied buffer 'dst'.  Pointers are written in index order (1, 2, …).
    /// Returns the number of pointers written — this is min(Count(), max_count).
    ///
    /// The caller must ensure dst has room for at least max_count void*
    /// elements.  On return, dst[0] holds the pointer from index 1, dst[1]
    /// from index 2, etc.
    ///
    /// Side effects: none (the array is not modified; only *dst is written).
    int CopyTo(void** dst, int max_count) const;

    //-------------------------------------------------------------------------
    // CopyFrom(src, count)
    //-------------------------------------------------------------------------
    //
    /// Replace the entire array contents with 'count' pointers read from
    /// the caller-supplied buffer 'src'.  Equivalent to Clear() followed by
    /// Reserve(count) and then Push(src[i]) for each i in [0, count).
    ///
    /// Side effects:
    ///   - Previous contents are discarded (no finalizers called).
    ///   - Count() becomes 'count'.
    ///   - May reallocate; if Reserve fails the array may be left partially
    ///     populated or cleared depending on where the failure occurred.
    void CopyFrom(void* const* src, int count);

private:
    lua_State* L_;
};

}  // namespace engine
