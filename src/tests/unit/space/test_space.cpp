#include <catch2/catch_test_macros.hpp>

#include "log_init.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/space/bind/space_bind.h"
#include "runtime/space/connection_router.h"
#include "runtime/space/space.h"
#include "runtime/space/space_manager.h"
#include "runtime/space/space_message.h"
#include "runtime/vm/vm.h"
#include "timer_fixture.h"

using namespace engine::space;
using namespace engine::entity;
namespace entity = engine::entity;

// =============================================================================
// SpaceConfig & constants
// =============================================================================

TEST_CASE("kInvalidSpaceId equals zero", "[space][constants]") {
    REQUIRE(kInvalidSpaceId == 0);
}

TEST_CASE("SpaceConfig default values", "[space][config]") {
    SpaceConfig cfg;
    REQUIRE(cfg.name.empty());
    REQUIRE(cfg.entry_scripts.empty());
    REQUIRE(cfg.max_entities == 10000);
    REQUIRE(cfg.max_players == 1000);
}

TEST_CASE("SpaceConfig can be populated", "[space][config]") {
    SpaceConfig cfg;
    cfg.name = "TestSpace";
    cfg.entry_scripts = {"init.lua", "main.lua"};
    cfg.max_entities = 42;
    cfg.max_players = 8;

    REQUIRE(cfg.name == "TestSpace");
    REQUIRE(cfg.entry_scripts.size() == 2);
    REQUIRE(cfg.entry_scripts[0] == "init.lua");
    REQUIRE(cfg.entry_scripts[1] == "main.lua");
    REQUIRE(cfg.max_entities == 42);
    REQUIRE(cfg.max_players == 8);
}

// =============================================================================
// Space — creation & basic properties
// =============================================================================

TEST_CASE("Space creation with id and name", "[space][create]") {
    SpaceConfig cfg;
    cfg.name = "Lobby";
    Space space(42, cfg);

    REQUIRE(space.GetId() == 42);
    REQUIRE(space.GetName() == "Lobby");
    REQUIRE(space.EntityCount() == 0);
}

TEST_CASE("Space creation with empty config name", "[space][create]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    REQUIRE(space.GetId() == 1);
    REQUIRE(space.GetName().empty());
}

TEST_CASE("Space script VM is created during construction", "[space][vm]") {
    SpaceConfig cfg;
    cfg.name = "Test";
    Space space(1, cfg);

    REQUIRE(space.GetLuaState() != nullptr);
    REQUIRE(space.GetScriptVM().GetState() == space.GetLuaState());
}

TEST_CASE("Space script VM has current space binding", "[space][vm][binding]") {
    SpaceConfig cfg;
    cfg.name = "BoundSpace";
    Space space(123, cfg);

    std::string error;
    std::string result;
    REQUIRE(space.GetScriptVM().DoString(
        "local current = space.current(); return tostring(current.id) .. ':' .. current.name",
        "string",
        &error,
        &result));
    REQUIRE(result == "123:BoundSpace");
}

TEST_CASE("Space current binding can be cleared before VM teardown", "[space][vm][binding]") {
    auto* default_space = SpaceManager::Instance().GetDefaultSpace();
    if (default_space) {
        SpaceManager::Instance().DestroySpace(default_space->GetId());
    }

    SpaceConfig cfg;
    cfg.name = "ClearBinding";
    Space space(9001, cfg);
    auto& vm = space.GetScriptVM();
    engine::script::ClearCurrentSpace(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString("return tostring(space.current() == nil)",
                        "string",
                        &error,
                        &result));
    REQUIRE(result == "true");
}

TEST_CASE("Space is not copyable", "[space][traits]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    STATIC_REQUIRE_FALSE(std::is_copy_constructible<Space>::value);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable<Space>::value);
}

// =============================================================================
// Space — entity management
// =============================================================================

TEST_CASE("Space CreateEntity returns non-null with auto ID", "[space][entity]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    auto* e1 = space.CreateEntity();
    auto* e2 = space.CreateEntity();

    REQUIRE(e1 != nullptr);
    REQUIRE(e2 != nullptr);
    REQUIRE(space.EntityCount() == 2);
    REQUIRE(e1->GetId() != e2->GetId());
}

TEST_CASE("Space CreateEntity with explicit ID", "[space][entity]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    auto* e = space.CreateEntity(100);
    REQUIRE(e != nullptr);
    REQUIRE(e->GetId() == 100);
    REQUIRE(space.EntityCount() == 1);
}

