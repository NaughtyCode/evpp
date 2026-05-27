#include "runtime/vm/custom_ptr_store.h"

#include <cstring>

extern "C" {
#include "lstate_custom_ptr.h"
}

namespace engine {

void VMCustomPtrStore::Set(int index, void* ptr) {
	lua_setcustomptr(L_, index, ptr);
}

void* VMCustomPtrStore::Get(int index) const {
	return lua_getcustomptr(L_, index);
}

void* VMCustomPtrStore::operator[](int index) const {
	if (index < 0) return nullptr;
	return lua_getcustomptr(L_, index + 1);
}

int VMCustomPtrStore::Push(void* ptr) {
	return lua_pushcustomptr(L_, ptr);
}

void VMCustomPtrStore::SetNull(int index) {
	lua_nullcustomptr(L_, index);
}

void VMCustomPtrStore::Clear() {
	lua_clearcustomptrs(L_);
}

bool VMCustomPtrStore::Reserve(int n) {
	int cap = lua_customptrcap(L_);
	if (cap >= n) return true;
	return lua_resizecustomptrs(L_, n) != 0;
}

int VMCustomPtrStore::Count() const {
	return lua_customptrlen(L_);
}

int VMCustomPtrStore::Capacity() const {
	return lua_customptrcap(L_);
}

bool VMCustomPtrStore::Empty() const {
	return Count() == 0;
}

int VMCustomPtrStore::Find(void* ptr) const {
	return lua_findcustomptr(L_, ptr);
}

bool VMCustomPtrStore::Contains(void* ptr) const {
	return Find(ptr) >= 0;
}

int VMCustomPtrStore::CopyTo(void** dst, int max_count) const {
	int n = Count();
	if (n > max_count) n = max_count;
	for (int i = 0; i < n; ++i)
		dst[i] = Get(i + 1);
	return n;
}

void VMCustomPtrStore::CopyFrom(void* const* src, int count) {
	Clear();
	if (count <= 0) return;
	if (!Reserve(count)) return;
	for (int i = 0; i < count; ++i) {
		if (Push(src[i]) == 0) break;
	}
}

}  // namespace engine
