#include "runtime/script/bind/script_bind.h"

#include "runtime/config/bind/config_bind.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

void ShutdownConfigBindings(ScriptVM& vm) {
	ShutdownConfigBindings(vm.GetState());
}

}  // namespace script
}  // namespace engine