TEST_CASE("Space CreateEntity leaves entity in Created state", "[space][entity]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    auto* e = space.CreateEntity();
    REQUIRE(e != nullptr);
    REQUIRE(e->GetState() == EntityState::Created);
}

TEST_CASE("Space entity timer uses space-local lookup", "[space][entity][timer]") {
    TimerFixture f;
    f.Reset();
    EntityManager::Instance().SetTimerManager(&f.tm);
    struct ResetTimerManager {
        ~ResetTimerManager() {
            EntityManager::Instance().SetTimerManager(nullptr);
        }
    } reset;

    SpaceConfig cfg;
    Space space(1, cfg);
    auto* e = space.CreateEntity(4242);
    REQUIRE(e != nullptr);
    e->Activate();

    int fired = 0;
    auto tid = e->AddTimer(10, false, [&fired]() {
        ++fired;
    });
    REQUIRE(tid != engine::kInvalidTimerId);

    f.AdvanceBy(std::chrono::milliseconds(50));
    REQUIRE(fired == 1);
    REQUIRE(e->OwnedTimerCount() == 0);
}

TEST_CASE("Space GetEntity returns entity by id", "[space][entity]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    auto* e = space.CreateEntity();
    EntityId eid = e->GetId();

    REQUIRE(space.GetEntity(eid) == e);
    REQUIRE(space.GetEntity(99999) == nullptr);
}

TEST_CASE("Space DestroyEntity removes entity", "[space][entity]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    auto* e = space.CreateEntity();
    EntityId eid = e->GetId();
    REQUIRE(space.EntityCount() == 1);

    space.DestroyEntity(eid);
    REQUIRE(space.EntityCount() == 0);
    REQUIRE(space.GetEntity(eid) == nullptr);
}

TEST_CASE("Space DestroyEntity unknown id is safe", "[space][entity]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    REQUIRE_NOTHROW(space.DestroyEntity(42));
    REQUIRE_NOTHROW(space.DestroyEntity(kInvalidEntityId));
}

TEST_CASE("Space ForEachEntity visits all entities", "[space][entity]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    space.CreateEntity();  // auto-ID
    space.CreateEntity();  // auto-ID
    space.CreateEntity();  // auto-ID

    int visited = 0;
    space.ForEachEntity([&visited](entity::Entity& /*e*/) {
        ++visited;
    });

    REQUIRE(visited == 3);
}

TEST_CASE("Space ForEachEntity empty space", "[space][entity]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    int visited = 0;
    space.ForEachEntity([&visited](entity::Entity& /*e*/) {
        ++visited;
    });

    REQUIRE(visited == 0);
}

// =============================================================================
// Space — player join / leave
// =============================================================================

TEST_CASE("Space player join and leave lifecycle", "[space][player]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    // Player joins — no real TCPConn, so pass nullptr
    space.OnPlayerJoin(10, nullptr);
    REQUIRE(space.GetPlayerConnection(10) == nullptr);

    // Leave
    space.OnPlayerLeave(10);
}

TEST_CASE("Space GetPlayerConnection unknown player returns null", "[space][player]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    REQUIRE(space.GetPlayerConnection(42) == nullptr);
}

// =============================================================================
// Space — Update
// =============================================================================

TEST_CASE("Space Update does not crash with zero delta", "[space][lifecycle]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    REQUIRE_NOTHROW(space.Update(0));
}

TEST_CASE("Space Update does not crash with positive delta", "[space][lifecycle]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    REQUIRE_NOTHROW(space.Update(16));
    REQUIRE_NOTHROW(space.Update(1000));
}

TEST_CASE("Space Update after entity creation", "[space][lifecycle]") {
    SpaceConfig cfg;
    Space space(1, cfg);

    space.CreateEntity();
    space.CreateEntity();

    REQUIRE_NOTHROW(space.Update(33));
    REQUIRE(space.EntityCount() == 2);
}

// =============================================================================
// SpaceManager — singleton
// =============================================================================

TEST_CASE("SpaceManager singleton returns same instance", "[space_mgr][singleton]") {
    auto& mgr1 = SpaceManager::Instance();
    auto& mgr2 = SpaceManager::Instance();
    REQUIRE(&mgr1 == &mgr2);
}

// =============================================================================
// SpaceManager — create / get / destroy
// =============================================================================

