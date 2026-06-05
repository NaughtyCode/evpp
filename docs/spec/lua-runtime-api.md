# Lua Runtime API

Last synced: 2026-06-05.

This document lists the C++ runtime bindings that are visible to project Lua
scripts. JSON has a separate detailed reference in [Lua JSON API](lua-json-api.md).

## Export Scope

The full runtime API is exported by:

```cpp
void MainThreadScriptVM::ExportRuntimeBindings(TimerManager& timer_mgr);
```

`MainThreadScriptVM` is the owner-thread VM used by the main engine script
loop. `ExportRuntimeBindings()`, network/timer/profiler shutdown helpers, and
the `profiler` Lua APIs must all run on that VM owner thread.

Other Lua VMs may receive a smaller purpose-specific API set. For example,
database, physics, space, and Redis worker VMs can export selected bindings, but
they must not be treated as having the full main runtime API. The `redis` module
is exported to owner-thread VMs when the Redis runtime is compiled, and Redis
callbacks are dispatched through each VM's `AsyncResultDispatcher`. The
`profiler` module is intentionally exported only to `MainThreadScriptVM`, and
every profiler API checks at call time that it is running on the owner thread.

| Lua global | Exported by | Notes |
|------------|-------------|-------|
| `log_trace`, `log_debug`, `log_info`, `log_warn`, `log_error`, `log_fatal` | `ExportLog` | Global logging functions. |
| `timer` | `ExportTimer` | Main VM timer callbacks. |
| `net` | `ExportNet` | TCP, UDP, KCP, and HTTP helpers. |
| `entity` | `ExportEntity` | Entity instance helpers. |
| `cmsgpack`, `cmsgpack_safe` | `ExportMsgPack` | MessagePack codec. |
| `json`, `json_safe` | `ExportJson` | JSON codec. See [Lua JSON API](lua-json-api.md). |
| `space` | `ExportSpace` | Space lifecycle and cross-space messages. |
| `aoi` | `ExportAOI` | Area-of-interest manager. |
| `rpc` | `ExportRpc` | In-process RPC objects and deferred dispatch. |
| `auth` | `ExportAuth` | Auth backend, sessions, and permissions. |
| `config` | `ExportConfigBindings` | Config reads and reload callbacks. |
| `profiler` | `ExportProfiler` | Main-thread-only profiler runtime control. |
| `import` | `ExportImport` | Callable module importer. |
| `mem` | `ExportMem` | Optional, when `ENGINE_MEM_STATS_ENABLED` is set. |
| `orm`, `mongo`, `db_service` | database exports | Optional, when MongoDB and database support are enabled. |
| `redis` | `ExportRedis` | Optional, when `ENGINE_REDIS_ENABLED` is set. |

## Error Conventions

- Most modules raise a Lua error for invalid arguments or failed required
  operations.
- `json_safe` and `cmsgpack_safe` wrap the throwing codec APIs and return
  `nil, err` when the wrapped operation raises.
- Some lifecycle operations use Lua-style result pairs such as `nil, err`
  when an operation can fail without invalid arguments.
- Async callbacks that are queued from worker threads are drained on the Lua VM
  owner thread by the owning C++ subsystem.
- Redis submission APIs return `false, err` synchronously when a request is not
  accepted. Accepted requests later invoke the registered callback on the
  submitting VM owner thread.

## Logging

```lua
log_trace(message)
log_debug(message)
log_info(message)
log_warn(message)
log_error(message)
log_fatal(message)
```

Each function takes one string and writes it through the engine logger with the
`[lua]` prefix.

## Import

`import` is a callable table:

```lua
local mod = import("runtime.helpers")
import.setpath("resources/script;resources/script/runtime")
import.addpath("resources/script/server")
local loaded = import.loaded()
import.clearcache()
```

