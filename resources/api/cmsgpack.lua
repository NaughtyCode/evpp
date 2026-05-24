--- MessagePack API
--- Global modules: cmsgpack, cmsgpack_safe
---
--- cmsgpack_safe is a safe wrapper around cmsgpack: each function calls via pcall internally,
--- returning nil, errmsg on error instead of throwing a Lua exception. Function signatures are identical.
---
--- Encoding type mapping:
---   nil        → msgpack nil
---   boolean    → msgpack true/false
---   integer    → msgpack int (uint64 degrades to number when out of int64 range)
---   number     → msgpack float32/64
---   string     → msgpack str
---   table(array) → msgpack array
---   table(map) → msgpack map
---   non-encodable types (function, thread, userdata) → msgpack nil
---
--- Maximum nesting depth: 16.

-- ============================================================================
-- cmsgpack
-- ============================================================================

--- Encode arbitrary Lua values to a MessagePack binary string. Multiple arguments are encoded individually and concatenated.
---@param ... any  one or more values to encode
---@return string packed  concatenated MessagePack binary data
function cmsgpack.pack(...) end

--- Decode all values from MessagePack binary data (multiple return values).
---@param packed string  MessagePack binary data
---@return ...  all decoded values (multiple return values, type any)
function cmsgpack.unpack(packed) end

--- Decode a single value from the given offset, returning the value and the next byte offset.
---@param packed  string   MessagePack binary data
---@param offset? integer  starting byte offset (default 0)
---@return any    value        decoded value
---@return integer next_offset  offset of the next item, -1 means end reached
function cmsgpack.unpack_one(packed, offset) end

--- Decode up to limit values from the given offset, followed by a next_offset.
---@param packed  string   MessagePack binary data
---@param limit   integer  max number of values to decode
---@param offset? integer  starting byte offset (default 0)
---@return ...             up to limit decoded values (type any)
---@return integer next_offset  offset of the next item, -1 means end reached
function cmsgpack.unpack_limit(packed, limit, offset) end

-- ============================================================================
-- cmsgpack_safe -- safe wrapper (returns nil, errmsg on error)
-- ============================================================================

---@param ... any  one or more values to encode
---@return string packed  success: MessagePack binary data
---@return nil, string errmsg  failure: error message
function cmsgpack_safe.pack(...) end

---@param packed string  MessagePack binary data
---@return ...  success: all decoded values
---@return nil, string errmsg  failure: error message
function cmsgpack_safe.unpack(packed) end

---@param packed  string   MessagePack binary data
---@param offset? integer  starting byte offset (default 0)
---@return any    value, integer next_offset  success: decoded value + next offset
---@return nil, string errmsg                  failure: error message
function cmsgpack_safe.unpack_one(packed, offset) end

---@param packed  string   MessagePack binary data
---@param limit   integer  max number of values to decode
---@param offset? integer  starting byte offset (default 0)
---@return ...  success: up to limit decoded values + next offset (integer)
---@return nil, string errmsg  failure: error message
function cmsgpack_safe.unpack_limit(packed, limit, offset) end
