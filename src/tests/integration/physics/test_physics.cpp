#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>

#ifdef ENGINE_PHYSICS_ENABLED
#include <runtime/physics/physics_engine_bridge.h>
#include <runtime/vm/vm.h>
#endif

namespace {
constexpr const char* kPhysicsConfigDir = "resources/physics/config";
constexpr const char* kFullPhysicsConfigDir = "resources/physics_full/config";
constexpr const char* kPhysicsScriptsDir = "";

#ifdef ENGINE_PHYSICS_ENABLED
constexpr const char* kFullPhysicsConfigJson = R"json({
  "version": 1,
  "scenePath": "/physics/data/full_scene.json",
  "gravityX": 0.0,
  "gravityY": -9.81,
  "gravityZ": 0.0,
  "fixedDeltaTime": 0.01667,
  "solverIterations": 10,
  "subStepCount": 2,
  "maxBodies": 4096,
  "maxContactPoints": 10240,
  "maxBodyPairs": 16384,
  "numBodyMutexes": 32,
  "layerConfig": {
    "objectLayers": {
      "static": 0,
      "dynamic": 1,
      "trigger": 2
    },
    "broadPhaseLayers": {
      "static": 0,
      "dynamic": 1
    },
    "layerMapping": {
      "static": "static",
      "dynamic": "dynamic",
      "trigger": "dynamic"
    },
    "collisionMatrix": [
      {"layerA": "static", "layerB": "static", "collide": false},
      {"layerA": "static", "layerB": "dynamic", "collide": true},
      {"layerA": "static", "layerB": "trigger", "collide": true},
      {"layerA": "dynamic", "layerB": "dynamic", "collide": true},
      {"layerA": "dynamic", "layerB": "trigger", "collide": true},
      {"layerA": "trigger", "layerB": "trigger", "collide": false}
    ]
  }
})json";

std::string PrepareFullPhysicsConfig() {
	const std::filesystem::path full_config_dir(kFullPhysicsConfigDir);
	std::error_code ec;
	std::filesystem::create_directories(full_config_dir, ec);
	REQUIRE_FALSE(ec);

	auto copy_config = [&](const char* file_name) {
		ec.clear();
		std::filesystem::copy_file(std::filesystem::path(kPhysicsConfigDir) / file_name,
								   full_config_dir / file_name,
								   std::filesystem::copy_options::overwrite_existing,
								   ec);
		REQUIRE_FALSE(ec);
	};
	copy_config("threading.json");
	copy_config("logging.json");
	copy_config("thresholds.json");

	std::ofstream physics_config(full_config_dir / "physics.json",
								 std::ios::binary | std::ios::trunc);
	REQUIRE(physics_config.good());
	physics_config << kFullPhysicsConfigJson;
	REQUIRE(physics_config.good());

	return full_config_dir.string();
}

engine::PhysicsEngineBridge& InitializeFreshPhysics(
	const std::string& config_dir = kPhysicsConfigDir,
	const std::string& scripts_dir = kPhysicsScriptsDir) {
	auto& bridge = engine::PhysicsEngineBridge::Instance();
	if (bridge.IsInitialized()) {
		bridge.Shutdown();
	}
	REQUIRE(bridge.Initialize(config_dir, scripts_dir));
	return bridge;
}

engine::PhysicsEngineBridge& StartFreshPhysics(
	const std::string& config_dir = kPhysicsConfigDir,
	const std::string& scripts_dir = kPhysicsScriptsDir) {
	auto& bridge = InitializeFreshPhysics(config_dir, scripts_dir);
	REQUIRE(bridge.Start());
	REQUIRE(bridge.IsRunning());
	REQUIRE(bridge.IsHealthy());
	return bridge;
}

