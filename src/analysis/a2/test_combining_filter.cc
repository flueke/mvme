#include "combining_datafilter.h"
#include <benchmark/benchmark.h>

#define ArrayCount(x) (sizeof(x) / sizeof(*x))

using namespace a2::data_filter;

static void TEST_combine(benchmark::State &state)
{
    // 4 u16 words, not reversed
    {
        const u32 data[] =
        {
            0x0101u,
            0x2020u,
            0x0303u,
            0x4040u,
        };

        auto cf = make_combining_filter(CombiningFilter::NoFlag, 4, {});

        u64 result = combine(&cf, data, ArrayCount(data));

        assert(result == 0x4040030320200101u);
    }

    // 4 u16 words, reversed
    {
        const u32 data[] =
        {
            0x0101u,
            0x2020u,
            0x0303u,
            0x4040u,
        };

        auto cf = make_combining_filter(CombiningFilter::ReverseCombine, 4, {});

        u64 result = combine(&cf, data, ArrayCount(data));

        assert(result == 0x0101202003034040u);
    }

    // 2 u32 words, not reversed
    {
        const u32 data[] =
        {
            0x01012020u,
            0x03034040u,
        };

        auto cf = make_combining_filter(CombiningFilter::WordSize32, 2, {});

        u64 result = combine(&cf, data, ArrayCount(data));

        assert(result == 0x0303404001012020u);
    }

    // 2 u32 words, reversed
    {
        const u32 data[] =
        {
            0x01012020u,
            0x03034040u,
        };

        auto cf = make_combining_filter(CombiningFilter::ReverseCombine | CombiningFilter::WordSize32, 2, {});

        u64 result = combine(&cf, data, ArrayCount(data));

        assert(result == 0x0101202003034040u);
    }
}
BENCHMARK(TEST_combine);

static void BM_combine(benchmark::State &state)
{
    // 4 u16 words, reversed
    {
        const u32 data[] =
        {
            0x0101u,
            0x2020u,
            0x0303u,
            0x4040u,
        };

        auto cf = make_combining_filter(CombiningFilter::ReverseCombine, 4, {});

        while (state.KeepRunning())
        {
            u64 result = combine(&cf, data, ArrayCount(data));
            benchmark::DoNotOptimize(result);
            assert(result == 0x0101202003034040u);
        }
    }
}
BENCHMARK(BM_combine);

static void TEST_combine_and_extract(benchmark::State &state)
{
    // 4 u16 words, not reversed
    {
        const u32 data[] =
        {
            0x0101u,
            0x2020u,
            0x0303u,
            0x4040u,
        };

        const std::vector<std::string> filters =
        {
            "DDDD DDDD DDDD DDDD DDDD DDDD", // 24 bits of the low word of the combined result
            "AAAA AAAA AAAA AAAA AAAA AAAA", // 24 bits of the high word of the combined result
        };

        auto cf = make_combining_filter(CombiningFilter::NoFlag, 4, filters);

        u32 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA);
        u32 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD);

        printf("address=0x%08x, value=0x%08x\n", address, value);

        // result of the combine step is 0x4040'0303'2020'0101u
        assert(address == 0x200101u);
        assert(value   == 0x400303u);
    }

#if 0

    // 4 u16 words, reversed
    {
        const u32 data[] =
        {
            0x0101u,
            0x2020u,
            0x0303u,
            0x4040u,
        };

        auto cf = make_combining_filter(CombiningFilter::ReverseCombine, 4, {});

        u64 result = combine(&cf, data, ArrayCount(data));

        assert(result == 0x0101202003034040u);
    }

    // 2 u32 words, not reversed
    {
        const u32 data[] =
        {
            0x01012020u,
            0x03034040u,
        };

        auto cf = make_combining_filter(CombiningFilter::WordSize32, 2, {});

        u64 result = combine(&cf, data, ArrayCount(data));

        assert(result == 0x0303404001012020u);
    }

    // 2 u32 words, reversed
    {
        const u32 data[] =
        {
            0x01012020u,
            0x03034040u,
        };

        auto cf = make_combining_filter(CombiningFilter::ReverseCombine | CombiningFilter::WordSize32, 2, {});

        u64 result = combine(&cf, data, ArrayCount(data));

        assert(result == 0x0101202003034040u);
    }
#endif
}
BENCHMARK(TEST_combine_and_extract);

BENCHMARK_MAIN();