| API | Return | Notes |
|-----|--------|-------|
| `import(module_name)` | module result | Resolves and runs a Lua module through `ScriptImporter`. |
| `import.setpath(paths)` | none | Replaces the importer search path string. |
| `import.addpath(path)` | none | Appends a search path. |
| `import.loaded()` | table | Returns a shallow copy of `package.loaded`. |
| `import.clearcache()` | none | Clears importer and package cache entries managed by the importer. |

## Timer

```lua
local id = timer.timeout(1000, function()
    log_info("one shot")
end)

local repeating = timer.interval(500, function()
    log_debug("tick")
end)

local ok, err = timer.cancel(repeating)
```

| API | Return | Notes |
|-----|--------|-------|
| `timer.timeout(ms, callback)` | timer id | Runs `callback()` once after a positive millisecond delay. |
| `timer.interval(ms, callback)` | timer id | Runs `callback()` repeatedly with the positive millisecond interval. |
| `timer.cancel(timer_id)` | `true` or `nil, err` | Cancels a live Lua timer. |

Timer state is per VM and is released by `MainThreadScriptVM::ShutdownTimerBindings()`.

## Serialization

### JSON

The engine exports `json` and `json_safe`. See [Lua JSON API](lua-json-api.md)
for detailed data model, file, validation, and formatting behavior.

### MessagePack

```lua
local bytes = cmsgpack.pack({ id = 7, name = "evpp" })
local value = cmsgpack.unpack(bytes)

local value, next_offset = cmsgpack.unpack_one(bytes)
local a, b, next_offset = cmsgpack.unpack_limit(bytes, 2)
```

| API | Return | Notes |
|-----|--------|-------|
| `cmsgpack.pack(...)` | binary string | Packs all arguments into one MessagePack byte string. |
| `cmsgpack.unpack(bytes)` | values | Unpacks all values from the byte string. |
| `cmsgpack.unpack_one(bytes[, offset])` | value, next offset | Decodes one value from `offset`, or from 0 when omitted. |
| `cmsgpack.unpack_limit(bytes, limit[, offset])` | values, next offset | Decodes up to `limit` values. |

`cmsgpack_safe` exposes the same function names and returns `nil, err` on
raised binding errors. MessagePack depth and payload limits come from
`server.msgpack.max_nesting_depth` and `server.msgpack.max_payload_size`.

## Config

```lua
local fps = config.get("frame.target_fps")
local items, err = config.get_module("items")

local id = config.on_change("server", function(changes)
    for _, change in ipairs(changes) do
        log_info(change.field .. " changed")
    end
end)

local fired = config.flush_changes()
config.unregister(id)
```

| API | Return | Notes |
|-----|--------|-------|
| `config.get(path)` | value or `nil` | Reads selected runtime, client, and server config fields. |
| `config.get_module(name)` | table or `nil, err` | Loads `resources/script/data/<name>.json`. The file must be a top-level JSON array. |
| `config.on_change(module, callback)` | callback id | Registers a reload callback. `module` can be a prefix or `*`. |
| `config.unregister(id)` | none | Removes a reload callback registered by the current Lua state. |
| `config.flush_changes()` | fired count | Drains queued reload events on the VM owner thread. |

`config.get_module()` returns a Lua array of rows and also indexes rows by an
integer `id` field when present.

## Redis

When built with `ENGINE_REDIS_ENABLED`, `ExportRedis()` installs a global
`redis` table and registers it as `package.loaded.redis`.

```lua
local ok, request_id = redis.command({ "GET", "player:1" }, function(result)
    if result.ok then
        local value = result.value
        log_info("redis type=" .. value.type .. " value=" .. tostring(value.value))
    else
        log_warn("redis failed: " .. tostring(result.error))
    end
end, {
    timeout_ms = 1000,
    routing_key = "player:1",
})

local submitted, eval_id = redis.eval(
    "return redis.call('GET', KEYS[1])",
    { "player:1" },
    {},
    function(result) end)
```

