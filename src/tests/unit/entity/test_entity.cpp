#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "log_init.h"
#include "runtime/entity/attribute.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_id.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/evpp/tcp_conn.h"
#include "runtime/script/entity_bind.h"
#include "runtime/vm/vm.h"

using namespace engine::entity;
using namespace std::chrono;

namespace {

bool RunLua(engine::ScriptVM& vm, const std::string& code, std::string& result) {
	return vm.DoString(code, "test_entity_lua", nullptr, &result);
}

}  // namespace

// EntityId & Allocator tests

TEST_CASE("SequentialIdAllocator generates unique IDs", "[entity][id]") {
	SequentialIdAllocator alloc;
	EntityId a = alloc.Allocate();
	EntityId b = alloc.Allocate();
	EntityId c = alloc.Allocate();
	REQUIRE(a != b);
	REQUIRE(b != c);
	REQUIRE(a != c);
}

TEST_CASE("SequentialIdAllocator starts at 1", "[entity][id]") {
	SequentialIdAllocator alloc;
	REQUIRE(alloc.Allocate() == 1);
}

// AttributeTable tests

TEST_CASE("AttributeTable set and get", "[entity][attribute]") {
	AttributeTable attrs;
	REQUIRE_FALSE(attrs.Has("hp"));

	attrs.Set("hp", AttrValue{int64_t(100)});
	REQUIRE(attrs.Has("hp"));

	auto val = attrs.Get("hp");
	REQUIRE(std::get<int64_t>(val) == 100);
}

TEST_CASE("AttributeTable returns default for missing key", "[entity][attribute]") {
	AttributeTable attrs;
	auto val = attrs.Get("missing", AttrValue{int64_t(42)});
	REQUIRE(std::get<int64_t>(val) == 42);
}

TEST_CASE("AttributeTable supports multiple types", "[entity][attribute]") {
	AttributeTable attrs;
	attrs.Set("int_val", AttrValue{int64_t(100)});
	attrs.Set("double_val", AttrValue{3.14});
	attrs.Set("string_val", AttrValue{std::string("hello")});
	attrs.Set("bool_val", AttrValue{true});

	REQUIRE(std::get<int64_t>(attrs.Get("int_val")) == 100);
	REQUIRE(std::get<double>(attrs.Get("double_val")) == 3.14);
	REQUIRE(std::get<std::string>(attrs.Get("string_val")) == "hello");
	REQUIRE(std::get<bool>(attrs.Get("bool_val")) == true);
}

TEST_CASE("AttributeTable remove", "[entity][attribute]") {
	AttributeTable attrs;
	attrs.Set("x", AttrValue{int64_t(1)});
	REQUIRE(attrs.Has("x"));
	REQUIRE(attrs.Remove("x"));
	REQUIRE_FALSE(attrs.Has("x"));
	REQUIRE_FALSE(attrs.Remove("x"));
}

TEST_CASE("AttributeTable keys are sorted for deterministic iteration", "[entity][attribute]") {
	AttributeTable attrs;
	attrs.Set("z", AttrValue{int64_t(1)});
	attrs.Set("a", AttrValue{int64_t(2)});
	attrs.Set("m", AttrValue{int64_t(3)});

	auto keys = attrs.Keys();
	REQUIRE(keys == std::vector<std::string>{"a", "m", "z"});
}

TEST_CASE("AttributeTable change callback", "[entity][attribute]") {
	AttributeTable attrs;
	attrs.SetOwnerId(42);

	int call_count = 0;
	EntityId last_id = 0;
	std::string last_key;
	int64_t last_old = 0;
	int64_t last_new = 0;

	attrs.SetChangeCallback([&](EntityId id, const std::string& key,
								 const AttrValue& old_val, const AttrValue& new_val) {
		call_count++;
		last_id = id;
		last_key = key;
		last_old = std::get<int64_t>(old_val);
		last_new = std::get<int64_t>(new_val);
	});

	attrs.Set("hp", AttrValue{int64_t(100)});
	REQUIRE(call_count == 0);  // new key, no change callback for initial set

	attrs.Set("hp", AttrValue{int64_t(80)});
	REQUIRE(call_count == 1);
	REQUIRE(last_id == 42);
	REQUIRE(last_key == "hp");
	REQUIRE(last_old == 100);
	REQUIRE(last_new == 80);

	attrs.Set("hp", AttrValue{int64_t(80)});
	REQUIRE(call_count == 1);  // no-op updates should not dirty the entity
}

