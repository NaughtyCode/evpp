-- luacheck configuration for CloudEngine Lua scripts
-- Run: luacheck resources/ --config resources/.luacheckrc

-- Global objects provided by the engine (read-only).
read_globals = {
	-- Engine API
	"engine",
	"import",
	"class",
	"DEFAULT",

	-- Timer API
	"timer",
	"timer_once",
	"timer_interval",

	-- Network API
	"net",
	"KCP",

	-- Database API
	"DB",

	-- Logging API
	"LOG",
	"Log",
	"log",

	-- Physics API
	"Physics",

	-- Sandbox (restricted environment globals)
	"assert",
	"error",
	"ipairs",
	"next",
	"pairs",
	"pcall",
	"print",
	"rawequal",
	"rawget",
	"rawset",
	"select",
	"tonumber",
	"tostring",
	"type",
	"unpack",
	"xpcall",
	"string",
	"table",
	"math",
	"os_clock",
	"os_date",
	"os_difftime",
	"os_time",

	-- Test-only (test harness globals)
	"run_tests",
	"describe",
	"it",
	"before_each",
	"after_each",
}

-- Files to exclude from checking.
exclude_files = {
	"**/thirdparty/**",
	"**/third_party/**",
}

-- Warning filter configuration.
-- Unused variables and arguments are warnings, not errors.
unused_args = false
allow_defined = true
module = false

-- Ignore globals matching these patterns (wildcards).
globals = {}

-- Per-file overrides.
files = {
	["**/tests/**"] = {
		read_globals = {
			"assert_eq",
			"assert_truthy",
			"assert_gt",
			"assert_contains",
			"assert_error",
			"assert_nil",
			"test",
			"setup",
			"teardown",
			"async_test",
			"wait_for",
		},
	},
}

-- Max line length before warning.
max_line_length = 140

-- Max cyclomatic complexity before warning.
max_cyclomatic_complexity = 20
