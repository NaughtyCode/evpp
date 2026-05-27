-- test_msgpack.lua (integration test wrapper)
-- Delegates to the existing comprehensive msgpack test in resources/.
-- This file enables CTest discovery while keeping the test logic in resources/.

dofile("resources/script/tests/msgpack_test.lua")