TEST_CASE("AttributeTable TryGet distinguishes missing from default value", "[entity][attribute]") {
	AttributeTable attrs;
	REQUIRE(attrs.TryGet("missing") == nullptr);

	attrs.Set("zero", AttrValue{int64_t(0)});
	auto* val = attrs.TryGet("zero");
	REQUIRE(val != nullptr);
	REQUIRE(std::get<int64_t>(*val) == 0);
}

// Entity lifecycle tests

TEST_CASE("Entity lifecycle: Created → Active → Suspended → Destroyed", "[entity][lifecycle]") {
	Entity e(1);
	REQUIRE(e.GetId() == 1);
	REQUIRE(e.GetState() == EntityState::Created);

	e.Activate();
	REQUIRE(e.GetState() == EntityState::Active);

	e.Suspend();
	REQUIRE(e.GetState() == EntityState::Suspended);

	e.Activate();  // can re-activate from suspended
	REQUIRE(e.GetState() == EntityState::Active);

	e.Destroy();
	REQUIRE(e.GetState() == EntityState::Destroyed);
}

TEST_CASE("Entity Destroy is idempotent", "[entity][lifecycle]") {
	Entity e(1);
	e.Destroy();
	REQUIRE(e.GetState() == EntityState::Destroyed);
	e.Destroy();  // should not crash
	REQUIRE(e.GetState() == EntityState::Destroyed);
}

TEST_CASE("Entity attribute access", "[entity][lifecycle]") {
	Entity e(1);
	e.Attrs().Set("name", AttrValue{std::string("test_entity")});
	e.Attrs().Set("level", AttrValue{int64_t(5)});

	REQUIRE(std::get<std::string>(e.Attrs().Get("name")) == "test_entity");
	REQUIRE(std::get<int64_t>(e.Attrs().Get("level")) == 5);
}

TEST_CASE("Entity Destroy clears attributes and state helpers reflect lifecycle", "[entity][lifecycle]") {
	Entity e(1);
	REQUIRE(e.IsCreated());
	e.Activate();
	REQUIRE(e.IsActive());
	e.Attrs().Set("name", AttrValue{std::string("temporary")});
	REQUIRE(e.Attrs().Count() == 1);

	e.Destroy();
	REQUIRE(e.IsDestroyed());
	REQUIRE(e.Attrs().Count() == 0);
}
// Entity Component tests

struct TestComponent {
	int value = 0;
	explicit TestComponent(int v) : value(v) {}
};

TEST_CASE("Entity add, get, and remove typed component", "[entity][component]") {
	Entity e(1);

	auto* comp = e.AddComponent(std::make_unique<TestComponent>(42));
	REQUIRE(comp != nullptr);
	REQUIRE(comp->value == 42);

	auto* found = e.GetComponent<TestComponent>();
	REQUIRE(found != nullptr);
	REQUIRE(found->value == 42);
	REQUIRE(e.HasComponent<TestComponent>());

	e.RemoveComponent<TestComponent>();
	REQUIRE(e.GetComponent<TestComponent>() == nullptr);
	REQUIRE_FALSE(e.HasComponent<TestComponent>());
}

TEST_CASE("Entity Lua component add and get", "[entity][component]") {
	Entity e(1);
	REQUIRE(e.GetLuaComponent("inventory") == -1);

	e.AddLuaComponent("inventory", 999);
	REQUIRE(e.GetLuaComponent("inventory") == 999);

	e.RemoveLuaComponent("inventory");
	REQUIRE(e.GetLuaComponent("inventory") == -1);
}

TEST_CASE("Entity Destroy clears typed and Lua components", "[entity][component][lifecycle]") {
	Entity e(1);
	e.AddComponent(std::make_unique<TestComponent>(7));
	e.AddLuaComponent("inventory", 123);

	REQUIRE(e.ComponentCount() == 1);
	REQUIRE(e.LuaComponentCount() == 1);

	e.Destroy();

	REQUIRE(e.ComponentCount() == 0);
	REQUIRE(e.LuaComponentCount() == 0);
	REQUIRE(e.GetComponent<TestComponent>() == nullptr);
	REQUIRE(e.GetLuaComponent("inventory") == -1);
}

// Entity timer ownership tests
// Note: actual timer firing is tested by the TimerManager unit tests;
// here we verify that Entity correctly tracks owned timer IDs.

