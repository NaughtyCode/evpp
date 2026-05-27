#include <benchmark/benchmark.h>
#include <runtime/evpp/buffer.h>
#include <cstring>
#include <string>

// Buffer write throughput at various sizes
static void BM_Buffer_Write(benchmark::State& state) {
    int size = static_cast<int>(state.range(0));
    std::string data(size, 'x');
    for (auto _ : state) {
        evpp::Buffer buf(size + 16);
        buf.Write(data.data(), data.size());
        benchmark::DoNotOptimize(buf.length());
    }
    state.SetBytesProcessed(
        static_cast<int64_t>(state.iterations()) * size);
}
BENCHMARK(BM_Buffer_Write)->Arg(64)->Arg(1024)->Arg(65536);

// Buffer AppendInt32 throughput
static void BM_Buffer_AppendInt32(benchmark::State& state) {
    for (auto _ : state) {
        evpp::Buffer buf(4096);
        for (int i = 0; i < 1000; ++i) {
            buf.AppendInt32(i);
        }
        benchmark::DoNotOptimize(buf.length());
    }
}
BENCHMARK(BM_Buffer_AppendInt32);

// Buffer read/write roundtrip
static void BM_Buffer_Roundtrip(benchmark::State& state) {
    int size = static_cast<int>(state.range(0));
    std::string data(size, 'x');
    for (auto _ : state) {
        evpp::Buffer buf(size + 16);
        buf.Append(data);
        std::string out = buf.ToString();
        benchmark::DoNotOptimize(out);
    }
    state.SetBytesProcessed(
        static_cast<int64_t>(state.iterations()) * size * 2);
}
BENCHMARK(BM_Buffer_Roundtrip)->Arg(64)->Arg(1024)->Arg(65536);
