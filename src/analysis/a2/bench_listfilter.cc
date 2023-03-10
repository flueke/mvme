/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "listfilter.h"
#include <benchmark/benchmark.h>

#define ArrayCount(x) (sizeof(x) / sizeof(*x))

using namespace a2::data_filter;

static void TEST_combine(benchmark::State &s)
{
    while (s.KeepRunning())
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

            auto cf = make_listfilter(ListFilter::NoFlag, 4, {});

            [[maybe_unused]] u64 result = combine(&cf, data, ArrayCount(data));

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

            auto cf = make_listfilter(ListFilter::ReverseCombine, 4, {});

            [[maybe_unused]] u64 result = combine(&cf, data, ArrayCount(data));

            assert(result == 0x0101202003034040u);
        }

        // 2 u32 words, not reversed
        {
            const u32 data[] =
            {
                0x01012020u,
                0x03034040u,
            };

            auto cf = make_listfilter(ListFilter::WordSize32, 2, {});

            [[maybe_unused]] u64 result = combine(&cf, data, ArrayCount(data));

            assert(result == 0x0303404001012020u);
        }

        // 2 u32 words, reversed
        {
            const u32 data[] =
            {
                0x01012020u,
                0x03034040u,
            };

            auto cf = make_listfilter(ListFilter::ReverseCombine | ListFilter::WordSize32, 2, {});

            [[maybe_unused]] u64 result = combine(&cf, data, ArrayCount(data));

            assert(result == 0x0101202003034040u);
        }
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

        auto cf = make_listfilter(ListFilter::ReverseCombine, 4, {});

        while (state.KeepRunning())
        {
            u64 result = combine(&cf, data, ArrayCount(data));
            benchmark::DoNotOptimize(result);
            assert(result == 0x0101202003034040u);
        }
    }
}
BENCHMARK(BM_combine);

static void TEST_combine_and_extract_max_words(benchmark::State &s)
{
    while (s.KeepRunning())
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

            auto cf = make_listfilter(ListFilter::NoFlag, 4, filters);

            [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));

            assert(combined == 0x4040030320200101u);

            [[maybe_unused]] u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            [[maybe_unused]] u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;

            //printf("address=0x%08x, value=0x%08x\n", address, value);

            assert(value   == 0x200101u);
            assert(address == 0x400303u);
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

            const std::vector<std::string> filters =
            {
                "DDDD DDDD DDDD DDDD DDDD DDDD", // 24 bits of the low word of the combined result
                "AAAA AAAA AAAA AAAA AAAA AAAA", // 24 bits of the high word of the combined result
            };

            auto cf = make_listfilter(ListFilter::ReverseCombine, 4, filters);

            [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));
            assert(combined == 0x0101202003034040u);

            [[maybe_unused]] u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            [[maybe_unused]] u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;

            //printf("address=0x%08x, value=0x%08x\n", address, value);

            assert(value   == 0x034040u);
            assert(address == 0x012020u);
        }

        // 2 u32 words, not reversed
        {
            const u32 data[] =
            {
                0x01012020u,
                0x03034040u,
            };

            const std::vector<std::string> filters =
            {
                "DDDD DDDD DDDD DDDD DDDD DDDD", // 24 bits of the low word of the combined result
                "AAAA AAAA AAAA AAAA AAAA AAAA", // 24 bits of the high word of the combined result
            };

            auto cf = make_listfilter(ListFilter::WordSize32, 2, filters);

            [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));

            assert(combined == 0x0303404001012020u);

            [[maybe_unused]] u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            [[maybe_unused]] u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;

            //printf("address=0x%08x, value=0x%08x\n", address, value);

            assert(value   == 0x012020u);
            assert(address == 0x034040u);
        }

        // 2 u32 words, reversed
        {
            const u32 data[] =
            {
                0x01012020u,
                0x03034040u,
            };

            const std::vector<std::string> filters =
            {
                "DDDD DDDD DDDD DDDD DDDD DDDD", // 24 bits of the low word of the combined result
                "AAAA AAAA AAAA AAAA AAAA AAAA", // 24 bits of the high word of the combined result
            };

            auto cf = make_listfilter(ListFilter::WordSize32 | ListFilter::ReverseCombine, 2, filters);

            [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));

            assert(combined == 0x0101202003034040u);

            [[maybe_unused]] u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            [[maybe_unused]] u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;

            //printf("address=0x%08x, value=0x%08x\n", address, value);

            assert(value   == 0x034040u);
            assert(address == 0x012020u);
        }
    }
}
BENCHMARK(TEST_combine_and_extract_max_words);