TEST_CASE("Entity AddTimer returns non-zero timer ID", "[entity][timer]") {
	engine::TimerManager tm;
	tm.initialize();
	Entity e(1);
	e.SetTimerManager(&tm);
	e.Activate();

	engine::TimerId tid = e.AddTimer(100, false, []() {});
	REQUIRE(tid != engine::kInvalidTimerId);
}

TEST_CASE("Entity AddTimer without TimerManager is safe", "[entity][timer]") {
	Entity e(1);
	e.Activate();

	REQUIRE(e.AddTimer(100, false, []() {}) == engine::kInvalidTimerId);
	REQUIRE(e.OwnedTimerCount() == 0);
}

TEST_CASE("Entity Destroy is safe with owned timers", "[entity][timer]") {
	Entity e(1);
	e.Activate();

	// Create several timers — TimerManager must be live
	engine::TimerManager tm2;
	tm2.initialize();
	e.SetTimerManager(&tm2);

	e.AddTimer(100, false, []() {});
	e.AddTimer(200, true, []() {});
	e.AddTimer(300, false, []() {});

	// Destroy should cancel all timers without crashing
	REQUIRE_NOTHROW(e.Destroy());
	REQUIRE(e.GetState() == EntityState::Destroyed);
	REQUIRE(e.OwnedTimerCount() == 0);
}

// EntityManager tests

TEST_CASE("EntityManager creates and retrieves entities", "[entity][manager]") {
	auto& mgr = EntityManager::Instance();

	auto* e1 = mgr.CreateEntity();
	auto* e2 = mgr.CreateEntity();

	REQUIRE(e1 != nullptr);
	REQUIRE(e2 != nullptr);
	REQUIRE(e1->GetId() != e2->GetId());

	REQUIRE(mgr.GetEntity(e1->GetId()) == e1);
	REQUIRE(mgr.GetEntity(e2->GetId()) == e2);
	REQUIRE(mgr.GetEntity(99999) == nullptr);
	REQUIRE(mgr.Count() == 2);

	mgr.DestroyAll();
	REQUIRE(mgr.Count() == 0);
}

TEST_CASE("EntityManager destroy hook fires once for manager-owned entities only",
		  "[entity][manager][lifecycle]") {
	auto& mgr = EntityManager::Instance();
	mgr.DestroyAll();

	int calls = 0;
	EntityId last_id = kInvalidEntityId;
	mgr.SetDestroyHook([&](Entity& e) {
		++calls;
		last_id = e.GetId();
	});

	Entity unmanaged(1000);
	unmanaged.Destroy();
	REQUIRE(calls == 0);

	auto* direct = mgr.CreateEntity();
	REQUIRE(direct != nullptr);
	EntityId direct_id = direct->GetId();
	direct->Destroy();
	REQUIRE(calls == 1);
	REQUIRE(last_id == direct_id);
	direct->Destroy();
	REQUIRE(calls == 1);

	auto* managed = mgr.CreateEntity();
	REQUIRE(managed != nullptr);
	EntityId managed_id = managed->GetId();
	mgr.DestroyEntity(managed_id);
	REQUIRE(calls == 2);
	REQUIRE(last_id == managed_id);

	mgr.SetDestroyHook({});
	mgr.DestroyAll();
}

TEST_CASE("EntityManager DestroyEntity removes single entity", "[entity][manager]") {
	auto& mgr = EntityManager::Instance();

	auto* e1 = mgr.CreateEntity();
	mgr.CreateEntity();
	EntityId e1_id = e1->GetId();
	REQUIRE(mgr.Count() == 2);

	mgr.DestroyEntity(e1_id);
	REQUIRE(mgr.Count() == 1);
	REQUIRE(mgr.GetEntity(e1_id) == nullptr);

	mgr.DestroyAll();
}

TEST_CASE("EntityManager SetTimerManager updates existing entities", "[entity][manager][timer]") {
	auto& mgr = EntityManager::Instance();
	mgr.DestroyAll();

	auto* e = mgr.CreateEntity();
	REQUIRE(e != nullptr);
	REQUIRE(e->GetTimerManager() == nullptr);

	engine::TimerManager tm;
	tm.initialize();
	mgr.SetTimerManager(&tm);
	REQUIRE(e->GetTimerManager() == &tm);

	mgr.SetTimerManager(nullptr);
	mgr.DestroyAll();
}

