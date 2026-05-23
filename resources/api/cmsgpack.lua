--- MessagePack API
--- 全局模块: cmsgpack, cmsgpack_safe
---
--- cmsgpack_safe 是 cmsgpack 的安全包装版：每个函数内部通过 pcall 调用，
--- 出错时返回 nil, errmsg 而非抛出 Lua 异常。函数签名完全相同。
---
--- 编码类型映射:
---   nil        → msgpack nil
---   boolean    → msgpack true/false
---   integer    → msgpack int (uint64 超出 int64 范围时降级为 number)
---   number     → msgpack float32/64
---   string     → msgpack str
---   table(数组) → msgpack array
---   table(映射) → msgpack map
---   不可编码类型 (function, thread, userdata) → msgpack nil
---
--- 最大嵌套深度: 16。

-- ============================================================================
-- cmsgpack
-- ============================================================================

--- 将任意 Lua 值编码为 MessagePack 二进制字符串。多个参数会被分别编码再拼接。
---@param ... any  一个或多个待编码的值
---@return string packed  拼接后的 MessagePack 二进制数据
function cmsgpack.pack(...) end

--- 从 MessagePack 二进制数据中解码全部值（多返回值）。
---@param packed string  MessagePack 二进制数据
---@return ...  全部解码出的值 (多返回值，类型为 any)
function cmsgpack.unpack(packed) end

--- 从指定偏移开始解码一个值，返回该值及下一字节偏移。
---@param packed  string   MessagePack 二进制数据
---@param offset? integer  起始字节偏移 (默认 0)
---@return any    value        解码出的值
---@return integer next_offset  下一条数据的起始偏移，-1 表示已读完
function cmsgpack.unpack_one(packed, offset) end

--- 从指定偏移开始解码最多 limit 个值，最后跟随一个 next_offset。
---@param packed  string   MessagePack 二进制数据
---@param limit   integer  最多解码个数
---@param offset? integer  起始字节偏移 (默认 0)
---@return ...             limit 个解码出的值 (类型为 any)
---@return integer next_offset  下一条数据的起始偏移，-1 表示已读完
function cmsgpack.unpack_limit(packed, limit, offset) end

-- ============================================================================
-- cmsgpack_safe — 安全包装版 (出错时返回 nil, errmsg)
-- ============================================================================

---@param ... any  一个或多个待编码的值
---@return string packed  成功: MessagePack 二进制数据
---@return nil, string errmsg  失败: 错误信息
function cmsgpack_safe.pack(...) end

---@param packed string  MessagePack 二进制数据
---@return ...  成功: 全部解码出的值
---@return nil, string errmsg  失败: 错误信息
function cmsgpack_safe.unpack(packed) end

---@param packed  string   MessagePack 二进制数据
---@param offset? integer  起始字节偏移 (默认 0)
---@return any    value, integer next_offset  成功: 解码值 + 下一偏移
---@return nil, string errmsg                  失败: 错误信息
function cmsgpack_safe.unpack_one(packed, offset) end

---@param packed  string   MessagePack 二进制数据
---@param limit   integer  最多解码个数
---@param offset? integer  起始字节偏移 (默认 0)
---@return ...  成功: limit 个解码值 + 下一偏移 (integer)
---@return nil, string errmsg  失败: 错误信息
function cmsgpack_safe.unpack_limit(packed, limit, offset) end
