#include "runtime/vm/main_thread_vm.h"

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/import_bind.h"
#include "runtime/script/script_bind.h"

namespace engine {

MainThreadScriptVM::MainThreadScriptVM(LuaSandboxLevel level) : ScriptVM(level) {}

MainThreadScriptVM::~MainThreadScriptVM() = default;

bool MainThreadScriptVM::IsMainThreadVM() const noexcept {
	return true;
}

void MainThreadScriptVM::ExportRuntimeBindings(TimerManager& timer_mgr) {
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptBind: exporting all APIs to Lua...");

	{
		ENGINE_PROFILE_SCRIPT_EXPORT("log");
		script::ExportLog(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("timer");
		script::ExportTimer(*this, timer_mgr);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("net");
		script::ExportNet(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("entity");
		script::ExportEntity(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("msgpack");
		script::ExportMsgPack(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("json");
		script::ExportJson(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("space");
		script::ExportSpace(*this, nullptr);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("aoi");
		script::ExportAOI(*this);
	}
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("orm");
		script::ExportOrm(*this);
	}
#endif
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("rpc");
		script::ExportRpc(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("auth");
		script::ExportAuth(*this);
	}
#if defined(ENGINE_MEM_STATS_ENABLED)
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("mem");
		script::ExportMem(*this);
	}
#endif
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("config");
		script::ExportConfigBindings(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("profiler");
		script::ExportProfiler(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("import");
		engine::ExportImport(*this);
	}
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("mongo");
		script::ExportMongo(*this);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("db_service");
		script::ExportDbService(*this);
	}
#endif

	ENGINE_LOG_INFO(logger, "ScriptBind: all APIs exported");
}

void MainThreadScriptVM::ShutdownNetworkBindings() {
	script::ShutdownRpcBindings(*this);
	script::ShutdownConfigBindings(*this);
	script::ShutdownNetBindings();
}

void MainThreadScriptVM::ShutdownTimerBindings() {
	script::ShutdownEntityBindings();
	script::ShutdownTimerBindings(*this);
}

void MainThreadScriptVM::ShutdownProfilerBindings() {
	script::ShutdownProfilerBindings(*this);
}

}  // namespace engine