| API | Return | Notes |
|-----|--------|-------|
| `redis.command(argv, callback[, options])` | `true, request_id` or `false, err` | `argv` must be an array of strings. The first item is normalized as the Redis command name. |
| `redis.eval(script, keys, args, callback[, options])` | `true, request_id` or `false, err` | Builds an `EVAL` command. If no `routing_key` is set and `keys[1]` exists, that key is used for routing. |
| `redis.is_running()` | boolean | True when `RedisClient` is initialized and worker threads are running. |
| `redis.is_healthy()` | boolean | True when all Redis workers are healthy. |
| `redis.dispatch([max])` | integer | Drains queued Redis callbacks for the current VM dispatcher. Most owner loops call this automatically through `ScriptVM::DispatchAsyncResults()`. |

`options` supports:

| Field | Type | Notes |
|-------|------|-------|
| `timeout_ms` | integer | `0` or omitted uses `redis.connection.command_timeout_ms`. |
| `routing_key` | string | Routes related commands to the same worker for per-key ordering. |

Accepted callbacks receive:

```lua
{
    status = "ok",
    ok = true,
    request_id = 1,
    error = nil,
    value = {
        type = "string",
        value = "payload",
    },
}
```

`status` is one of `ok`, `command_error`, `connection_error`, `auth_error`,
`protocol_error`, `timeout`, `shutdown`, or `dropped`. Redis values are nested
tables with `type` set to `null`, `string`, `status`, `error`, `integer`,
`double`, `bool`, `array`, `map`, `set`, `push`, `attribute`, `bignumber`,
`verbatim_string`, or `unknown`. Array-like Redis values store child Redis value
tables in `value`.

The first Redis runtime rejects connection-state, blocking, Pub/Sub, and
transaction commands from normal `redis.command()` calls, including `AUTH`,
`HELLO`, `SELECT`, `QUIT`, `RESET`, `CLIENT`, `MONITOR`, `SUBSCRIBE`, `MULTI`,
`EXEC`, `WATCH`, `UNWATCH`, `WAIT`, `WAITAOF`, the `B*` blocking commands, and
`XREAD` / `XREADGROUP` when they include `BLOCK`.

## Profiler

The `profiler` module is only available in the main-thread Lua VM. All calls
raise a Lua error when made after shutdown, from another VM, or from a
non-owner thread.

### Runtime Switches

```lua
profiler.set_runtime_enabled(true)
local enabled = profiler.is_runtime_enabled()

local mask, names = profiler.set_enabled_groups({ "physics", "script" })
profiler.disable_groups("network,rpc")
profiler.set_group_enabled("frame", true)
```

| API | Return | Notes |
|-----|--------|-------|
| `profiler.set_runtime_enabled(enabled)` | none | Enables or disables profiler event emission at runtime. |
| `profiler.is_runtime_enabled()` | boolean | Reads the runtime master switch. |
| `profiler.enabled_groups()` | mask, names | Reads the enabled event-group mask and formatted names. |
| `profiler.set_enabled_groups(groups)` | mask, names | Replaces the enabled group mask. |
| `profiler.enable_groups(groups)` | mask, names | Adds groups to the enabled mask. |
| `profiler.disable_groups(groups)` | mask, names | Removes groups from the enabled mask. |
| `profiler.set_group_enabled(group, enabled)` | mask, names | Enables or disables one event group. |
| `profiler.is_group_enabled(group)` | boolean | Checks whether one group is currently enabled. |

`groups` can be a non-negative mask integer, a delimited string using `,`, `;`,
or `|`, or a flat Lua table containing group strings and/or mask integers.
Nested group tables are rejected.

### Group Helpers

| API | Return | Notes |
|-----|--------|-------|
| `profiler.parse_groups(groups)` | mask, names | Parses a group spec without changing state. |
| `profiler.format_groups(groups)` | names | Formats a group spec. |
| `profiler.group_from_name(name)` | mask, canonical name | Resolves one group name. |
| `profiler.group_from_category(category)` | mask, canonical name | Resolves one Perfetto category. |
| `profiler.group_name(group)` | name | Returns the canonical name for one group. |
| `profiler.group_category(group)` | category | Returns the Perfetto category for one group. |
| `profiler.list_groups()` | table | Returns rows with `name`, `category`, `mask`, and `enabled`. |

