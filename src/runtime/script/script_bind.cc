#include "runtime/script/script_bind.h"

#include "runtime/config/config_bind.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

void ShutdownConfigBindings(ScriptVM& vm) {
	ShutdownConfigBindings(vm.GetState());
}

}  // namespace script
}  // namespace engine