TEST_CASE("EntityManager CreateEntity with explicit ID", "[entity][manager]") {
	auto& mgr = EntityManager::Instance();

	auto* e = mgr.CreateEntity(42);
	REQUIRE(e != nullptr);
	REQUIRE(e->GetId() == 42);
	REQUIRE(mgr.GetEntity(42) == e);

	// Duplicate ID should fail
	auto* dup = mgr.CreateEntity(42);
	REQUIRE(dup == nullptr);

	mgr.DestroyAll();
}

TEST_CASE("EntityManager auto allocation skips explicit ID collisions", "[entity][manager][id]") {
	auto& mgr = EntityManager::Instance();
	mgr.DestroyAll();

	auto* first = mgr.CreateEntity();
	REQUIRE(first != nullptr);
	EntityId colliding_id = first->GetId() + 1;
	auto* explicit_entity = mgr.CreateEntity(colliding_id);
	REQUIRE(explicit_entity != nullptr);

	auto* automatic = mgr.CreateEntity();
	REQUIRE(automatic != nullptr);
	REQUIRE(automatic->GetId() != colliding_id);

	mgr.DestroyAll();
}

TEST_CASE("EntityManager ForEachActive only visits active entities", "[entity][manager]") {
	auto& mgr = EntityManager::Instance();

	auto* e1 = mgr.CreateEntity();
	e1->Activate();
	auto* e2 = mgr.CreateEntity();  // stays Created
	auto* e3 = mgr.CreateEntity();
	e3->Activate();

	int count = 0;
	mgr.ForEachActive([&count](Entity& e) {
		count++;
		REQUIRE(e.GetState() == EntityState::Active);
	});

	REQUIRE(count == 2);

	mgr.DestroyAll();
}

TEST_CASE("EntityManager ForEachActive tolerates entity destruction in callback", "[entity][manager]") {
	auto& mgr = EntityManager::Instance();
	mgr.DestroyAll();

	for (int i = 0; i < 3; ++i) {
		auto* e = mgr.CreateEntity();
		REQUIRE(e != nullptr);
		e->Activate();
	}

	int visited = 0;
	mgr.ForEachActive([&](Entity& e) {
		++visited;
		mgr.DestroyEntity(e.GetId());
	});

	REQUIRE(visited == 3);
	REQUIRE(mgr.Count() == 0);
}

TEST_CASE("EntityManager ActiveCount", "[entity][manager]") {
	auto& mgr = EntityManager::Instance();

	auto* e = mgr.CreateEntity();
	REQUIRE(mgr.ActiveCount() == 0);

	e->Activate();
	REQUIRE(mgr.ActiveCount() == 1);

	e->Destroy();
	REQUIRE(mgr.ActiveCount() == 0);

	mgr.DestroyAll();
}

TEST_CASE("EntityManager connection binding and lookup", "[entity][manager]") {
	auto& mgr = EntityManager::Instance();

	auto* e = mgr.CreateEntity();
	REQUIRE(e != nullptr);

	REQUIRE(mgr.FindByConnection(nullptr) == nullptr);

	e->UnbindConnection();
	REQUIRE(e->GetConnection() == nullptr);

	mgr.DestroyAll();
}

TEST_CASE("Unmanaged entity connection binding does not touch EntityManager index",
		  "[entity][manager][connection]") {
	auto& mgr = EntityManager::Instance();
	mgr.DestroyAll();

	auto conn = std::make_shared<evpp::TCPConn>(nullptr, "entity-test", -1, "local", "remote", 1);
	Entity unmanaged(5000);
	unmanaged.BindConnection(conn);
	REQUIRE(mgr.FindByConnection(conn) == nullptr);

	auto* managed = mgr.CreateEntity();
	REQUIRE(managed != nullptr);
	managed->BindConnection(conn);
	REQUIRE(mgr.FindByConnection(conn) == managed);
	REQUIRE(mgr.FindByConnection(conn.get()) == managed);

	unmanaged.UnbindConnection();
	REQUIRE(mgr.FindByConnection(conn) == managed);

	mgr.DestroyAll();
}