Supported group constants are:

```lua
profiler.GROUP_NONE
profiler.GROUP_ALL
profiler.GROUP_ENGINE
profiler.GROUP_FRAME
profiler.GROUP_TIMER
profiler.GROUP_PHYSICS
profiler.GROUP_SCRIPT
profiler.GROUP_ENTITY
profiler.GROUP_SPACE
profiler.GROUP_AOI
profiler.GROUP_AUTH
profiler.GROUP_VM
profiler.GROUP_NETWORK
profiler.GROUP_RPC
profiler.GROUP_DATABASE
profiler.GROUP_MONITORING
profiler.GROUP_CONFIG
```

### Session And Trace

```lua
profiler.initialize({
    output_path = "trace.pftrace",
    buffer_size_kb = 4096,
    duration_ms = 0,
    flush_interval_ms = 1000,
    write_into_file = true,
    runtime_enabled = true,
    enabled_event_groups = "frame,timer,script",
})

profiler.start_session()
profiler.stop_session()
local path = profiler.save_trace()
```

| API | Return | Notes |
|-----|--------|-------|
| `profiler.initialize([config])` | boolean | Initializes the profiler with optional config fields shown above. |
| `profiler.shutdown()` | none | Shuts down the profiler manager. |
| `profiler.start_session()` | boolean | Starts trace capture. |
| `profiler.stop_session()` | none | Stops trace capture. |
| `profiler.is_enabled()` | boolean | Compile/runtime availability check from the profiler manager. |
| `profiler.is_initialized()` | boolean | Returns whether the manager is initialized. |
| `profiler.is_active()` | boolean | Returns whether a trace session is active. |
| `profiler.flush()` | none | Flushes pending profiler data. |
| `profiler.read_trace()` | binary string | Reads the cached trace bytes. |
| `profiler.save_trace()` | path or `nil` | Saves using the configured/default path. |
| `profiler.save_trace_exact(path)` | boolean | Saves the cached trace to an exact path. |
| `profiler.last_saved_path()` | string | Returns the last saved trace path. |
| `profiler.cached_trace_size()` | integer | Returns cached trace byte size. |
| `profiler.clear_cached_trace()` | none | Clears cached trace bytes. |
| `profiler.status()` | table | Returns status fields. |

`profiler.status()` returns:

```lua
{
    enabled = true,
    initialized = true,
    active = false,
    runtime_enabled = true,
    enabled_event_groups = profiler.GROUP_ALL,
    enabled_event_group_names = "engine,frame,...",
    cached_trace_size = 0,
    last_saved_path = "",
}
```

## Entity

```lua
local e = entity.create()
e:set_attr("hp", 100)
local hp = e:get_attr("hp")
e:add_component("combat", { level = 1 })
e:destroy()
```