static void TEST_combine_and_extract_non_max_words(benchmark::State &s)
{
    while (s.KeepRunning())
    {
        // 3 u16 words, not reversed
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

            auto cf = make_listfilter(ListFilter::NoFlag, 3, filters);

            [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));

            assert(combined == 0x030320200101u);

            [[maybe_unused]] u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            [[maybe_unused]] u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;

            //printf("address=0x%08x, value=0x%08x\n", address, value);

            assert(value   == 0x200101u);
            assert(address == 0x0303u);
        }

        // 3 u16 words, reversed
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
                "DDDD DDDD DDDD DDDD DDDD DDDD",
                "AAAA AAAA AAAA AAAA AAAA AAAA",
            };

            auto cf = make_listfilter(ListFilter::ReverseCombine, 3, filters);

            [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));
            assert(combined == 0x010120200303u);

            [[maybe_unused]] u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            [[maybe_unused]] u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;

            //printf("address=0x%08x, value=0x%08x\n", address, value);

            assert(value   == 0x200303u);
            assert(address == 0x0101u);
        }

        // 1 u32 words, not reversed
        {
            const u32 data[] =
            {
                0x01012020u,
                0x03034040u,
            };

            const std::vector<std::string> filters =
            {
                "DDDD DDDD DDDD DDDD DDDD DDDD", // 24 bits of the low word of the combined result
                "AAAA AAAA AAAA AAAA AAAA AAAA", // 24 bits of the high word of the combined result
            };

            auto cf = make_listfilter(ListFilter::WordSize32, 1, filters);

            [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));

            assert(combined == 0x01012020u);

            [[maybe_unused]] u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            [[maybe_unused]] u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;

            //printf("address=0x%08x, value=0x%08x\n", address, value);

            assert(value   == 0x012020u);
            assert(address == 0x0u);
        }

        // 1 u32 words, reversed
        {
            const u32 data[] =
            {
                0x01012020u,
                0x03034040u,
            };

            const std::vector<std::string> filters =
            {
                "DDDD DDDD DDDD DDDD DDDD DDDD", // 24 bits of the low word of the combined result
                "AAAA AAAA AAAA AAAA AAAA AAAA", // 24 bits of the high word of the combined result
            };

            auto cf = make_listfilter(ListFilter::WordSize32 | ListFilter::ReverseCombine, 1, filters);

            [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));

            assert(combined == 0x01012020u);

            [[maybe_unused]] u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            [[maybe_unused]] u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;

            //printf("address=0x%08x, value=0x%08x\n", address, value);

            assert(value   == 0x012020u);
            assert(address == 0x0u);
        }
    }
}
BENCHMARK(TEST_combine_and_extract_non_max_words);

static void BM_combine_and_extract(benchmark::State &state)
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

        const std::vector<std::string> filters =
        {
            "DDDD DDDD DDDD DDDD DDDD DDDD", // 24 bits of the low word of the combined result
            "AAAA AAAA AAAA AAAA AAAA AAAA", // 24 bits of the high word of the combined result
        };

        auto cf = make_listfilter(ListFilter::ReverseCombine, 4, filters);

        [[maybe_unused]] u64 combined = combine(&cf, data, ArrayCount(data));
        assert(combined == 0x0101202003034040u);

        while (state.KeepRunning())
        {
            u64 address = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheA).first;
            u64 value   = combine_and_extract(&cf, data, ArrayCount(data), MultiWordFilter::CacheD).first;
            benchmark::DoNotOptimize(address);
            benchmark::DoNotOptimize(value);
            assert(address == 0x012020u);
            assert(value   == 0x034040u);
        }
    }
}
BENCHMARK(BM_combine_and_extract);

BENCHMARK_MAIN();
