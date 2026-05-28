#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "log_init.h"
#include "runtime/entity/attribute.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_id.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/core/timer/timer_manager.h"
#include "runtime/evpp/tcp_conn.h"

using namespace engine::entity;
using namespace std::chrono;

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
	attrs.Remove("x");
	REQUIRE_FALSE(attrs.Has("x"));
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

	e.RemoveComponent<TestComponent>();
	REQUIRE(e.GetComponent<TestComponent>() == nullptr);
}

TEST_CASE("Entity Lua component add and get", "[entity][component]") {
	Entity e(1);
	REQUIRE(e.GetLuaComponent("inventory") == -1);

	e.AddLuaComponent("inventory", 999);
	REQUIRE(e.GetLuaComponent("inventory") == 999);

	e.RemoveLuaComponent("inventory");
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

TEST_CASE("EntityManager DestroyEntity removes single entity", "[entity][manager]") {
	auto& mgr = EntityManager::Instance();

	auto* e1 = mgr.CreateEntity();
	mgr.CreateEntity();
	REQUIRE(mgr.Count() == 2);

	mgr.DestroyEntity(e1->GetId());
	REQUIRE(mgr.Count() == 1);
	REQUIRE(mgr.GetEntity(e1->GetId()) == nullptr);

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

// Static assertions

TEST_CASE("EntityId is 64-bit", "[entity][id]") {
	STATIC_REQUIRE(sizeof(EntityId) == 8);
}

TEST_CASE("kInvalidEntityId is zero", "[entity][id]") {
	REQUIRE(kInvalidEntityId == 0);
}