| API | Return | Notes |
|-----|--------|-------|
| `entity.create([id])` | entity instance | Creates and activates an entity. |
| `entity:destroy()` | boolean | Destroys the entity and releases Lua refs. |
| `entity:get_id()` | integer | Returns the entity id. |
| `entity:get_state()` | string | `created`, `active`, `suspended`, `destroyed`, or `unknown`. |
| `entity:activate()` | none | Activates the entity. |
| `entity:suspend()` | none | Suspends the entity. |
| `entity:get_attr(key)` | value or `nil` | Reads a scalar attribute. |
| `entity:set_attr(key, value)` | none | Writes integer, number, string, or boolean. `nil` removes. |
| `entity:remove_attr(key)` | boolean | Removes an attribute. |
| `entity:has_attr(key)` | boolean | Tests for an attribute. |
| `entity:attr_count()` | integer | Counts attributes. |
| `entity:list_attrs()` | table | Returns attribute names. |
| `entity:bind_connection(conn)` | none | Stores a connection table used by `entity:send`. Pass `nil` to unbind. |
| `entity:get_connection()` | conn or `nil` | Reads the bound connection. |
| `entity:send(data)` | none | Delegates to bound `conn:send(data)`. |
| `entity:add_timer(interval_ms, repeat, callback)` | timer id | Adds an entity-scoped timer. |
| `entity:cancel_timer(timer_id)` | none | Cancels an entity-scoped timer. |
| `entity:add_component(name, table)` | none | Stores a Lua component table. |
| `entity:get_component(name)` | table or `nil` | Reads a Lua component table. |
| `entity:remove_component(name)` | none | Removes a Lua component table. |

## Space

```lua
local id = space.create("arena", {
    max_entities = 1000,
    max_players = 100,
    scripts = { "arena.init" },
})

space.send(id, target_entity_id, cmsgpack.pack({ op = "ping" }))
local msg = space.poll()
```

| API | Return | Notes |
|-----|--------|-------|
| `space.create(name[, config])` | space id or `nil, err` | Creates a space. Config supports `max_entities`, `max_players`, and `scripts`. |
| `space.get(id)` | info table or `nil` | Info contains `id`, `name`, `entity_count`, and `player_count`. |
| `space.destroy(id)` | none or `nil, err` | Refuses to destroy the current bound space from its own script. |
| `space.send(space_id, target_entity, payload[, source_entity])` | none | Queues a cross-space binary payload. Max payload is 1 MiB. |
| `space.list()` | table | Lists all spaces. |
| `space.current()` | info table or `nil` | Returns the bound space or default space. |
| `space.poll()` | message table or `nil` | Pops the next pending cross-space message. |

Internal hooks:

- `space._deliver_message(src_space, src_entity, tgt_entity, payload)` is used
  by `SpaceMessageRouter`.
- `space._on_connection_data(conn_lightuserdata, data)` is a default no-op hook
  that game scripts may replace in the `space` table.

## AOI

```lua
aoi.init(1000, 1000, 50)
aoi.set_event_callback(function(observer, target, entered)
    -- entered is true on enter and false on leave.
end)
aoi.register_entity(1, 10, 20, 100)
local visible = aoi.get_visible(1)
```

| API | Return | Notes |
|-----|--------|-------|
| `aoi.init(world_width, world_height[, cell_size])` | `true` or `nil, err` | Initializes a spatial grid. |
| `aoi.set_event_callback(function_or_nil)` | `true` or `nil, err` | Sets or clears enter/leave callback. |
| `aoi.register_entity(entity_id, x, y[, aoi_radius])` | none or `nil, err` | Adds or updates an entity. |
| `aoi.update_entity(entity_id, x, y)` | none or `nil, err` | Moves an entity. |
| `aoi.update_radius(entity_id, aoi_radius)` | none or `nil, err` | Updates visibility radius. |
| `aoi.unregister_entity(entity_id)` | none or `nil, err` | Removes an entity. |
| `aoi.get_visible(entity_id)` | table | Returns visible entity ids. |
| `aoi.query_radius(x, y, radius)` | table | Returns entity ids in a radius. |
| `aoi.count()` | integer | Returns registered entity count. |
| `aoi.shutdown()` | none or `nil, err` | Releases AOI state. |

AOI mutation APIs reject calls made from inside an AOI callback.

## Network

The `net` table contains protocol-specific subtables:

```lua
net.client
net.server
net.http
net.udp_client
net.udp_server
net.kcp_client
net.kcp_server
```

### TCP Client