std::string PreparePhysicsBindingScriptDir() {
	const auto script_dir = std::filesystem::temp_directory_path() / "evpp2_physics_bind_scripts";
	std::error_code ec;
	std::filesystem::remove_all(script_dir, ec);
	ec.clear();
	std::filesystem::create_directories(script_dir, ec);
	REQUIRE_FALSE(ec);

	std::ofstream script(script_dir / "spawn.lua", std::ios::binary | std::ios::trunc);
	REQUIRE(script.good());
	script << R"lua(
local slots = physics.get_core_slots()
if not (slots.system and slots.thread and slots.world and slots.script_vm and slots.all) then
	error("missing physics custom ptr slots")
end

local thread = physics.get_thread_info()
if not thread.is_physics_thread then
	error("script did not execute on physics thread")
end

local prototypes = physics.list_prototypes()
if type(prototypes) ~= "table" or #prototypes == 0 then
	error("missing physics prototypes")
end
local prototype_ids = physics.list_prototype_ids()
if type(prototype_ids) ~= "table" or #prototype_ids == 0 or not physics.has_prototype("crate") then
	error("prototype id helpers failed")
end
local crate_proto = physics.get_prototype("crate")
if type(crate_proto) ~= "table" or crate_proto.proto_id ~= "crate" then
	error("missing crate prototype")
end

local body_id, err = physics.spawn("crate", 0.0, 2.0, 0.0, 0.0, 0.0, 0.0, 1.0, 123)
if not body_id or body_id <= 0 then
	error(err or "physics.spawn failed")
end
if not physics.has_body_id(body_id) then
	error("spawned body not registered")
end

local x, y, z = physics.get_transform(body_id)
if type(x) ~= "number" or type(y) ~= "number" or type(z) ~= "number" then
	error("physics.get_transform failed")
end
local transform = physics.get_transform_table(body_id)
if type(transform) ~= "table" or transform.bodyId ~= body_id or transform.position[2] == nil then
	error("physics.get_transform_table failed")
end
local velocity = physics.get_velocity_table(body_id)
if type(velocity) ~= "table" or type(velocity.x) ~= "number" then
	error("physics.get_velocity_table failed")
end
local state = physics.get_body_state(body_id)
if type(state) ~= "table" or state.bodyId ~= body_id or type(state.transform) ~= "table" then
	error("physics.get_body_state failed")
end

local stats = physics.get_stats()
if type(stats) ~= "table" or stats.bodies < 1 then
	error("physics.get_stats failed")
end

if physics.get_registry_size() < 1 then
	error("physics registry not populated")
end
local registry = physics.list_registry()
if type(registry) ~= "table" or #registry < 1 then
	error("physics.list_registry failed")
end
local body_ids = physics.list_registered_body_ids()
if type(body_ids) ~= "table" or #body_ids < 1 then
	error("physics.list_registered_body_ids failed")
end
local names = physics.list_registered_asset_names()
if type(names) ~= "table" then
	error("physics.list_registered_asset_names failed")
end

local state_blob, state_err = physics.save_state()
if type(state_blob) ~= "string" or #state_blob == 0 then
	error(state_err or "physics.save_state failed")
end
local restored, restore_err = physics.restore_state(state_blob)
if restored ~= true then
	error(restore_err or "physics.restore_state failed")
end
)lua";
	REQUIRE(script.good());
	return script_dir.string();
}
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration: Physics engine bridge lifecycle
// ═══════════════════════════════════════════════════════════════════════════
// These tests are only compiled when ENGINE_PHYSICS_ENABLED is ON.

#ifdef ENGINE_PHYSICS_ENABLED

TEST_CASE("Physics bridge initialize and shutdown", "[integration][physics]") {
    auto& bridge = StartFreshPhysics();

    // Shutdown
    bridge.Shutdown();
    REQUIRE_FALSE(bridge.IsRunning());
}

