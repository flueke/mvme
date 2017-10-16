#include <benchmark/benchmark.h>
#include "data_filter.h"
#include "data_filter_c_style.h"
#include "analysis/multiword_datafilter.h"

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
BENCHMARK(BM_DataFilter_gather);

static void BM_DataFilter_no_gather(benchmark::State &state)
{
    DataFilter filter(makeFilterFromString("0000 XXXX XXXX XXXX XXAA AAAA DDDD DDDD"));
    u32 dataWord = 0b111101001110100101;

    while (state.KeepRunning())
    {
        u32 result = filter.extractData(dataWord, 'a');
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_DataFilter_no_gather);

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
BENCHMARK(BM_DataFilterExternalCache_gather);

static void BM_DataFilterExternalCache_no_gather(benchmark::State &state)
{
    DataFilterExternalCache filter("0000 XXXX XXXX XXXX XXAA AAAA DDDD DDDD");
    auto fcA = filter.makeCacheEntry('a');
    u32 dataWord = 0b111101001110100101;
    while (state.KeepRunning())
    {
        u32 result = filter.extractData(dataWord, fcA);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_DataFilterExternalCache_no_gather);

static void BM_DataFilterCStyle_gather(benchmark::State &state)
{
    using namespace data_filter;
    auto filter = make_filter("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD");
    auto fcA = make_cache_entry(filter, 'a');

    u32 dataWord = 0x010006fe;

    while (state.KeepRunning())
    {
        u32 result = extract(fcA, dataWord);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_DataFilterCStyle_gather);

static void BM_multiword_process_data(benchmark::State &state)
{
    using namespace data_filter;

    MultiWordFilter mf;
    add_subfilter(&mf, make_filter("AAAADDDD"));
    add_subfilter(&mf, make_filter("DDDDAAAA"));

    while (state.KeepRunning())
    {
        benchmark::DoNotOptimize(process_data(&mf, 0xab));
        benchmark::DoNotOptimize(process_data(&mf, 0xcd));
    }
}
BENCHMARK(BM_multiword_process_data);

static void BM_multiword_extract(benchmark::State &state)
{
    using namespace data_filter;

    MultiWordFilter mf;
    add_subfilter(&mf, make_filter("AAAADDDD"));
    add_subfilter(&mf, make_filter("DDDDAAAA"));

    process_data(&mf, 0xab);
    process_data(&mf, 0xcd);

    while (state.KeepRunning())
    {
        benchmark::DoNotOptimize(extract(&mf, MultiWordFilter::CacheA));
        benchmark::DoNotOptimize(extract(&mf, MultiWordFilter::CacheD));
    }
}
BENCHMARK(BM_multiword_extract);

BENCHMARK_MAIN();
