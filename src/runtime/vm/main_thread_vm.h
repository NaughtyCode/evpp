#pragma once

#include "runtime/vm/vm.h"

namespace engine {

class TimerManager;

class CLOUD_ENGINE_API MainThreadScriptVM : public ScriptVM {
	public:
	explicit MainThreadScriptVM(LuaSandboxLevel level = LuaSandboxLevel::Full);
	~MainThreadScriptVM() override;

	MainThreadScriptVM(const MainThreadScriptVM&) = delete;
	MainThreadScriptVM& operator=(const MainThreadScriptVM&) = delete;
	MainThreadScriptVM(MainThreadScriptVM&&) noexcept = default;
	MainThreadScriptVM& operator=(MainThreadScriptVM&&) noexcept = default;

	bool IsMainThreadVM() const noexcept override;

	void ExportRuntimeBindings(TimerManager& timer_mgr);
	void ShutdownNetworkBindings();
	void ShutdownTimerBindings();
	void ShutdownProfilerBindings();
};

}  // namespace engine
