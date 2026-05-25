/*
** $Id: lstate_custom_ptr.c $
** Lua State Custom Pointer Array — implementation
** See Copyright Notice in lua.h
*/

#define lstate_custom_ptr_c
#define LUA_CORE

#include "lprefix.h"

#include "lua.h"

#include "lapi.h"
#include "lmem.h"
#include "lstate.h"

#include "lstate_custom_ptr.h"


/*
** Convert a 1-based public index (possibly negative) to a 0-based
** C array index. Returns -1 if out of range.
*/
static int absindex (global_State *g, int index) {
  if (index > g->customptr_len || index == 0)
    return -1;
  if (index < 0) {
    if (-index > g->customptr_len)
      return -1;
    return g->customptr_len + index;
  }
  return index - 1;
}


/*
** Ensure the array has at least 'need' slots (capacity, not length).
** Returns 1 on success, 0 on allocation failure.
*/
static int ensurecap (lua_State *L, global_State *g, int need) {
  if (need <= g->customptr_cap)
    return 1;
  /* grow by at least 2x, starting from 4 */
  int newcap = g->customptr_cap > 0 ? g->customptr_cap * 2 : 4;
  while (newcap < need)
    newcap *= 2;
  void **newarr = luaM_reallocvector(L, g->customptrs,
                                     g->customptr_cap, newcap, void*);
  if (newarr == NULL)
    return 0;
  g->customptrs = newarr;
  g->customptr_cap = newcap;
  return 1;
}


LUA_API void lua_setcustomptr (lua_State *L, int index, void *ptr) {
  global_State *g = G(L);
  lua_lock(L);
  if (index == 0 || -index > g->customptr_len) {
    lua_unlock(L);
    return;
  }
  if (index < 0)
    index = g->customptr_len + index + 1;
  if (index > g->customptr_len) {
    /* fill intermediate slots with NULL */
    if (!ensurecap(L, g, index))
      goto done;
    while (g->customptr_len < index)
      g->customptrs[g->customptr_len++] = NULL;
  }
  g->customptrs[index - 1] = ptr;
done:
  lua_unlock(L);
}


LUA_API void *lua_getcustomptr (lua_State *L, int index) {
  global_State *g = G(L);
  lua_lock(L);
  int i = absindex(g, index);
  void *ptr = (i >= 0) ? g->customptrs[i] : NULL;
  lua_unlock(L);
  return ptr;
}


LUA_API int lua_pushcustomptr (lua_State *L, void *ptr) {
  global_State *g = G(L);
  lua_lock(L);
  if (!ensurecap(L, g, g->customptr_len + 1)) {
    lua_unlock(L);
    return 0;
  }
  g->customptrs[g->customptr_len] = ptr;
  g->customptr_len++;
  int idx = g->customptr_len;  /* 1-based index */
  lua_unlock(L);
  return idx;
}


LUA_API void lua_nullcustomptr (lua_State *L, int index) {
  global_State *g = G(L);
  lua_lock(L);
  int i = absindex(g, index);
  if (i >= 0)
    g->customptrs[i] = NULL;
  lua_unlock(L);
}


LUA_API int lua_customptrlen (lua_State *L) {
  global_State *g = G(L);
  return g->customptr_len;
}


LUA_API int lua_customptrcap (lua_State *L) {
  global_State *g = G(L);
  return g->customptr_cap;
}


LUA_API int lua_resizecustomptrs (lua_State *L, int newcap) {
  global_State *g = G(L);
  lua_lock(L);
  if (newcap <= 0) {
    /* free and reset */
    luaM_freearray(L, g->customptrs, cast_sizet(g->customptr_cap));
    g->customptrs = NULL;
    g->customptr_cap = 0;
    g->customptr_len = 0;
    lua_unlock(L);
    return 1;
  }
  void **newarr = luaM_reallocvector(L, g->customptrs,
                                     g->customptr_cap, newcap, void*);
  if (newarr == NULL) {
    lua_unlock(L);
    return 0;
  }
  g->customptrs = newarr;
  g->customptr_cap = newcap;
  if (g->customptr_len > newcap)
    g->customptr_len = newcap;
  lua_unlock(L);
  return 1;
}


LUA_API void lua_clearcustomptrs (lua_State *L) {
  global_State *g = G(L);
  lua_lock(L);
  g->customptr_len = 0;
  lua_unlock(L);
}


LUA_API int lua_findcustomptr (lua_State *L, void *ptr) {
  global_State *g = G(L);
  lua_lock(L);
  int found = -1;
  for (int i = 0; i < g->customptr_len; i++) {
    if (g->customptrs[i] == ptr) {
      found = i + 1;  /* 1-based */
      break;
    }
  }
  lua_unlock(L);
  return found;
}