TEST_CASE("PhysicsScriptVM exposes config and status bindings",
		  "[integration][physics][script]") {
	auto& bridge = InitializeFreshPhysics();
	auto* vm = bridge.GetScriptVM();
	REQUIRE(vm != nullptr);

	std::string error;
	std::string result;
	constexpr const char* script = R"lua(
local cfg = physics.get_config()
local phys = physics.get_physics_config()
local threading = physics.get_threading_config()
local log = physics.get_log_config()
local thresholds = physics.get_thresholds_config()
local status = physics.status()
local slots = physics.get_core_slots()
local thread = physics.get_thread_info()
local paths = physics.get_paths()
local dump = physics.dump_config()
local scene = physics.load_configured_scene_asset()
local scene_by_path = physics.load_scene_asset(phys.scenePath)
local parsed_scene = physics.parse_scene_asset_json([[
{
  "materials": [
    {"name": "script_mat", "friction": 0.4, "restitution": 0.1}
  ],
  "staticBodies": [],
  "dynamicPrototypes": [],
  "dynamicBodies": [],
  "constraints": []
}
]], "inline_scene.json")
local scene_query_json = [[
{
  "materials": [
    {"name": "script_mat", "friction": 0.4, "restitution": 0.1}
  ],
  "staticBodies": [
    {
      "id": "floor",
      "shape": {"type": "box", "params": {"halfExtent": [1, 1, 1]}},
      "material": {"friction": 0.4, "restitution": 0.1},
      "transform": {"position": [0, 0, 0], "rotation": [0, 0, 0, 1]},
      "objectLayer": "static"
    }
  ],
  "dynamicPrototypes": [
    {
      "protoId": "crate_script",
      "shape": {"type": "box", "params": {"halfExtent": [0.5, 0.5, 0.5]}},
      "mass": 1.0,
      "material": {"friction": 0.4, "restitution": 0.1},
      "objectLayer": "dynamic"
    }
  ],
  "dynamicBodies": [
    {
      "id": "crate_instance",
      "protoId": "crate_script",
      "transform": {"position": [0, 2, 0], "rotation": [0, 0, 0, 1]},
      "userData": 77
    }
  ],
  "constraints": [
    {"type": "fixed", "bodyA": "floor", "bodyB": "crate_instance", "pivot": [0, 1, 0], "axis": [0, 1, 0]}
  ]
}
]]
local materials_json = [[
[
  {"name": "script_mat", "friction": 0.4, "restitution": 0.1}
]
]]
local materials = physics.parse_materials_json(materials_json)

assert(type(cfg.physics) == "table")
assert(type(cfg.threading) == "table")
assert(type(cfg.log) == "table")
assert(type(cfg.thresholds) == "table")
assert(type(dump) == "string" and #dump > 0)
assert(status.initialized == true)
assert(status.running == false)
assert(slots.system and slots.thread and slots.world and slots.script_vm and slots.all)
assert(thread.running == false)
assert(thread.is_physics_thread == false)
assert(phys.fixed_delta_time > 0)
assert(phys.fixedDeltaTime == phys.fixed_delta_time)
assert(phys.layerConfig.objectLayers.dynamic == phys.layer_config.object_layers.dynamic)
assert(threading.command_queue_size > 0)
assert(threading.commandQueueSize == threading.command_queue_size)
assert(log.level ~= nil)
assert(thresholds.position_epsilon > 0)
assert(thresholds.positionEpsilon == thresholds.position_epsilon)
assert(type(paths.assetsPath) == "string" and #paths.assetsPath > 0)
assert(scene.counts.dynamicPrototypes >= 1)
assert(scene_by_path.counts.staticBodies >= 1)
assert(parsed_scene.counts.materials == 1)
assert(parsed_scene.materials[1].name == "script_mat")
local scene_validation = physics.validate_scene_asset_json(scene_query_json, "query_scene.json")
assert(scene_validation.valid == true and scene_validation.counts.staticBodies == 1)
local bad_scene_validation = physics.validate_scene_asset_json([[{
  "dynamicBodies": [
    {"id": "bad_body", "protoId": "missing", "transform": {"position": [0, 0, 0], "rotation": [0, 0, 0, 1]}}
  ]
}]], "bad_scene.json")
assert(bad_scene_validation.valid == false and bad_scene_validation.error:find("prototype") ~= nil)
assert(physics.find_scene_material_json(scene_query_json, "script_mat").name == "script_mat")
assert(physics.find_scene_static_body_json(scene_query_json, "floor").objectLayer == "static")
assert(physics.find_scene_dynamic_prototype_json(scene_query_json, "crate_script").protoId == "crate_script")
assert(physics.find_scene_dynamic_body_json(scene_query_json, "crate_instance").userData == 77)
assert(physics.get_scene_constraint_json(scene_query_json, 1).bodyA == "floor")
assert(physics.find_scene_dynamic_body_json(scene_query_json, "missing") == nil)
assert(materials.count == 1)
assert(materials.empty == false)
assert(math.abs(materials.byName.script_mat.friction - 0.4) < 0.0001)
assert(materials.entries[1].name == "script_mat")
local registry = physics.list_registry()
local body_ids = physics.list_registered_body_ids()
local asset_names = physics.list_registered_asset_names()
assert(type(registry) == "table" and #registry >= 1 and registry[1].bodyId ~= nil)
assert(type(body_ids) == "table" and #body_ids == physics.get_registry_size())
assert(type(asset_names) == "table" and #asset_names >= 1)
assert(physics.has_asset_name(asset_names[1]) == true)
assert(physics.get_body_id(asset_names[1]) ~= nil)
local prototype_ids = physics.list_prototype_ids()
assert(type(prototype_ids) == "table" and #prototype_ids >= 1)
assert(physics.has_prototype(prototype_ids[1]) == true)
assert(physics.has_prototype("__missing__") == false)
assert(physics.has_material_in_json(materials_json, "script_mat") == true)
assert(physics.has_material_in_json(materials_json, "missing") == false)
local material = physics.get_material_in_json(materials_json, "script_mat")
assert(material.name == "script_mat" and math.abs(material.restitution - 0.1) < 0.0001)
local vec3 = physics.parse_vec3({1, 2, 3})
local fvec3 = physics.parse_float_vec3({4, 5, 6})
local quat = physics.normalize_quat({0, 0, 0, 2})
local parsed_quat = physics.parse_quat({0, 0, 0, 0})
assert(vec3.x == 1 and vec3[2] == 2)
assert(fvec3.z == 6)
assert(math.abs(quat.w - 1.0) < 0.0001)
assert(parsed_quat.w == 1)
assert(physics.is_positive_finite(1.0) == true)
assert(physics.is_positive_finite(0.0) == false)
assert(physics.is_finite_float(1.0) == true)
assert(physics.is_finite_double_vec({1, 2, 3}, 3) == true)
assert(physics.is_finite_float_vec({1, 2, 3}, 3) == true)
assert(physics.is_material_valid({friction = 0.4, restitution = 0.1}) == true)
assert(physics.parse_motion_type("kinematic").name == "kinematic")
assert(physics.parse_motion_quality("linearCast").name == "linear_cast")
assert(physics.is_motion_type_name("dynamic") == true)
assert(physics.is_motion_quality_name("discrete") == true)
assert(physics.build_allowed_dofs({0, 1, 2}) > 0)
assert(physics.resolve_object_layer("dynamic") == phys.layerConfig.objectLayers.dynamic)
assert(physics.get_broad_phase_layer("dynamic") == phys.layerConfig.broadPhaseLayers.dynamic)
assert(physics.object_layers_should_collide("static", "dynamic") == true)
assert(physics.object_layers_should_collide("static", "static") == false)
assert(physics.object_vs_broad_phase_should_collide("dynamic", "static") == true)
assert(physics.get_asset_directory(paths.assetsPath) ~= "")
assert(physics.resolve_asset_path(scene.assetsDir, "scene.json") ~= "")
local diff = physics.generate_diff(7, {
	position = {1, 0, 0},
	rotation = {0, 0, 0, 1},
	linear_velocity = {1, 0, 0},
	angular_velocity = {0, 1, 0}
}, {
	position = {0, 0, 0},
	rotation = {0, 0, 0, 1},
	linear_velocity = {0, 0, 0},
	angular_velocity = {0, 0, 0}
}, {
	position_epsilon = 0.001,
	linear_velocity_epsilon = 0.001,
	angular_velocity_epsilon = 0.001
})
assert(type(diff) == "table" and diff.object_id == 7 and #diff.values >= 3)
assert(physics.COMMAND_SPAWN == "spawn")
assert(physics.COLLISION_START == "start")
assert(type(physics.log_info) == "function")
assert(type(physics.enqueue_spawn) == "function")
assert(type(physics.tick) == "function")
assert(type(physics.fetch_result) == "function")
local unavailable_state, unavailable_state_err = physics.save_state()
assert(unavailable_state == nil and type(unavailable_state_err) == "string")
local spawn_args = physics.make_spawn_args("crate", 1, 2, 3, 0, 0, 0, 1, 99)
assert(spawn_args.type == physics.COMMAND_SPAWN)
assert(spawn_args.protoId == "crate")
assert(spawn_args.position[3] == 3)
assert(spawn_args.rotation.w == 1)
assert(spawn_args.userData == 99)
assert(physics.make_destroy_args(5).bodyId == 5)
assert(physics.make_apply_force_args(5, 1, 2, 3, 4, 5, 6).force.y == 2)
assert(physics.make_set_velocity_args(5, 7, 8, 9).velocity.z == 9)
assert(physics.make_tick_args(42, 0.016).frameId == 42)
local transform_packet = physics.make_body_transform(7, 1, 2, 3, 0, 0, 0, 1)
assert(transform_packet.bodyId == 7 and transform_packet.position[2] == 2)
local collision = physics.make_collision_event(1, 2, physics.COLLISION_PERSIST, {
	{1, 2, 3},
	{x = 4, y = 5, z = 6}
})
assert(collision.type == physics.COLLISION_PERSIST)
assert(collision.contactPoints[2].z == 6 and collision.points[1][1] == 1)
local packet = physics.make_diff_packet(9, physics.DIFF_POSITION, {1, 2, 3})
assert(packet.objectId == 9 and packet.changeMask == physics.DIFF_POSITION)
local frame = physics.make_frame_result(12)
assert(frame.frameId == 12 and #frame.transforms == 0 and #frame.diffPackets == 0)
assert(physics.command_type_name(physics.COMMAND_TICK) == physics.COMMAND_TICK)
assert(physics.collision_type_name(physics.COLLISION_END) == physics.COLLISION_END)
local load_result = physics.make_asset_load_result(true, "", 1, 2, 3, 4, 5)
assert(load_result.success == true and load_result.counts.sceneObjects == 5)
local body_record = physics.make_scene_body_record(99, "crate_instance", {1, 2, 3}, {0, 0, 0, 1}, {4, 5, 6}, {7, 8, 9}, true)
assert(body_record.bodyId == 99 and body_record.assetId == "crate_instance")
assert(body_record.position[2] == 2 and body_record.linearVelocity.z == 6 and body_record.dynamic == true)

return table.concat({
	tostring(status.initialized),
	tostring(status.running),
	tostring(slots.all),
	tostring(phys.fixed_delta_time > 0),
	tostring(cfg.physics.max_bodies >= 1)
}, "|")
)lua";

	bool ok = vm->DoString(script, "physics_bind_config_test", &error, &result);
	INFO(error);
	REQUIRE(ok);
	REQUIRE(result == "true|false|true|true|true");

	bridge.Shutdown();
}

TEST_CASE("PhysicsScriptVM physics thread bindings use custom ptr world",
		  "[integration][physics][script]") {
	const std::string script_dir = PreparePhysicsBindingScriptDir();
	auto& bridge = StartFreshPhysics(kPhysicsConfigDir, script_dir);

	auto stats = bridge.GetPhysicsStats();
	REQUIRE(stats.total_bodies >= 1);

	bridge.Shutdown();
}

TEST_CASE("Physics bridge Tick completes without error", "[integration][physics]") {
    auto& bridge = StartFreshPhysics();

    REQUIRE(bridge.Tick(1, 0.016f));

    bridge.Shutdown();
}

TEST_CASE("Physics bridge command helpers spawn query and destroy body", "[integration][physics]") {
	auto& bridge = StartFreshPhysics();

	REQUIRE(bridge.EnqueueSpawn("crate", 0.0, 2.0, 0.0));

	uint64_t frame_id = 1000;
	REQUIRE(bridge.Tick(frame_id, 0.016f));
	auto result = bridge.FetchResult(frame_id, 250);
	REQUIRE(result.has_value());
	REQUIRE(result->error.empty());
	REQUIRE_FALSE(result->transforms.empty());

	uint32_t body_id = result->transforms.front().body_id;
	REQUIRE(bridge.GetTransform(body_id).has_value());
	REQUIRE(bridge.GetVelocity(body_id).has_value());

	auto stats = bridge.GetPhysicsStats();
	REQUIRE(stats.total_bodies >= 1);

	REQUIRE(bridge.EnqueueSetVelocity(body_id, 1.0f, 0.0f, 0.0f));
	REQUIRE(bridge.EnqueueApplyForce(body_id, 0.0f, 10.0f, 0.0f, 0.0, 2.0, 0.0));

	++frame_id;
	REQUIRE(bridge.Tick(frame_id, 0.016f));
	REQUIRE(bridge.FetchResult(frame_id, 250).has_value());

	REQUIRE(bridge.EnqueueDestroy(body_id));
	++frame_id;
	REQUIRE(bridge.Tick(frame_id, 0.016f));
	REQUIRE(bridge.FetchResult(frame_id, 250).has_value());
	REQUIRE_FALSE(bridge.GetTransform(body_id).has_value());

	bridge.Shutdown();
}

TEST_CASE("Physics bridge loads full resource data and simulates spawned prototypes",
		  "[integration][physics][data]") {
	const std::string full_config_dir = PrepareFullPhysicsConfig();
	auto& bridge = StartFreshPhysics(full_config_dir);

	const auto initial_stats = bridge.GetPhysicsStats();
	constexpr uint32_t kExpectedSceneBodies = 8;
	REQUIRE(initial_stats.total_bodies >= kExpectedSceneBodies);

	auto ground_hit = bridge.RayCast(0.0, 8.0, 0.0, 0.0, -1.0, 0.0, 20.0f);
	REQUIRE(ground_hit.has_value());

	struct SpawnCase {
		const char* proto_id;
		double x;
		double y;
		double z;
	};
	const std::vector<SpawnCase> spawn_cases = {
		{"crate_full", -5.0, 4.0, 3.0},
		{"ball_full", -3.0, 4.0, 3.0},
		{"capsule_full", -1.0, 4.0, 3.0},
		{"cylinder_full", 1.0, 4.0, 3.0},
		{"hull_full", 3.0, 4.0, 3.0},
		{"compound_full", 5.0, 4.0, 3.0},
	};

	for (const auto& spawn : spawn_cases) {
		REQUIRE(bridge.EnqueueSpawn(spawn.proto_id, spawn.x, spawn.y, spawn.z));
	}

	constexpr uint64_t kSpawnFrame = 5000;
	REQUIRE(bridge.Tick(kSpawnFrame, bridge.GetFixedDeltaTime()));
	auto spawn_result = bridge.FetchResult(kSpawnFrame, 500);
	REQUIRE(spawn_result.has_value());
	INFO(spawn_result->error);
	REQUIRE(spawn_result->error.empty());
	REQUIRE(spawn_result->frame_id == kSpawnFrame);
	REQUIRE(spawn_result->transforms.size() >= spawn_cases.size());

	const auto after_spawn_stats = bridge.GetPhysicsStats();
	REQUIRE(after_spawn_stats.total_bodies >=
			initial_stats.total_bodies + static_cast<uint32_t>(spawn_cases.size()));

	const uint32_t body_id = spawn_result->transforms.front().body_id;
	REQUIRE(bridge.GetTransform(body_id).has_value());
	REQUIRE(bridge.GetVelocity(body_id).has_value());
	REQUIRE(bridge.EnqueueSetVelocity(body_id, 0.25f, 0.0f, 0.0f));
	REQUIRE(bridge.EnqueueApplyForce(body_id,
									 0.0f,
									 8.0f,
									 0.0f,
									 spawn_result->transforms.front().pos_x,
									 spawn_result->transforms.front().pos_y,
									 spawn_result->transforms.front().pos_z));

	constexpr uint64_t kForceFrame = kSpawnFrame + 1;
	REQUIRE(bridge.Tick(kForceFrame, bridge.GetFixedDeltaTime()));
	auto force_result = bridge.FetchResult(kForceFrame, 500);
	REQUIRE(force_result.has_value());
	INFO(force_result->error);
	REQUIRE(force_result->error.empty());

	bridge.Shutdown();
}

TEST_CASE("Physics bridge rejects invalid tick input", "[integration][physics]") {
	auto& bridge = StartFreshPhysics();
	REQUIRE_FALSE(bridge.Tick(42, 0.0f));
	bridge.Shutdown();
}

TEST_CASE("Physics bridge recover restarts a healthy physics thread", "[integration][physics]") {
	auto& bridge = StartFreshPhysics();
	REQUIRE(bridge.Recover());
	REQUIRE(bridge.IsRunning());
	REQUIRE(bridge.IsHealthy());
	bridge.Shutdown();
}

#endif  // ENGINE_PHYSICS_ENABLED

// Fallback when physics is disabled
#ifndef ENGINE_PHYSICS_ENABLED
TEST_CASE("Physics integration tests skipped (engine built without physics)", "[integration][physics]") {
    SUCCEED("Physics is disabled — skipping integration tests");
}
#endif
