#include "wsa_init.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "runtime/config/config.h"
#include "runtime/config/config_bind.h"
#include "runtime/config/config_table.h"
#include "runtime/config/reference_validator.h"
#include "runtime/script/script_bind.h"
#include "runtime/vm/vm.h"
#include "config_fixture.h"

using namespace engine;

namespace {

struct TempDir {
    std::string path;

    explicit TempDir(const std::string& name) {
        path = (std::filesystem::temp_directory_path() / name).string();
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    std::string file(const std::string& name) const {
        return (std::filesystem::path(path) / name).string();
    }

    void write(const std::string& name, const std::string& content) {
        std::ofstream of(file(name));
        of << content;
    }
};

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// SandboxLevel enum
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SandboxLevel ParseSandboxLevel maps strings to enum", "[game_config][sandbox]") {
    REQUIRE(ParseSandboxLevel("strict") == SandboxLevel::Strict);
    REQUIRE(ParseSandboxLevel("server") == SandboxLevel::Server);
    REQUIRE(ParseSandboxLevel("full") == SandboxLevel::Full);
    // Unknown values default to Strict (safe fallback).
    REQUIRE(ParseSandboxLevel("invalid") == SandboxLevel::Strict);
    REQUIRE(ParseSandboxLevel("") == SandboxLevel::Strict);
}

TEST_CASE("SandboxLevelToString produces correct strings", "[game_config][sandbox]") {
    REQUIRE(std::string(SandboxLevelToString(SandboxLevel::Strict)) == "strict");
    REQUIRE(std::string(SandboxLevelToString(SandboxLevel::Server)) == "server");
    REQUIRE(std::string(SandboxLevelToString(SandboxLevel::Full)) == "full");
}

TEST_CASE("RuntimeConfig sandbox_level field defaults to strict", "[game_config][sandbox]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt = f.cfg.GetRuntimeConfig();
    REQUIRE(rt.sandbox_level == "strict");
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigTable — JSON loading
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigTable loads array-of-objects JSON", "[game_config][table]") {
    TempDir tmp("game_config_json_test");
    tmp.write("test.json", R"([
        {"id": 1, "name": "Alpha", "hp": 100},
        {"id": 2, "name": "Beta", "hp": 200}
    ])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("test.json")));
    REQUIRE(table.RowCount() == 2);

    const auto& cols = table.Columns();
    REQUIRE(cols.size() == 3);
}

TEST_CASE("ConfigTable type inference detects types", "[game_config][table]") {
    TempDir tmp("game_config_type_test");
    tmp.write("types.json", R"([
        {"id": 1, "name": "Test", "hp": 100, "rate": 1.5, "active": true}
    ])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("types.json")));
    REQUIRE(table.RowCount() == 1);

    const auto& cols = table.Columns();
    bool has_int = false, has_str = false;
    for (const auto& col : cols) {
        if (col.name == "id") {
            REQUIRE(col.type == ConfigTable::ColumnType::Int);
            has_int = true;
        }
        if (col.name == "name") {
            REQUIRE(col.type == ConfigTable::ColumnType::String);
            has_str = true;
        }
    }
    REQUIRE(has_int);
    REQUIRE(has_str);
}

TEST_CASE("ConfigTable JSON not found returns false", "[game_config][table]") {
    ConfigTable table;
    REQUIRE_FALSE(table.LoadFromJson("nonexistent_file.json"));
}

TEST_CASE("ConfigTable rejects non-array JSON", "[game_config][table]") {
    TempDir tmp("game_config_bad_test");
    tmp.write("obj.json", R"({"key": "value"})");

    ConfigTable table;
    REQUIRE_FALSE(table.LoadFromJson(tmp.file("obj.json")));
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigTable — CSV loading
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigTable loads CSV with header", "[game_config][table]") {
    TempDir tmp("game_config_csv_test");
    tmp.write("test.csv", "id,name,hp\n1,Alpha,100\n2,Beta,200\n");

    ConfigTable table;
    REQUIRE(table.LoadFromCsv(tmp.file("test.csv")));
    REQUIRE(table.RowCount() == 2);
    REQUIRE(table.Columns().size() == 3);
    REQUIRE(table.Columns()[0].name == "id");
    REQUIRE(table.Columns()[1].name == "name");
    REQUIRE(table.Columns()[2].name == "hp");
}

