#include "runtime/script/script_bind.h"

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"
#include "runtime/script/aoi_bind.h"
#include "runtime/script/auth_bind.h"
#include "runtime/config/config_bind.h"
#include "runtime/script/import_bind.h"
#include "runtime/script/json_bind.h"
#include "runtime/script/log_bind.h"
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
#include "runtime/script/orm_bind.h"
#endif
#include "runtime/script/rpc_bind.h"
#include "runtime/script/entity_bind.h"
#include "runtime/script/msgpack_bind.h"
#include "runtime/script/net_bind.h"
#include "runtime/script/space_bind.h"
#include "runtime/script/timer_bind.h"
#if defined(ENGINE_MEM_STATS_ENABLED)
#include "runtime/script/mem_bind.h"
#endif
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
#include "runtime/database/data_service/db_service_main_bind.h"
#include "runtime/database/mongo_bind/mongo_bind.h"
#endif
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

void ExportAll(ScriptVM& vm, TimerManager& tm) {
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptBind: exporting all APIs to Lua...");

	{
		ENGINE_PROFILE_SCRIPT_EXPORT("log");
		ExportLog(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("timer");
		ExportTimer(vm, tm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("net");
		ExportNet(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("entity");
		ExportEntity(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("msgpack");
		ExportMsgPack(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("json");
		ExportJson(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("space");
		ExportSpace(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("aoi");
		ExportAOI(vm);
	}
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("orm");
		ExportOrm(vm);
	}
#endif
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("rpc");
		ExportRpc(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("auth");
		ExportAuth(vm);
	}
#if defined(ENGINE_MEM_STATS_ENABLED)
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("mem");
		ExportMem(vm);
	}
#endif
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("config");
		ExportConfigBindings(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("import");
		engine::ExportImport(vm);
	}
#if defined(ENGINE_MONGODB_ENABLED) && ENGINE_DATABASE_ENABLED
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("mongo");
		ExportMongo(vm);
	}
	{
		ENGINE_PROFILE_SCRIPT_EXPORT("db_service");
		ExportDbService(vm);
	}
#endif

	ENGINE_LOG_INFO(logger, "ScriptBind: all APIs exported");
}

void ShutdownConfigBindings(ScriptVM& vm) {
	ShutdownConfigBindings(vm.GetState());
}

}  // namespace script
}  // namespace engine
