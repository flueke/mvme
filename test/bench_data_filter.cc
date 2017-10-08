#include <benchmark/benchmark.h>
#include "data_filter.h"

static void BM_DataFilter_gather(benchmark::State &state)
{
    DataFilter filter(makeFilterFromString("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD"));
    u32 dataWord = 0x010006fe;

    while (state.KeepRunning())
    {
        u32 result = filter.extractData(dataWord, 'a');
        benchmark::DoNotOptimize(result);
    }
}

static void BM_DataFilterExternalCache_gather(benchmark::State &state)
{
    DataFilterExternalCache filter("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD");
    auto fcA = filter.makeCacheEntry('a');
    u32 dataWord = 0x010006fe;

    while (state.KeepRunning())
    {
        u32 result = filter.extractData(dataWord, fcA);
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_DataFilter_gather);
BENCHMARK(BM_DataFilterExternalCache_gather);

BENCHMARK_MAIN();
