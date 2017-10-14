#include <benchmark/benchmark.h>
#include <sys/time.h>
#include "typedefs.h"

static void BM_gettimeofday(benchmark::State &state)
{
    while (state.KeepRunning())
    {
        struct timeval tv;
        gettimeofday(&tv, 0);
        u64 value = u64(tv.tv_sec) * static_cast<u64>(1000) + tv.tv_usec / 1000;
        benchmark::DoNotOptimize(value);
    }
}

BENCHMARK(BM_gettimeofday);

BENCHMARK_MAIN();
