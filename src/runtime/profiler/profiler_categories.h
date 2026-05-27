#pragma once

#ifdef ENGINE_PROFILER_ENABLED

#include "thirdparty/perfetto/perfetto.h"

PERFETTO_DEFINE_CATEGORIES(
	perfetto::Category("engine").SetDescription("Engine general (frame loop, lifecycle)"),
	perfetto::Category("engine.physics").SetDescription("Physics system"),
	perfetto::Category("engine.script").SetDescription("Script system"),
	perfetto::Category("engine.timer").SetDescription("Timer system"),
	perfetto::Category("engine.vm").SetDescription("VM operations"), );

#endif	// ENGINE_PROFILER_ENABLED