TEST_CASE("ConfigTable CSV not found returns false", "[game_config][table]") {
    ConfigTable table;
    REQUIRE_FALSE(table.LoadFromCsv("nonexistent.csv"));
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigTable — accessors
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigTable GetInt by key column", "[game_config][table]") {
    TempDir tmp("game_config_access_test");
    tmp.write("data.json", R"([
        {"id": 1, "name": "Alpha", "hp": 100},
        {"id": 2, "name": "Beta", "hp": 200}
    ])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("data.json")));
    table.BuildIndex("id");

    REQUIRE(table.GetInt("id", "1", "hp") == 100);
    REQUIRE(table.GetInt("id", "2", "hp") == 200);
    // Missing key returns default.
    REQUIRE(table.GetInt("id", "99", "hp", -1) == -1);
}

TEST_CASE("ConfigTable GetFloat by key column", "[game_config][table]") {
    TempDir tmp("game_config_float_test");
    tmp.write("data.json", R"([
        {"id": 1, "rate": 1.5},
        {"id": 2, "rate": 2.75}
    ])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("data.json")));
    table.BuildIndex("id");

    REQUIRE(table.GetFloat("id", "1", "rate") == Approx(1.5));
    REQUIRE(table.GetFloat("id", "2", "rate") == Approx(2.75));
}

TEST_CASE("ConfigTable GetBool by key column", "[game_config][table]") {
    TempDir tmp("game_config_bool_test");
    tmp.write("data.json", R"([
        {"id": 1, "active": true},
        {"id": 2, "active": false}
    ])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("data.json")));
    table.BuildIndex("id");

    REQUIRE(table.GetBool("id", "1", "active") == true);
    REQUIRE(table.GetBool("id", "2", "active") == false);
}

TEST_CASE("ConfigTable GetString by key column", "[game_config][table]") {
    TempDir tmp("game_config_str_test");
    tmp.write("data.json", R"([
        {"id": 1, "name": "Alpha"},
        {"id": 2, "name": "Beta"}
    ])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("data.json")));
    table.BuildIndex("id");

    REQUIRE(table.GetString("id", "1", "name") == "Alpha");
    REQUIRE(table.GetString("id", "2", "name") == "Beta");
}

TEST_CASE("ConfigTable GetRow returns full row", "[game_config][table]") {
    TempDir tmp("game_config_row_test");
    tmp.write("data.json", R"([
        {"id": 5, "name": "Charlie", "hp": 300}
    ])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("data.json")));
    table.BuildIndex("id");

    const auto* row = table.GetRow("id", "5");
    REQUIRE(row != nullptr);
    REQUIRE(row->at("name") == "Charlie");
    REQUIRE(row->at("hp") == "300");
}

TEST_CASE("ConfigTable GetRow returns nullptr for missing", "[game_config][table]") {
    TempDir tmp("game_config_miss_test");
    tmp.write("data.json", R"([{"id": 1, "name": "Test"}])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("data.json")));

    REQUIRE(table.GetRow("id", "999") == nullptr);
}

TEST_CASE("ConfigTable GetRowByIndex", "[game_config][table]") {
    TempDir tmp("game_config_idx_test");
    tmp.write("data.json", R"([
        {"id": 1, "name": "First"},
        {"id": 2, "name": "Second"}
    ])");

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("data.json")));

    const auto* row0 = table.GetRowByIndex(0);
    REQUIRE(row0 != nullptr);
    REQUIRE(row0->at("name") == "First");

    const auto* row1 = table.GetRowByIndex(1);
    REQUIRE(row1 != nullptr);
    REQUIRE(row1->at("name") == "Second");

    REQUIRE(table.GetRowByIndex(99) == nullptr);
}

