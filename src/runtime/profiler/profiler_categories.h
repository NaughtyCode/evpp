#pragma once

#ifdef ENGINE_PROFILER_ENABLED

#include "thirdparty/perfetto/perfetto.h"

PERFETTO_DEFINE_CATEGORIES(
	perfetto::Category("engine").SetDescription("Engine general (frame loop, lifecycle)"),
	perfetto::Category("engine.entity").SetDescription("Entity lifecycle and queries"),
	perfetto::Category("engine.space").SetDescription("Space lifecycle and message routing"),
	perfetto::Category("engine.aoi").SetDescription("AOI / spatial queries"),
	perfetto::Category("engine.auth").SetDescription("Auth and session management"),
	perfetto::Category("engine.script").SetDescription("Script system"),
	perfetto::Category("engine.physics").SetDescription("Physics system"),
	perfetto::Category("engine.timer").SetDescription("Timer system"),
	perfetto::Category("engine.vm").SetDescription("VM operations"),
	perfetto::Category("engine.net").SetDescription("Network encoding and transport"),
	perfetto::Category("engine.rpc").SetDescription("RPC client/server dispatch"),
	perfetto::Category("engine.db").SetDescription("Database and ORM operations"),
	perfetto::Category("engine.monitoring").SetDescription("Metrics, health, and admin HTTP"),
	perfetto::Category("engine.config").SetDescription("Configuration loading and validation"));

#endif  // ENGINE_PROFILER_ENABLED
