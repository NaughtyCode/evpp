# Verification Checklist: Game Business Config Framework

## Unit Tests

- [ ] `SandboxLevel` enum: "strict" → Strict, "server" → Server, "full" → Full
- [ ] `SandboxLevel` enum: "invalid" → parse error
- [ ] Lua `config.get("frame.target_fps")` returns int value
- [ ] Lua `config.get("log.level")` returns string value
- [ ] Lua `config.get_module("monster")` returns table with all rows
- [ ] Lua `config.get_module("monster")` table indexed by id column
- [ ] ConfigTable JSON: array of objects parsed correctly
- [ ] ConfigTable CSV: header row + data rows parsed correctly
- [ ] ConfigTable type inference: int, float, bool, string
- [ ] ConfigTable index: `GetRow("id", 5)` returns correct row
- [ ] ReferenceValidator: all valid → empty error list
- [ ] ReferenceValidator: broken item_id → error with source and target
- [ ] ReferenceValidator: cycle in dependencies → detected
- [ ] Lua `config.on_change("monster", callback)` fires after Reload
- [ ] Lua `config.on_change` callback receives old and new values

## Integration Tests

- [ ] Load monster.json via Lua → query by ID → verify HP value
- [ ] Validate drop_table references → broken item_id → error logged
- [ ] Reload item.json → Lua on_change callback triggered
- [ ] Server starts with example data tables → accessible from Lua

## Manual Verification

- [ ] `require("config").get("frame.target_fps")` in Lua console returns 30
- [ ] `require("config").get_module("item")` returns all items as Lua table