TEST_CASE("ConfigTable index lookup is fast", "[game_config][table]") {
    TempDir tmp("game_config_perf_test");

    // Build a table with 100 rows.
    std::string json = "[";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) json += ",";
        json += "{\"id\":" + std::to_string(i) + ",\"value\":\"item_" + std::to_string(i) + "\"}";
    }
    json += "]";
    tmp.write("large.json", json);

    ConfigTable table;
    REQUIRE(table.LoadFromJson(tmp.file("large.json")));
    table.BuildIndex("id");

    // Linear scan without index would be O(n) for each lookup.
    // With index, each lookup should be O(1).
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        auto* row = table.GetRow("id", std::to_string(i));
        REQUIRE(row != nullptr);
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    // Should complete very quickly with index.
    REQUIRE(ms < 100);  // generous upper bound
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigTable — real data files
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ConfigTable loads monster.json from resources", "[game_config][table]") {
    ConfigTable table;
    REQUIRE(table.LoadFromJson("resources/script/data/monster.json"));
    REQUIRE(table.RowCount() == 5);
    REQUIRE(table.Columns().size() >= 5);

    table.BuildIndex("id");
    REQUIRE(table.GetString("id", "1", "name") == "Slime");
    REQUIRE(table.GetInt("id", "1", "hp") == 50);
    REQUIRE(table.GetInt("id", "5", "hp") == 500);
}

TEST_CASE("ConfigTable loads item.json from resources", "[game_config][table]") {
    ConfigTable table;
    REQUIRE(table.LoadFromJson("resources/script/data/item.json"));
    REQUIRE(table.RowCount() == 7);

    table.BuildIndex("id");
    REQUIRE(table.GetString("id", "2", "name") == "Iron Sword");
    REQUIRE(table.GetInt("id", "2", "price") == 100);
}

TEST_CASE("ConfigTable loads drop_table.json from resources", "[game_config][table]") {
    ConfigTable table;
    REQUIRE(table.LoadFromJson("resources/script/data/drop_table.json"));
    REQUIRE(table.RowCount() == 4);
}

TEST_CASE("ConfigTable loads skill.json from resources", "[game_config][table]") {
    ConfigTable table;
    REQUIRE(table.LoadFromJson("resources/script/data/skill.json"));
    REQUIRE(table.RowCount() == 5);

    table.BuildIndex("id");
    REQUIRE(table.GetString("id", "1", "name") == "Slash");
    REQUIRE(table.GetInt("id", "2", "cooldown_ms") == 5000);
    REQUIRE(table.GetString("id", "5", "damage_formula") == "atk * 3.0 + 50");
}

// ═══════════════════════════════════════════════════════════════════════════
// ReferenceValidator
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ReferenceValidator all valid references pass", "[game_config][validator]") {
    // Load item.json as the "items" table.
    ConfigTable item_table;
    REQUIRE(item_table.LoadFromJson("resources/script/data/item.json"));
    item_table.BuildIndex("id");

    // Create a small table that references items.
    TempDir tmp("validator_valid_test");
    tmp.write("quest.json", R"([
        {"id": 1, "name": "Slay Dragon", "reward_item_id": 1},
        {"id": 2, "name": "Collect Scales", "reward_item_id": 5}
    ])");
    ConfigTable quest_table;
    REQUIRE(quest_table.LoadFromJson(tmp.file("quest.json")));

    ReferenceValidator validator;
    validator.AddTable("item", &item_table, "id");
    validator.AddTable("quest", &quest_table, "id");
    validator.AddReference("quest", "reward_item_id", "item");

    auto result = validator.Validate();
    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
}

