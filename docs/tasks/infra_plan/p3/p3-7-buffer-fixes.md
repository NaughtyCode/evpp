# P3-7: Buffer::Reserve Implementation + Endian Fix

## Objective

Implement the empty `Buffer::Reserve()` method and resolve the byte order issue in `Buffer`'s integer append/prepend methods.

## Current State

- `buffer.h:121`: `Reserve()` marked "TODO add the implementation logic here" — **empty method body**. If code calls `Reserve()` expecting pre-allocated space, it will write past the buffer boundary.
- `buffer.h:141`: Marked "TODO XXX Little-Endian/Big-Endian problem" — `AppendInt16`/`AppendInt32`/`PrependInt16`/`PrependInt32` may have incorrect byte order depending on platform.

## Implementation Steps

### Step 1: Implement Reserve()

**File**: `src/runtime/evpp/buffer.h`

```cpp
void Reserve(size_t additional_bytes) {
    size_t required = write_index_ + additional_bytes;
    if (required > capacity_) {
        /* Grow buffer: double until enough capacity */
        size_t new_capacity = capacity_ ? capacity_ : 64;
        while (new_capacity < required) {
            new_capacity *= 2;
        }
        char* new_buf = new char[new_capacity];
        size_t readable = ReadableBytes();
        if (readable > 0) {
            memcpy(new_buf, buf_ + read_index_, readable);
        }
        write_index_ = readable;
        read_index_ = 0;
        delete[] buf_;
        buf_ = new_buf;
        capacity_ = new_capacity;
    }
}
```

### Step 2: Fix Byte Order

**File**: `src/runtime/evpp/buffer.h`

Ensure all integer append/prepend methods consistently use network byte order (big-endian):

```cpp
void AppendInt32(int32_t value) {
    int32_t net_value = htonl(value);  /* host to network (big-endian) */
    Append(reinterpret_cast<const char*>(&net_value), sizeof(net_value));
}

int32_t ReadInt32() {
    int32_t net_value;
    memcpy(&net_value, ReadableData(), sizeof(net_value));
    Skip(sizeof(net_value));
    return ntohl(net_value);  /* network to host */
}
```

Same pattern for `AppendInt16`/`ReadInt16` using `htons`/`ntohs`.

### Step 3: Update All Callers

Audit all code that calls `AppendInt16`/`AppendInt32`/`PrependInt16`/`PrependInt32` to ensure they're compatible with the network byte order convention. Update any callers that assumed host byte order.

### Step 4: Tests

**File**: `src/tests/unit/buffer_test.cc`

```cpp
TEST(BufferTest, Reserve_GrowsBuffer) {
    Buffer buf;
    buf.Reserve(1024);
    EXPECT_GE(buf.Capacity(), 1024);
}

TEST(BufferTest, Reserve_PreservesData) {
    Buffer buf;
    buf.Append("hello");
    buf.Reserve(1024);
    EXPECT_EQ(buf.NextAllString(), "hello");
}

TEST(BufferTest, Int32_NetworkByteOrder) {
    Buffer buf;
    buf.AppendInt32(0x12345678);
    /* Verify wire format is big-endian */
    uint8_t* data = reinterpret_cast<uint8_t*>(buf.ReadableData());
    EXPECT_EQ(data[0], 0x12);
    EXPECT_EQ(data[1], 0x34);
    EXPECT_EQ(data[2], 0x56);
    EXPECT_EQ(data[3], 0x78);

    int32_t value = buf.ReadInt32();
    EXPECT_EQ(value, 0x12345678);
}

TEST(BufferTest, PrependInt32_PreservesExistingData) {
    Buffer buf;
    buf.Append("world");
    buf.PrependInt32(42);
    /* Buffer should be: [42][world] */
    EXPECT_EQ(buf.ReadInt32(), 42);
    EXPECT_EQ(buf.NextAllString(), "world");
}
```

## Acceptance Criteria

1. `Reserve()` correctly grows buffer capacity
2. `Reserve()` preserves existing readable data
3. All integer append/prepend/read methods use consistent network byte order
4. Callers are audited for byte order compatibility
5. All existing buffer tests pass
6. New tests verify Reserve and byte order correctness

## Dependencies: None (but P0-2 depends on these fixes) | Estimated Effort: ~100 lines