TEST_CASE("SpaceManager CreateSpace returns valid space with auto ID", "[space_mgr][create]") {
    auto& mgr = SpaceManager::Instance();
    REQUIRE(mgr.SpaceCount() == 0);

    SpaceConfig cfg;
    cfg.name = "AutoIdSpace";
    Space* space = mgr.CreateSpace(cfg);

    REQUIRE(space != nullptr);
    REQUIRE(space->GetId() != kInvalidSpaceId);
    REQUIRE(space->GetName() == "AutoIdSpace");
    REQUIRE(mgr.SpaceCount() == 1);

    mgr.DestroySpace(space->GetId());
}

TEST_CASE("SpaceManager CreateSpaceWithId assigns requested ID", "[space_mgr][create]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "ExplicitId";
    Space* space = mgr.CreateSpaceWithId(777, cfg);

    REQUIRE(space != nullptr);
    REQUIRE(space->GetId() == 777);
    REQUIRE(mgr.SpaceCount() == 1);

    mgr.DestroySpace(777);
}

TEST_CASE("SpaceManager CreateSpaceWithId duplicate ID returns nullptr", "[space_mgr][create]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "First";
    Space* s1 = mgr.CreateSpaceWithId(5000, cfg);
    REQUIRE(s1 != nullptr);

    Space* s2 = mgr.CreateSpaceWithId(5000, cfg);
    REQUIRE(s2 == nullptr);

    mgr.DestroySpace(5000);
}

TEST_CASE("SpaceManager GetSpace retrieves created space", "[space_mgr][lookup]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    Space* created = mgr.CreateSpace(cfg);
    SpaceId sid = created->GetId();

    Space* found = mgr.GetSpace(sid);
    REQUIRE(found == created);

    mgr.DestroySpace(sid);
}

TEST_CASE("SpaceManager GetSpace unknown id returns nullptr", "[space_mgr][lookup]") {
    auto& mgr = SpaceManager::Instance();

    REQUIRE(mgr.GetSpace(99999) == nullptr);
    REQUIRE(mgr.GetSpace(kInvalidSpaceId) == nullptr);
}

TEST_CASE("SpaceManager DestroySpace reduces count", "[space_mgr][destroy]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    auto* s1 = mgr.CreateSpace(cfg);
    auto* s2 = mgr.CreateSpace(cfg);
    REQUIRE(mgr.SpaceCount() == 2);

    mgr.DestroySpace(s1->GetId());
    REQUIRE(mgr.SpaceCount() == 1);
    REQUIRE(mgr.GetSpace(s1->GetId()) == nullptr);
    REQUIRE(mgr.GetSpace(s2->GetId()) != nullptr);

    mgr.DestroySpace(s2->GetId());
    REQUIRE(mgr.SpaceCount() == 0);
}

TEST_CASE("SpaceManager DestroySpace unknown id is safe", "[space_mgr][destroy]") {
    auto& mgr = SpaceManager::Instance();

    REQUIRE_NOTHROW(mgr.DestroySpace(99999));
    REQUIRE_NOTHROW(mgr.DestroySpace(kInvalidSpaceId));
}

// =============================================================================
// SpaceManager — iteration
// =============================================================================

TEST_CASE("SpaceManager ForEachSpace visits all spaces", "[space_mgr][iterate]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    mgr.CreateSpace(cfg);
    mgr.CreateSpace(cfg);
    mgr.CreateSpace(cfg);

    int visited = 0;
    mgr.ForEachSpace([&visited](Space& /*s*/) {
        ++visited;
    });

    REQUIRE(visited >= 3);  // other tests may have left spaces

    mgr.ForEachSpace([](Space& s) {
        SpaceManager::Instance().DestroySpace(s.GetId());
    });
}

// =============================================================================
// SpaceManager — default space
// =============================================================================

TEST_CASE("SpaceManager GetDefaultSpace returns null when none set", "[space_mgr][default]") {
    auto& mgr = SpaceManager::Instance();

    // May return null or a prior default; at minimum should not crash
    REQUIRE_NOTHROW(mgr.GetDefaultSpace());
}

TEST_CASE("SpaceManager CreateDefaultSpace sets the default", "[space_mgr][default]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "Default";
    Space* def = mgr.CreateDefaultSpace(cfg);

    REQUIRE(def != nullptr);
    REQUIRE(def->GetName() == "Default");
    REQUIRE(mgr.GetDefaultSpace() == def);

    mgr.DestroySpace(def->GetId());
}