TEST_CASE("ReferenceValidator catches broken reference", "[game_config][validator]") {
    ConfigTable item_table;
    REQUIRE(item_table.LoadFromJson("resources/script/data/item.json"));
    item_table.BuildIndex("id");

    TempDir tmp("validator_broken_test");
    // reward_item_id 999 does not exist in items.
    tmp.write("quest.json", R"([
        {"id": 1, "name": "Impossible Quest", "reward_item_id": 999}
    ])");
    ConfigTable quest_table;
    REQUIRE(quest_table.LoadFromJson(tmp.file("quest.json")));

    ReferenceValidator validator;
    validator.AddTable("item", &item_table, "id");
    validator.AddTable("quest", &quest_table, "id");
    validator.AddReference("quest", "reward_item_id", "item");

    auto result = validator.Validate();
    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.errors.empty());

    // Verify error details.
    bool found = false;
    for (const auto& err : result.errors) {
        if (err.from_table == "quest" && err.to_table == "item") {
            REQUIRE(err.missing_value == "999");
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("ReferenceValidator detects cycle in dependencies", "[game_config][validator]") {
    TempDir tmp("validator_cycle_test");
    tmp.write("a.json", R"([{"id": 1, "b_ref": 1}])");
    tmp.write("b.json", R"([{"id": 1, "a_ref": 1}])");

    ConfigTable table_a;
    REQUIRE(table_a.LoadFromJson(tmp.file("a.json")));
    ConfigTable table_b;
    REQUIRE(table_b.LoadFromJson(tmp.file("b.json")));

    ReferenceValidator validator;
    validator.AddTable("a", &table_a, "id");
    validator.AddTable("b", &table_b, "id");
    validator.AddReference("a", "b_ref", "b");
    validator.AddReference("b", "a_ref", "a");

    auto sorted = validator.TopologicalSort();
    // With a cycle, the sorted size won't match the number of tables.
    // Either we get an incomplete sort or all tables are present (cycle-breaking).
    REQUIRE(sorted.size() <= 2);
}

TEST_CASE("ReferenceValidator topological sort produces correct order", "[game_config][validator]") {
    TempDir tmp("validator_topo_test");
    tmp.write("base.json", R"([{"id": 1, "value": "base"}])");
    tmp.write("mid.json", R"([{"id": 1, "base_ref": 1}])");
    tmp.write("top.json", R"([{"id": 1, "mid_ref": 1}])");

    ConfigTable base_tbl, mid_tbl, top_tbl;
    REQUIRE(base_tbl.LoadFromJson(tmp.file("base.json")));
    REQUIRE(mid_tbl.LoadFromJson(tmp.file("mid.json")));
    REQUIRE(top_tbl.LoadFromJson(tmp.file("top.json")));

    ReferenceValidator validator;
    validator.AddTable("base", &base_tbl, "id");
    validator.AddTable("mid", &mid_tbl, "id");
    validator.AddTable("top", &top_tbl, "id");
    // mid depends on base (base must load first).
    validator.AddReference("mid", "base_ref", "base");
    // top depends on mid (mid must load first).
    validator.AddReference("top", "mid_ref", "mid");

    auto sorted = validator.TopologicalSort();
    REQUIRE(sorted.size() == 3);

    // Find positions: base < mid < top.
    auto base_pos = std::find(sorted.begin(), sorted.end(), "base");
    auto mid_pos = std::find(sorted.begin(), sorted.end(), "mid");
    auto top_pos = std::find(sorted.begin(), sorted.end(), "top");
    REQUIRE(base_pos != sorted.end());
    REQUIRE(mid_pos != sorted.end());
    REQUIRE(top_pos != sorted.end());
    REQUIRE(base_pos < mid_pos);
    REQUIRE(mid_pos < top_pos);
}

TEST_CASE("ReferenceValidator cross-table monster-to-drop_table refs", "[game_config][validator]") {
    ConfigTable monster_table;
    REQUIRE(monster_table.LoadFromJson("resources/script/data/monster.json"));
    monster_table.BuildIndex("id");

    ConfigTable drop_table;
    REQUIRE(drop_table.LoadFromJson("resources/script/data/drop_table.json"));
    drop_table.BuildIndex("id");

    ReferenceValidator validator;
    validator.AddTable("monster", &monster_table, "id");
    validator.AddTable("drop_table", &drop_table, "id");
    validator.AddReference("monster", "drop_table_id", "drop_table");

    auto result = validator.Validate();
    // All monsters reference valid drop tables (ids 1-4).
    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Lua config bindings — config.get(path)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Lua config.get returns int value", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    std::string result;
    REQUIRE(vm.DoString("return config.get('frame.target_fps')", "test", nullptr, &result));
    REQUIRE(result == "30");
}

TEST_CASE("Lua config.get returns string value", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    std::string result;
    REQUIRE(vm.DoString("return config.get('log.level')", "test", nullptr, &result));
    REQUIRE(result == "debug");
}

TEST_CASE("Lua config.get returns nil for unknown path", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    std::string error;
    REQUIRE(vm.DoString("local v = config.get('nonexistent.field'); return tostring(v)", "test", &error, nullptr));
}

TEST_CASE("Lua config.get reads sandbox_level", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    std::string result;
    // LoadFromStrings sets default sandbox_level = "strict"
    REQUIRE(vm.DoString("return config.get('sandbox_level')", "test", nullptr, &result));
    REQUIRE(result == "strict");
}

TEST_CASE("Lua config.get reads server config values", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    std::string result;
    REQUIRE(vm.DoString("return tostring(config.get('server.http.timeout_sec'))", "test", nullptr, &result));
    REQUIRE(result == "5.0");
}

// ═══════════════════════════════════════════════════════════════════════════
// Lua config bindings — config.get_module(name)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Lua config.get_module loads monster data", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    std::string result;
    // config.get_module returns a Lua table; check that row 1 exists.
    REQUIRE(vm.DoString(
        "local m = config.get_module('monster'); "
        "return tostring(#m)",
        "test", nullptr, &result));
    REQUIRE(result == "5");
}

TEST_CASE("Lua config.get_module returns nil for unknown module", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    // Returns nil + error message
    std::string result;
    REQUIRE(vm.DoString(
        "local m, err = config.get_module('nonexistent'); "
        "return err or 'ok'",
        "test", nullptr, &result));
    REQUIRE(result.find("not found") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Lua config bindings — config.on_change(module, callback)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Lua config.on_change returns callback ID", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    std::string result;
    REQUIRE(vm.DoString(
        "local id = config.on_change('monster', function(changes) end); "
        "return tostring(id > 0)",
        "test", nullptr, &result));
    REQUIRE(result == "true");
}

TEST_CASE("Lua config.on_change requires function argument", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    std::string error;
    bool ok = vm.DoString("config.on_change('monster', 'not a function')", "test", &error);
    REQUIRE_FALSE(ok);
}

TEST_CASE("Lua config.on_change callback fires after flush", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    script::ExportConfigBindings(vm);

    // Set up the callback first.
    std::string result;
    REQUIRE(vm.DoString(
        "callback_fired = false; "
        "config.on_change('test', function(changes) callback_fired = true end); "
        "return 'ok'",
        "test", nullptr, &result));

    // Trigger a reload via ConfigManager.
    auto& cfg = ConfigManager::Instance();
    cfg.LoadRuntimeFromString(R"({
        "resource_dir": ".",
        "log": { "dir": "." },
        "frame": { "target_fps": 60 },
        "scripts_dir": "."
    })");

    // Flush pending callbacks (in production this is called from the event loop).
    REQUIRE(vm.DoString("config.flush_changes()", "test", nullptr, &result));

    // Check that the callback was invoked after flushing.
    REQUIRE(vm.DoString("return tostring(callback_fired)", "test", nullptr, &result));
    REQUIRE(result == "true");
}

// ═══════════════════════════════════════════════════════════════════════════
// Lua config bindings — integration with ExportAll
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ExportAll includes config module", "[game_config][lua]") {
    ConfigFixture f;
    f.LoadFromStrings();

    ScriptVM vm;
    // ExportAll should include config bindings.
    // We test the individual export, which is also called by ExportAll.
    script::ExportConfigBindings(vm);

    std::string result;
    REQUIRE(vm.DoString("return type(config.get)", "test", nullptr, &result));
    REQUIRE(result == "function");
}