TEST_CASE("EntityManager physics body binding stays one-to-one", "[entity][manager][physics]") {
	auto& mgr = EntityManager::Instance();
	mgr.DestroyAll();

	auto* e1 = mgr.CreateEntity();
	auto* e2 = mgr.CreateEntity();
	REQUIRE(e1 != nullptr);
	REQUIRE(e2 != nullptr);

	mgr.RegisterPhysicsBodyBinding(100, e1->GetId());
	REQUIRE(e1->HasPhysicsBody());
	REQUIRE(e1->GetPhysicsBodyId() == 100);
	REQUIRE(mgr.FindByPhysicsBodyId(100) == e1);

	mgr.RegisterPhysicsBodyBinding(100, e2->GetId());
	REQUIRE_FALSE(e1->HasPhysicsBody());
	REQUIRE(e2->HasPhysicsBody());
	REQUIRE(mgr.FindByPhysicsBodyId(100) == e2);

	mgr.UnregisterPhysicsBodyBinding(100);
	REQUIRE_FALSE(e2->HasPhysicsBody());
	REQUIRE(mgr.FindByPhysicsBodyId(100) == nullptr);

	mgr.DestroyAll();
}

TEST_CASE("Entity direct Destroy unregisters physics body binding", "[entity][manager][physics]") {
	auto& mgr = EntityManager::Instance();
	mgr.DestroyAll();

	auto* e = mgr.CreateEntity();
	REQUIRE(e != nullptr);
	mgr.RegisterPhysicsBodyBinding(200, e->GetId());
	REQUIRE(mgr.FindByPhysicsBodyId(200) == e);

	e->Destroy();

	REQUIRE_FALSE(e->HasPhysicsBody());
	REQUIRE(mgr.FindByPhysicsBodyId(200) == nullptr);

	mgr.DestroyAll();
}

TEST_CASE("Lua entity get_attr returns nil for missing attributes", "[entity][lua]") {
	EntityManager::Instance().DestroyAll();
	engine::ScriptVM vm;
	engine::script::ExportEntity(vm);

	std::string result;
	REQUIRE(RunLua(vm,
				   "local e = entity.create()\n"
				   "return tostring(e:get_attr('missing'))",
				   result));
	REQUIRE(result == "nil");

	engine::script::ShutdownEntityBindings();
}

TEST_CASE("Lua entity destroy releases entity and components", "[entity][lua]") {
	EntityManager::Instance().DestroyAll();
	engine::ScriptVM vm;
	engine::script::ExportEntity(vm);

	std::string result;
	REQUIRE(RunLua(vm,
				   "local e = entity.create()\n"
				   "e:add_component('bag', {slots = 8})\n"
				   "local id = e:get_id()\n"
				   "local ok = e:destroy()\n"
				   "return tostring(ok) .. ',' .. tostring(id)",
				   result));
	REQUIRE(result.find("true,") == 0);
	REQUIRE(EntityManager::Instance().Count() == 0);

	engine::script::ShutdownEntityBindings();
}

TEST_CASE("Lua entity attribute convenience helpers", "[entity][lua]") {
	EntityManager::Instance().DestroyAll();
	engine::ScriptVM vm;
	engine::script::ExportEntity(vm);

	std::string result;
	REQUIRE(RunLua(vm,
				   "local e = entity.create()\n"
				   "e:set_attr('z', 1)\n"
				   "e:set_attr('a', true)\n"
				   "local keys = e:list_attrs()\n"
				   "local removed = e:remove_attr('z')\n"
				   "return tostring(removed) .. ',' .. tostring(e:attr_count()) .. ',' .. "
				   "table.concat(keys, '|')",
				   result));
	REQUIRE(result == "true,1,a|z");

	engine::script::ShutdownEntityBindings();
}

TEST_CASE("Lua entity context is invalidated when C++ destroys entity", "[entity][lua]") {
	EntityManager::Instance().DestroyAll();
	engine::ScriptVM vm;
	engine::script::ExportEntity(vm);

	std::string result;
	REQUIRE(RunLua(vm,
				   "e = entity.create()\n"
				   "e:add_component('bag', {slots = 8})\n"
				   "return e:get_id()",
				   result));
	EntityId id = static_cast<EntityId>(std::stoull(result));

	EntityManager::Instance().DestroyEntity(id);
	REQUIRE(EntityManager::Instance().Count() == 0);

	REQUIRE(RunLua(vm,
				   "local ok, err = pcall(function() return e:get_state() end)\n"
				   "return tostring(ok) .. ',' .. tostring(err:find('invalid context', 1, true) ~= nil)",
				   result));
	REQUIRE(result == "false,true");

	engine::script::ShutdownEntityBindings();
}

// Static assertions

TEST_CASE("EntityId is 64-bit", "[entity][id]") {
	STATIC_REQUIRE(sizeof(EntityId) == 8);
}

TEST_CASE("kInvalidEntityId is zero", "[entity][id]") {
	REQUIRE(kInvalidEntityId == 0);
}
