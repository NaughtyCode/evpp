#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/bind/physics_bindings.h"

#include "runtime/physics/bind/physics_bind_common.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace physics_bindings {

void Register(ScriptVM& vm) {
	RegisterLogGlobals(vm);

	lua_State* L = vm.GetState();
	if (!L) return;

	lua_newtable(L);
	RegisterBodyBindings(L);
	RegisterStateBindings(L);
	RegisterConfigBindings(L);
	RegisterAssetBindings(L);
	RegisterLogModuleBindings(L);
	RegisterConstants(L);
	lua_setglobal(L, "physics");
}

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