| API | Return | Notes |
|-----|--------|-------|
| `net.client.connect(addr)` | client instance | Connects to an address string. |
| `client:send(data)` | none | Sends a framed binary payload. |
| `client:disconnect()` | boolean | Disconnects and releases callbacks. |
| `client:is_connected()` | boolean | Reads connection state. |
| `client:set_on_connect(callback)` | none | Sets `on_connect`. |
| `client:set_on_message(callback)` | none | Sets `on_message`. |
| `client:set_on_close(callback)` | none | Sets `on_close`. |

### TCP Server

| API | Return | Notes |
|-----|--------|-------|
| `net.server.listen(addr)` | server instance | Starts a TCP server. |
| `server:stop()` | boolean | Stops the server. |
| `server:set_on_connect(callback)` | none | Sets server-level connect callback. |
| `server:set_on_close(callback)` | none | Sets server-level close callback. |
| `server:set_on_message(callback)` | none | Sets server-level message callback. |
| `conn:send(data)` | none | Sends to a TCP connection. |
| `conn:close()` | boolean | Closes a TCP connection. |
| `conn:is_connected()` | boolean | Reads connection state. |
| `conn:set_on_message(callback)` | none | Overrides message callback for that connection. |
| `conn:set_on_close(callback)` | none | Overrides close callback for that connection. |

### HTTP

| API | Return | Notes |
|-----|--------|-------|
| `net.http.get(url, on_response)` | none | Runs an async GET. |
| `net.http.post(url, body, on_response)` | none | Runs an async POST. |

### UDP

| API | Return | Notes |
|-----|--------|-------|
| `net.udp_client.connect(host, port)` | client instance or `nil, err` | Creates a connected UDP client. |
| `net.udp_client.do_request(host, port, data, timeout_ms)` | string | One-shot request. |
| `net.udp_client.send_to(host, port, data)` | boolean or `false, err` | One-shot send. |
| `udp_client:send(data)` | boolean | Sends on a connected UDP client. |
| `udp_client:do_request(data, timeout_ms)` | string | Request on connected UDP client. |
| `udp_client:close()` | boolean | Closes the client. |
| `udp_client:is_connected()` | boolean | Reads connection state. |
| `net.udp_server.listen(port_or_ports, on_message)` | server instance | Starts one or more UDP listeners. |
| `udp_server:stop()` | boolean | Stops the server. |
| `udp_server:pause()` | none | Pauses receive handling. |
| `udp_server:continue()` | none | Resumes receive handling. |
| `udp_server:is_running()` | boolean | Reads server state. |
| `udp_server:set_on_message(callback)` | none | Replaces message callback. |

### KCP

| API | Return | Notes |
|-----|--------|-------|
| `net.kcp_client.new([conv])` | client instance | Creates an unconnected KCP client for tuning before connect. |
| `net.kcp_client.connect(host, port[, conv])` | client instance or `nil, err` | Connects immediately. |
| `net.kcp_client.do_request(host, port, data, timeout_ms[, conv])` | string | One-shot request. |
| `kcp_client:connect(host, port)` | boolean or `nil, err` | Connects an instance from `new`. |
| `kcp_client:send(data)` | boolean | Sends data. |
| `kcp_client:do_request(data, timeout_ms)` | string | Request on connected client. |
| `kcp_client:close()` | boolean | Closes the client. |
| `kcp_client:is_connected()` | boolean | Reads connection state. |
| `kcp_client:set_kcp_nodelay(nodelay, interval, resend, nc)` | none | KCP tuning before connect. |
| `kcp_client:set_kcp_wnd_size(sndwnd, rcvwnd)` | none | KCP window tuning. |
| `kcp_client:set_kcp_mtu(mtu)` | none | KCP MTU tuning. |
| `kcp_client:set_kcp_conv(conv)` | none | Sets conversation id before connect. |
| `net.kcp_server.listen(port_or_ports, on_message)` | server instance | Starts one or more KCP listeners. |
| `kcp_server:stop()` | boolean | Stops the server. |
| `kcp_server:pause()` | none | Pauses receive handling. |
| `kcp_server:continue()` / `kcp_server:resume()` | none | Resumes receive handling. |
| `kcp_server:is_running()` | boolean | Reads server state. |
| `kcp_server:set_on_message(callback)` | none | Replaces message callback. |
| `kcp_server:set_kcp_nodelay(nodelay, interval, resend, nc)` | none | KCP tuning. |
| `kcp_server:set_kcp_wnd_size(sndwnd, rcvwnd)` | none | KCP window tuning. |
| `kcp_server:set_kcp_mtu(mtu)` | none | KCP MTU tuning. |
| `kcp_server:set_session_timeout(ms)` | none | Sets KCP session timeout. |
| `kcp_server:set_max_message_size(bytes)` | none | Sets max message size. |

