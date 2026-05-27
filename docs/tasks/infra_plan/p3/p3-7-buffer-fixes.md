# P3-7: Buffer Cleanup — Remove Stale TODO + Fix int64 Endian

## Objective

Remove the stale TODO comment from `Buffer::Reserve()` (which is already functional via `grow()`) and fix the `evppbswap_64` custom byte-order macro in `AppendInt64`/`PrependInt64` by replacing it with standard `htonll`/`ntohll`.

## Current State

- `buffer.h:122`: `Reserve()` has a stale `// TODO add the implementation logic here` comment but actually calls `grow()` — functionally correct. The TODO comment should be removed; the grow strategy may benefit from a `realloc`-based optimization.
- `buffer.h:142`: Byte order for `AppendInt16`/`AppendInt32`/`PrependInt16`/`PrependInt32` already uses `htons`/`htonl`/`ntohs`/`ntohl` correctly. Only `AppendInt64`/`PrependInt64` use a custom `evppbswap_64` macro (line 143) — this should be replaced with standard `htonll`/`ntohll`.

## Implementation Steps

### Step 1: Remove Stale TODO, Optionally Optimize Reserve()

**File**: `src/runtime/evpp/buffer.h`

Remove the stale `// TODO add the implementation logic here` comment from `Reserve()` (line ~122). The method already calls `grow()` and is functionally correct. Optionally, replace the allocate-copy-free pattern in `grow()` with `realloc` for potential in-place expansion.

### Step 2: Fix int64 Byte Order

**File**: `src/runtime/evpp/buffer.h`

Replace the custom `evppbswap_64` macro in `AppendInt64`/`PrependInt64`/`PeekInt64` with standard `htonll`/`ntohll`. Note: `AppendInt16`/`AppendInt32`/`PrependInt16`/`PrependInt32` and their `Peek`/`Read` counterparts already use `htons`/`htonl`/`ntohs`/`ntohl` correctly — no changes needed for 16/32-bit.

### Step 3: Tests

**File**: `src/tests/unit/buffer_test.cc`

```cpp
TEST(BufferTest, Int64_NetworkByteOrder) {
    Buffer buf;
    buf.AppendInt64(0x1234567890ABCDEF);
    uint8_t* data = reinterpret_cast<uint8_t*>(buf.ReadableData());
    EXPECT_EQ(data[0], 0x12);
    EXPECT_EQ(data[7], 0xEF);

    int64_t value = buf.ReadInt64();
    EXPECT_EQ(value, 0x1234567890ABCDEF);
}

TEST(BufferTest, Int32_AlreadyNetworkByteOrder) {
    Buffer buf;
    buf.AppendInt32(0x12345678);
    uint8_t* data = reinterpret_cast<uint8_t*>(buf.ReadableData());
    EXPECT_EQ(data[0], 0x12);  // big-endian: MSB first
    EXPECT_EQ(data[3], 0x78);
    EXPECT_EQ(buf.ReadInt32(), 0x12345678);
}
```

## Acceptance Criteria

1. Stale TODO comment removed from `Reserve()`
2. `AppendInt64`/`PrependInt64`/`PeekInt64` use standard `htonll`/`ntohll`
3. All existing buffer tests pass
4. New tests verify int64 byte order correctness

## Dependencies: None | Estimated Effort: ~30 lines