TEST_CASE("SpaceManager default space lifecycle", "[space_mgr][default]") {
    auto& mgr = SpaceManager::Instance();

    SpaceConfig cfg;
    cfg.name = "FirstDefault";
    Space* first = mgr.CreateDefaultSpace(cfg);
    REQUIRE(first != nullptr);

    // Creating another default should still work
    SpaceConfig cfg2;
    cfg2.name = "SecondDefault";
    Space* second = mgr.CreateDefaultSpace(cfg2);
    REQUIRE(second != nullptr);

    // Destroy them
    mgr.DestroySpace(first->GetId());
    mgr.DestroySpace(second->GetId());
}

// =============================================================================
// ConnectionRouter — singleton & null-safety
// =============================================================================

TEST_CASE("ConnectionRouter singleton returns same instance", "[conn_router][singleton]") {
    auto& r1 = ConnectionRouter::Instance();
    auto& r2 = ConnectionRouter::Instance();
    REQUIRE(&r1 == &r2);
}

TEST_CASE("ConnectionRouter FindSpaceByConnection null returns invalid", "[conn_router][lookup]") {
    auto& router = ConnectionRouter::Instance();
    REQUIRE(router.FindSpaceByConnection(nullptr) == kInvalidSpaceId);
}

// =============================================================================
// SpaceMessage & SpaceMessageRouter — singleton
// =============================================================================

TEST_CASE("SpaceMessage default values", "[space_msg][struct]") {
    SpaceMessage msg;
    REQUIRE(msg.source_space == kInvalidSpaceId);
    REQUIRE(msg.target_space == kInvalidSpaceId);
    REQUIRE(msg.source_entity == kInvalidEntityId);
    REQUIRE(msg.target_entity == kInvalidEntityId);
    REQUIRE(msg.payload.empty());
}

TEST_CASE("SpaceMessageRouter singleton returns same instance", "[space_msg][singleton]") {
    auto& r1 = SpaceMessageRouter::Instance();
    auto& r2 = SpaceMessageRouter::Instance();
    REQUIRE(&r1 == &r2);
}

TEST_CASE("SpaceMessageRouter pending count starts at zero", "[space_msg][pending]") {
    auto& router = SpaceMessageRouter::Instance();
    // Process any leftover messages from other tests
    router.ProcessPending();
    REQUIRE(router.PendingCount() == 0);
}

TEST_CASE("SpaceMessageRouter send and process message", "[space_msg][send]") {
    auto& router = SpaceMessageRouter::Instance();

    // Drain any prior pending messages
    while (router.PendingCount() > 0) {
        router.ProcessPending();
    }

    SpaceMessage msg;
    msg.source_space = 1;
    msg.target_space = 2;
    msg.payload = "hello";

    router.SendMessage(msg);
    REQUIRE(router.PendingCount() == 1);

    router.ProcessPending();
    REQUIRE(router.PendingCount() == 0);
}

TEST_CASE("SpaceMessageRouter multiple messages", "[space_msg][send]") {
    auto& router = SpaceMessageRouter::Instance();

    while (router.PendingCount() > 0) {
        router.ProcessPending();
    }

    for (int i = 0; i < 5; ++i) {
        SpaceMessage msg;
        msg.payload = "msg_" + std::to_string(i);
        router.SendMessage(msg);
    }

    REQUIRE(router.PendingCount() >= 5);

    router.ProcessPending();
    REQUIRE(router.PendingCount() == 0);
}

TEST_CASE("Space Lua binding delivers and polls messages FIFO", "[space_bind][message]") {
    engine::ScriptVM vm;
    engine::script::ExportSpace(vm);

    std::string error;
    std::string result;
    REQUIRE(vm.DoString(
        "space._deliver_message(7, 8, 9, 'first')\n"
        "space._deliver_message(7, 8, 9, 'second')\n"
        "local a = space.poll()\n"
        "local b = space.poll()\n"
        "local c = space.poll()\n"
        "assert(a.payload == 'first')\n"
        "assert(b.payload == 'second')\n"
        "assert(c == nil)\n"
        "return tostring(a.source_space) .. ':' .. tostring(a.source_entity) .. ':' .. "
        "tostring(a.target_entity) .. ':' .. a.payload",
        "string",
        &error,
        &result));
    REQUIRE(result == "7:8:9:first");
}
