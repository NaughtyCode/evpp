# Implementation Plan: Game Business Config Framework

## Step 1: Add SandboxLevel enum

Modify `config.h`:
```cpp
enum class SandboxLevel { Strict, Server, Full };
```
- Replace `std::string sandbox_level = "strict"` with `SandboxLevel sandbox_level = SandboxLevel::Strict`
- Add `glaze::meta` mapping for string-to-enum conversion

## Step 2: Implement Lua config API bindings

Create `src/runtime/config/config_bind.cpp`:
- `config.get(path)` → reads value from C++ ConfigManager by dotted path
- `config.get_module(name)` → loads a named config module (JSON file) into Lua table
- `config.on_change(module, callback)` → registers Lua callback for config hot-reload
- Export to Lua via `script::ExportConfigBindings()`

## Step 3: Implement ConfigTable class

Create `src/runtime/config/config_table.h/cc`:
- `ConfigTable::LoadFromJson(path)` → parses array-of-objects JSON into typed table
- `ConfigTable::LoadFromCsv(path)` → parses CSV with header row into typed table
- Type inference: detects int, float, bool, string per column
- Access: `table.Get<int>("monster_id", "hp")` or `table.GetRow("monster_id", 5)`
- Index support: create hash index on ID column for fast lookup

## Step 4: Implement reference integrity checker

Create `src/runtime/config/reference_validator.h/cc`:
- `ReferenceValidator::AddTable(name, table, id_column)` → register a table
- `ReferenceValidator::AddReference(from_table, from_column, to_table)` → define reference
- `ReferenceValidator::Validate()` → check all references, return list of broken refs
- Optional: topological sort for load order

## Step 5: Create example data tables

Create `resources/script/data/`:
- `monster.json`: id, name, hp, atk, def, exp_reward, drop_table_id
- `item.json`: id, name, type, atk_bonus, price
- `drop_table.json`: id, items[{item_id, weight, quantity_min, quantity_max}]
- `skill.json`: id, name, damage_formula, cooldown_ms, mp_cost

## Step 6: Wire into Lua VM

- Register config bindings in `script::ExportAll()` or dedicated export function
- Add example Lua script showing config usage: `config.get_module("monster")`
- Add example on_change handler for hot-reload

## Step 7: Update tests

In new `test_game_config.cpp`:
- Test: Lua `config.get("frame.target_fps")` returns correct value
- Test: Lua `config.get_module("monster")` returns Lua table with correct data
- Test: ConfigTable JSON load and type inference
- Test: ConfigTable CSV load and type inference
- Test: ConfigTable index lookup performance
- Test: Reference validator catches broken item_id reference
- Test: Reference validator catches cycle in dependencies
- Test: Lua on_change callback fires after Reload