## RPC

```lua
local server = rpc.new_server()
server:register_service("echo", function(service, method, body)
    return body
end)

local client = rpc.new_client()
client:set_send_callback(function(msgid, service, method, body)
    -- Transport integration sends the request bytes.
end)
```

| API | Return | Notes |
|-----|--------|-------|
| `rpc.new_server()` | server instance | Creates an RPC server wrapper. |
| `server:register_service(service, callback)` | boolean | Callback receives `service`, `method`, `body` and returns a string body. |
| `server:unregister_service(service)` | boolean | Removes a service callback. |
| `server:stop()` | boolean | Stops the server and releases callbacks. |
| `rpc.new_client()` | client instance | Creates an RPC client wrapper. |
| `client:set_send_callback(callback)` | boolean | Callback receives `msgid`, `service`, `method`, `body`. |
| `client:call(service, method[, args[, timeout_ms]])` | body, nil or `nil, err` | Synchronous call. `args` defaults to `{}` and timeout to 5000 ms. |
| `client:call_async(service, method, args, callback[, timeout_ms])` | boolean | Async call. Callback receives `body, nil` or `nil, err`. |
| `client:stop()` | boolean | Stops the client and releases callbacks. |

Deferred RPC callbacks are drained by the C++ runtime through
`UpdateRpcBindings()` on the VM owner thread.

## Auth

| API | Return | Notes |
|-----|--------|-------|
| `auth.set_token_backend()` | boolean | Uses the token backend. |
| `auth.set_jwt_backend(secret)` | boolean | Uses the JWT backend with the given secret. |
| `auth.add_token(token, entity_id)` | boolean | Adds a token mapping to the token backend. |
| `auth.authenticate(method, params_table)` | `true, entity_id, session_id` or `nil, err` | Authenticates through the active backend. |
| `auth.create_session(entity_id)` | session id or `nil` | Creates a session directly. |
| `auth.validate_session(session_id)` | boolean | Validates backend or manager session state. |
| `auth.revoke_session(session_id)` | boolean | Revokes a session. |
| `auth.grant_permission(entity_id, permission)` | boolean | Grants permission through the backend. |
| `auth.revoke_permission(entity_id, permission)` | boolean | Revokes permission through the backend. |
| `auth.has_permission(entity_id, permission)` | boolean | Checks permission through the backend. |
| `auth.cleanup_expired()` | none | Removes expired sessions. |

## Optional Memory Stats

When built with `ENGINE_MEM_STATS_ENABLED`, the `mem` table is exported:

| API | Return | Notes |
|-----|--------|-------|
| `mem.is_enabled()` | boolean | Always true when the module exists. |
| `mem.get_stats()` | table | Returns totals, operation counters, size buckets, and recent allocations. |
| `mem.reset_stats()` | none | Resets memory stats. |
| `mem.dump_stats(filepath)` | boolean | Dumps stats to a file. |

## Optional Database Bindings

When both `ENGINE_MONGODB_ENABLED` and `ENGINE_DATABASE_ENABLED` are set,
`MainThreadScriptVM::ExportRuntimeBindings()` also exports:

- `orm`
- `mongo`
- `db_service`

Those APIs are database-specific and are not duplicated in this runtime overview.
