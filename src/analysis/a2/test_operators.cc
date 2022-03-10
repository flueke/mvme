/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "a2.h"
#include "a2_impl.h"
#include "a2_data_filter.h"
#include "memory.h"
#include "multiword_datafilter.h"
#include "util/nan.h"
#include "util/sizes.h"

#include <benchmark/benchmark.h>
#include <iostream>
#include <fstream>

using namespace a2;
using namespace data_filter;
using namespace memory;

using std::cout;
using std::endl;
using benchmark::Counter;

#define ArrayCount(x) (sizeof(x) / sizeof(*x))

#ifndef NDEBUG
// Comparator taking into account that extractors add a random in [0.0, 1.0).
// To catch a bug where the random value is lost we assume that the random is
// in (0.0, 1.0) and compare accordingly.
inline bool dcmp(double d, double expected)
{
    return expected < d && d <= expected + 1.0;
};
#endif


static void BM_extractor_begin_event(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    MultiWordFilter filter;

    add_subfilter(&filter, make_filter("xxxx aaaa xxxx dddd"));

    auto ex = arena.push(make_datasource_extractor(&arena, filter, 1, 1234, 0));

    assert(ex->outputs[0].size == (1u << 4));

    double eventsProcessed = 0;

    while (state.KeepRunning())
    {
        extractor_begin_event(ex);
        eventsProcessed++;
        benchmark::DoNotOptimize(ex);
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["eR"] = Counter(eventsProcessed, Counter::kIsRate);
    //state.counters["eT"] = Counter(eventsProcessed);
}
BENCHMARK(BM_extractor_begin_event);

static void BM_extractor_process_module_data(benchmark::State &state)
{
    static u32 inputData[] =
    {
        0x0001, 0x0102, 0x0203, 0x0304,
        0x040a, 0x050f, 0x060f, 0x070e,
        0x0801, 0x0902, 0x0a03, 0x0b04,
        0x0c0a, 0x0d0f, 0x0e0f, 0x0f0e,
    };

    static const s32 inputSize = ArrayCount(inputData);
    double bytesProcessed = 0;
    double moduleCounter = 0;

    Arena arena(Kilobytes(256));

    MultiWordFilter filter;

    add_subfilter(&filter, make_filter("xxxx aaaa xxxx dddd"));

    auto ex = arena.push(make_datasource_extractor(&arena, filter, 1, 1234, 0));

    assert(ex->outputs[0].size == (1u << 4));

    extractor_begin_event(ex);

#ifndef NDEBUG
    auto cmp = [](double d, double expected)
    {
        return expected <= d && d <= expected + 1.0;
    };
#endif

    while (state.KeepRunning())
    {
        extractor_process_module_data(ex, inputData, inputSize);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        for (s32 i = 0; i < inputSize; i++)
        {
            assert(cmp(ex->outputs[0][i], inputData[i] & 0xf));
        }
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    //state.counters["bT"] = Counter(bytesProcessed);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
    //state.counters["mT"] = Counter(moduleCounter);

    //print_param_vector(ex->output);
}
BENCHMARK(BM_extractor_process_module_data);

static void TEST_listfilter_extractor(benchmark::State &)
{
    // single extractor, 16 bit, not reversed, wordcount=2, repetitions=4
    {
        const u16 wordCount = 2;
        const u16 repetitions = 4; // contributes 2 address bits
        const u64 rngSeed = 1234;
        const u8  moduleIndex = 0;

        Arena arena(Kilobytes(256));

        static u32 inputData[] =
        {
            // [1] = 0x1111
            0x0001,
            0x1111,
            // [2] = 0x2222
            0x0002,
            0x2222,
            // [3] = 0x3333
            0x0003,
            0x3333,
            // [8] = 0x8888
            0x0008,
            0x8888,
        };

        static const u32 inputSize = ArrayCount(inputData);

        ListFilter cf = make_listfilter(
            ListFilter::NoFlag,
            wordCount,
            { "DDDD DDDD DDDD DDDD XXXX XXXX AAAA AAAA" });

        auto ce = make_datasource_listfilter_extractor(&arena, cf,
                                           repetitions, rngSeed,
                                           moduleIndex);

        assert(ce.outputs[0].size == (1u << (8 + 2)));

        listfilter_extractor_begin_event(&ce);

        const u32 *dataPtr = listfilter_extractor_process_module_data(&ce, inputData, inputSize);

        assert(dataPtr == inputData + inputSize); // all data should have been consumed
        assert(!is_param_valid(ce.outputs[0][0]));
        assert(!is_param_valid(ce.outputs[0][4]));

        // 8 address bits are extracted by the filter.
        // 2 additional address bits are generated by the repetition.
        // The filter is repeated 4 times, consuming all input data.
        // The filter addresses are 0x01, 0x02, 0x03 and 0x08.
        // The repetition addresses are 0, 1, 2, 3.
        // Thus the full addresses are 0x001, 0x102, 0x203 and 0x308

        assert(dcmp(ce.outputs[0][0x001], 0x1111));
        assert(dcmp(ce.outputs[0][0x102], 0x2222));
        assert(dcmp(ce.outputs[0][0x203], 0x3333));
        assert(dcmp(ce.outputs[0][0x308], 0x8888));
    }

    // single extractor, 16 bit, not reversed, wordcount=2, repetitions=4, repetition forms low address bits
    {
        const u16 wordCount = 2;
        const u16 repetitions = 4; // contributes 2 address bits
        const u64 rngSeed = 1234;
        const u8  moduleIndex = 0;

        Arena arena(Kilobytes(256));

        static u32 inputData[] =
        {
            // [1] = 0x1111
            0x0001,
            0x1111,
            // [2] = 0x2222
            0x0002,
            0x2222,
            // [3] = 0x3333
            0x0003,
            0x3333,
            // [8] = 0x8888
            0x0008,
            0x8888,
        };

        static const u32 inputSize = ArrayCount(inputData);

        ListFilter cf = make_listfilter(
            ListFilter::NoFlag,
            wordCount,
            { "DDDD DDDD DDDD DDDD XXXX XXXX AAAA AAAA" });

        auto ce = make_datasource_listfilter_extractor(&arena, cf,
                                                       repetitions, rngSeed,
                                                       moduleIndex,
                                                       DataSourceOptions::RepetitionContributesLowAddressBits
                                                      );

        assert(ce.outputs[0].size == (1u << (8 + 2)));

        listfilter_extractor_begin_event(&ce);

        const u32 *dataPtr = listfilter_extractor_process_module_data(&ce, inputData, inputSize);

        assert(dataPtr == inputData + inputSize); // all data should have been consumed

        // 8 address bits are extracted by the filter.
        // 2 additional address bits are generated by the repetition.
        // The filter is repeated 4 times, consuming all input data.
        // The filter addresses are 0x01, 0x02, 0x03 and 0x08.
        // The repetition addresses are 0, 1, 2, 3 and contribute the lowest 2
        // address bits.
        //
        // Thus the final address are
        // (0x01 << 2) | 0x0
        // (0x02 << 2) | 0x1
        // (0x03 << 2) | 0x2
        // (0x08 << 2) | 0x3

        assert(dcmp(ce.outputs[0][(0x01u << 2) | 0x0 ], 0x1111));
        assert(dcmp(ce.outputs[0][(0x02u << 2) | 0x1 ], 0x2222));
        assert(dcmp(ce.outputs[0][(0x03u << 2) | 0x2 ], 0x3333));
        assert(dcmp(ce.outputs[0][(0x08u << 2) | 0x3 ], 0x8888));
    }

    // 16 bit high word bug testing
    // This is the case used with mesytec modules: two 16 bit reads of the ctrA
    // registers, yielding two 32 bit words in the data stream. The first word
    // read contains the 16 low bits, the second word the 16 high bits.
    // Extract the low 16 bits of each of the words and combine them to a 32
    // bit counter value.
    {
        const u16 wordCount = 2;
        const u16 repetitions = 1;
        const u64 rngSeed = 1234;
        const u8  moduleIndex = 0;

        Arena arena(Kilobytes(256));

        static u32 inputData[] =
        {
            0x4321affe,
            0x87651001,
        };

        static const u32 inputSize = ArrayCount(inputData);

        ListFilter cf = make_listfilter(
            ListFilter::NoFlag,
            wordCount,
            { "DDDD DDDD DDDD DDDD DDDD DDDD DDDD DDDD" });

        auto ce = make_datasource_listfilter_extractor(&arena, cf,
                                                       repetitions, rngSeed,
                                                       moduleIndex);

        assert(ce.outputs[0].size == (1u << 0));

        listfilter_extractor_begin_event(&ce);

        const u32 *dataPtr = listfilter_extractor_process_module_data(&ce, inputData, inputSize);

        assert(dataPtr == inputData + inputSize);
        assert(is_param_valid(ce.outputs[0][0]));
        assert(dcmp(ce.outputs[0][0], 0x1001affe));
    }
}
BENCHMARK(TEST_listfilter_extractor);

static void BM_listfilter_extractor(benchmark::State &state)
{
    // single extractor, 16 bit, not reversed, wordcount=2, repetitions=4, using repetitionAddressFilter
    const u16 wordCount = 2;
    const u16 repetitions = 4;
    const u64 rngSeed = 1234;
    const u8  moduleIndex = 0;

    Arena arena(Kilobytes(256));

    static u32 inputData[] =
    {
        // [1] = 0x1111, rep=0
        0x0001,
        0x1111,
        // [2] = 0x2222, rep=1
        0x0002,
        0x2222,
        // [3] = 0x3333, rep=2
        0x0003,
        0x3333,
        // [8] = 0x8888, rep=3
        0x0008,
        0x8888,
    };

    static const u32 inputSize = ArrayCount(inputData);

    ListFilter cf = make_listfilter(
        ListFilter::NoFlag,
        wordCount,
        { "DDDD DDDD DDDD DDDD XXXX XXXX XXXX AAAA" });

    auto ce = make_datasource_listfilter_extractor(&arena, cf,
                                       repetitions, rngSeed,
                                       moduleIndex);

    assert(ce.outputs[0].size == (1u << (4 + 2)));

    listfilter_extractor_begin_event(&ce);

    double bytesProcessed = 0;
    double moduleCounter = 0;

    while (state.KeepRunning())
    {
        const u32 *dataPtr = listfilter_extractor_process_module_data(&ce, inputData, inputSize);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        assert(dataPtr == inputData + inputSize);
        assert(!is_param_valid(ce.outputs[0][0x00]));
        assert(!is_param_valid(ce.outputs[0][0x11]));
        assert(!is_param_valid(ce.outputs[0][0x22]));
        assert(dcmp(ce.outputs[0][0x01], 0x1111));
        assert(dcmp(ce.outputs[0][0x12], 0x2222));
        assert(dcmp(ce.outputs[0][0x23], 0x3333));
        assert(dcmp(ce.outputs[0][0x38], 0x8888));
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
}
BENCHMARK(BM_listfilter_extractor);


static void BM_calibration_step(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputData);
    static const s32 invalidIndex = 13;
    double bytesProcessed = 0;
    double moduleCounter = 0;

    auto calib = make_calibration(
        &arena,
        {
            ParamVec{inputData, inputSize},
            push_param_vector(&arena, inputSize, 0.0),
            push_param_vector(&arena, inputSize, 20.0),
        },
        0.0,
        200.0);

    while (state.KeepRunning())
    {
        calibration_step(&calib);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        assert(calib.outputCount == 1);
        assert(calib.outputs[0].size == inputSize);
        assert(calib.outputs[0].data[0] == 0.0);
        assert(calib.outputs[0].data[1] == 10.0);
        assert(calib.outputs[0].data[2] == 20.0);
        assert(calib.outputs[0].data[3] == 30.0);
        assert(!is_param_valid(calib.outputs[0].data[invalidIndex]));
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    //state.counters["bT"] = Counter(bytesProcessed);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
    //state.counters["mT"] = Counter(moduleCounter);

    //print_param_vector(calib.outputs[0]);
    //std::cout << "arena usage: " << arena.used() << std::endl;
    //std::cout << "inputData: " << inputSize << ", " << sizeof(inputData) << " bytes" << std::endl;
}
BENCHMARK(BM_calibration_step);

static void BM_calibration_SSE2_step(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputData);
    double bytesProcessed = 0;
    double moduleCounter = 0;

    //print_param_vector({inputData, inputSize});

    auto calib = make_calibration(
        &arena,
        {   {inputData, inputSize},
            push_param_vector(&arena, inputSize, 0.0),
            push_param_vector(&arena, inputSize, 20.0),
        },
        0.0,
        200.0);
    calib.type = Operator_Calibration_sse;

    while (state.KeepRunning())
    {
        calibration_sse_step(&calib);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        //print_param_vector(calib.outputs[0]);
        assert(calib.outputCount == 1);
        assert(calib.outputs[0].size == inputSize);
        assert(calib.outputs[0].data[0] == 0.0);
        assert(calib.outputs[0].data[1] == 10.0);
        assert(calib.outputs[0].data[2] == 20.0);
        assert(calib.outputs[0].data[3] == 30.0);
        assert(!is_param_valid(calib.outputs[0].data[13]));
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    //state.counters["bT"] = Counter(bytesProcessed);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
    //state.counters["mT"] = Counter(moduleCounter);

    //print_param_vector(calib.outputs[0]);
    //std::cout << "arena usage: " << arena.used() << std::endl;
    //std::cout << "inputData: " << inputSize << ", " << sizeof(inputData) << " bytes" << std::endl;
}
BENCHMARK(BM_calibration_SSE2_step);

static void BM_difference_step(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputDataA[] =
    {
        0.0, 1.0, 5.0, 10.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputDataA);
    static const s32 invalidIndex = 13;
    double bytesProcessed = 0;
    double moduleCounter = 0;

    static double inputDataB[inputSize];
#ifndef NDEBUG
    static double resultData[inputSize];
#endif

    for (s32 i = 0; i < inputSize; ++i)
    {
        inputDataB[i] = inputDataA[i] * 2 * (i % 2 == 0 ? 1 : -1);
#ifndef NDEBUG
        resultData[i] = inputDataA[i] - inputDataB[i];
#endif
    }

    PipeVectors inputA;
    inputA.data = ParamVec{inputDataA, inputSize};
    inputA.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    inputA.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    PipeVectors inputB;
    inputB.data = ParamVec{inputDataB, inputSize};
    inputB.lowerLimits = inputA.lowerLimits;
    inputB.upperLimits = inputA.upperLimits;

    auto diff = make_difference(&arena, inputA, inputB);

    while (state.KeepRunning())
    {
        difference_step(&diff);
        bytesProcessed += sizeof(inputDataA);
        moduleCounter++;

#ifndef NDEBUG
        for (s32 i = 0; i < inputSize; ++i)
        {
            if (i == invalidIndex)
            {
                assert(!is_param_valid(diff.outputs[0][i]));
            }
            else
            {
                assert(diff.outputs[0][i] == resultData[i]);
            }
        }
#endif
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    //state.counters["bT"] = Counter(bytesProcessed);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
    //state.counters["mT"] = Counter(moduleCounter);

    //print_param_vector(diff.outputs[0]);
    //std::cout << "arena usage: " << arena.used() << std::endl;
    //std::cout << "inputDataA: " << inputSize << ", " << sizeof(inputDataA) << " bytes" << std::endl;
}
BENCHMARK(BM_difference_step);

static void BM_array_map_step(benchmark::State &state)
{
    // TODO: multi input test
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputData);
    double bytesProcessed = 0;
    double moduleCounter = 0;

    static const s32 MappingsCount = inputSize;
    static ArrayMapData::Mapping mappings[MappingsCount];

    for (s32 outIdx = 0; outIdx < inputSize; outIdx++)
    {
        /* [paramOutIndex] = { inputIndex, paramInIndex } */
        mappings[outIdx] = { 0, (inputSize - outIdx - 1) % inputSize};
        //cout << outIdx << " -> " << (u32)mappings[outIdx].inputIndex << ", " << mappings[outIdx].paramIndex << endl;
    }

    PipeVectors input;
    input.data = { inputData, inputSize };
    input.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    input.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    auto am = make_array_map(
        &arena,
        make_typed_block(&input, 1),
        make_typed_block(mappings, MappingsCount));

    while (state.KeepRunning())
    {
        array_map_step(&am);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        for (s32 outIdx = 0; outIdx < am.outputs[0].size; outIdx++)
        {
            assert((am.outputs[0][outIdx] == inputData[(inputSize - outIdx - 1) % inputSize])
                   || (std::isnan(am.outputs[0][outIdx])
                       && std::isnan(inputData[(inputSize - outIdx - 1) % inputSize])));
        }
        //print_param_vector(am.outputs[0]);
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    //state.counters["bT"] = Counter(bytesProcessed);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
    //state.counters["mT"] = Counter(moduleCounter);
}
BENCHMARK(BM_array_map_step);

static void BM_keep_previous_step(benchmark::State &state)
{
    static double inputDataSets[2][16] =
    {
        {
            0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
            8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
        },
    };
    static const s32 inputSize = ArrayCount(inputDataSets[0]);

    for (s32 i = 0; i < inputSize; i++)
    {
        inputDataSets[1][i] = inputDataSets[0][inputSize - i - 1];
    }
    double bytesProcessed = 0;
    double moduleCounter = 0;

    Arena arena(Kilobytes(256));

    PipeVectors input;
    input.data = { inputDataSets[0], inputSize };
    input.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    input.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    // keepValid = false
    auto kp = make_keep_previous(&arena, input, false);

    assert(kp.outputs[0].size == inputSize);

    // step it once using inputDataSets[0] and make sure the output is invalid
    keep_previous_step(&kp);

    for (s32 i = 0; i < kp.outputs[0].size; i++)
    {
        assert(!is_param_valid(kp.outputs[0][i]));
    }

    u32 dataSetIndex = 1;

    while (state.KeepRunning())
    {
        kp.inputs[0].data = inputDataSets[dataSetIndex];

        keep_previous_step(&kp);

        bytesProcessed += sizeof(inputDataSets[0]);
        moduleCounter++;

        dataSetIndex ^= 1u;

        for (s32 i = 0; i < inputSize; i++)
        {
            assert((kp.outputs[0][i] == inputDataSets[dataSetIndex][i])
                   || (!is_param_valid(kp.outputs[0][i])
                       && !is_param_valid(inputDataSets[dataSetIndex][i])));
        }
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    //state.counters["bT"] = Counter(bytesProcessed);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
    //state.counters["mT"] = Counter(moduleCounter);
}
BENCHMARK(BM_keep_previous_step);

static void BM_aggregate_sum_step(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputData);
    double bytesProcessed = 0;
    double moduleCounter = 0;

    PipeVectors input;
    input.data = { inputData, inputSize };
    input.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    input.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    Thresholds thresholds = { 0.0, 20.0 };

    auto op = make_aggregate_sum(
        &arena,
        input,
        thresholds);

    assert(op.outputCount == 1);
    assert(op.outputs[0].size == 1);

    double expectedResult = 0.0;

    for (s32 i = 0; i < inputSize; i++)
    {
        if (!std::isnan(inputData[i]))
        {
            expectedResult += inputData[i];
        }
    }

    assert(op.outputLowerLimits[0][0] == 0.0);
    assert(op.outputUpperLimits[0][0] == inputSize * 20.0);

    while (state.KeepRunning())
    {
        aggregate_sum_step(&op);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        assert(op.outputs[0][0] == expectedResult);

        //print_param_vector(op.outputs[0]);
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
}
BENCHMARK(BM_aggregate_sum_step);

static void BM_aggregate_multiplicity_step(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputData);
    double bytesProcessed = 0;
    double moduleCounter = 0;

    PipeVectors input;
    input.data = { inputData, inputSize };
    input.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    input.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    Thresholds thresholds = { 0.0, 20.0 };

    auto op = make_aggregate_multiplicity(
        &arena,
        input,
        thresholds);

    assert(op.outputCount == 1);
    assert(op.outputs[0].size == 1);

    double expectedResult = inputSize - 1;

    assert(op.outputLowerLimits[0][0] == 0.0);
    assert(op.outputUpperLimits[0][0] == inputSize);

    while (state.KeepRunning())
    {
        aggregate_multiplicity_step(&op);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        assert(op.outputs[0][0] == expectedResult);

        //print_param_vector(op.outputs[0]);
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
}
BENCHMARK(BM_aggregate_multiplicity_step);

static void BM_aggregate_max_step(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputData);
    double bytesProcessed = 0;
    double moduleCounter = 0;

    PipeVectors input;
    input.data = { inputData, inputSize };
    input.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    input.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    Thresholds thresholds = { 0.0, 20.0 };

    auto op = make_aggregate_max(
        &arena,
        input,
        thresholds);

    assert(op.outputCount == 1);
    assert(op.outputs[0].size == 1);

    double expectedResult = 15;

    assert(op.outputLowerLimits[0][0] == 0.0);
    assert(op.outputUpperLimits[0][0] == 20.0);

    while (state.KeepRunning())
    {
        aggregate_max_step(&op);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        assert(op.outputs[0][0] == expectedResult);

        //print_param_vector(op.outputs[0]);
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
}
BENCHMARK(BM_aggregate_max_step);

static void BM_h1d_sink_step(benchmark::State &state)
{
    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputData);
    double bytesProcessed = 0;
    double moduleCounter = 0;

    static const s32 histoBins = 20;
    static H1D histograms[inputSize];

    Arena histArena(Kilobytes(256));

    for (s32 i=0; i < inputSize; i++)
    {
        histograms[i] = {};
        auto storage = push_param_vector(&histArena, histoBins, 0.0);
        histograms[i].data = storage.data;
        histograms[i].size = storage.size;
        histograms[i].binningFactor = storage.size / 20.0;
        histograms[i].binning.min = 0.0;
        histograms[i].binning.range = 20.0;
    }

    auto histos = make_typed_block(histograms, inputSize);

    Arena arena(Kilobytes(256));

    PipeVectors input;
    input.data = { inputData, inputSize };
    input.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    input.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    auto sink = make_h1d_sink(&arena, input, histos);
    auto d = reinterpret_cast<H1DSinkData *>(sink.d);

    while (state.KeepRunning())
    {
        h1d_sink_step(&sink);
        bytesProcessed += sizeof(inputData);
        moduleCounter++;


        //for (s32 i = 0; i < inputSize; i++)
        //{
        //    cout << i << " -> " << d->histos[i].entryCount << endl;
        //}
        //print_param_vector(d->histos[0]);
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["hMem"] = Counter(histArena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    //state.counters["bT"] = Counter(bytesProcessed);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
    //state.counters["mT"] = Counter(moduleCounter);

    if (state.thread_index == 0)
    {
#if 0
        auto histo = d->histos[0];
        printf("h1d data@%p, size=%d, bin.min=%lf, bin.range=%lf, uf=%lf, of=%lf\n",
               histo.data, histo.size,
               histo.binning.min, histo.binning.range,
               histo.underflow, histo.overflow);

        print_param_vector(d->histos[0]);
#endif

        std::ofstream histoOut("h1d_sink_step.histos", std::ios::binary);
        write_histo_list(histoOut, d->histos);
    }
}
BENCHMARK(BM_h1d_sink_step);

// FIXME: BM_h2d_sink_step() is bad
static void BM_h2d_sink_step(benchmark::State &state)
{
    static double xValues[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };

    static double yValues[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };

    static const s32 inputSize = ArrayCount(xValues);
    double bytesProcessed = 0;
    double moduleCounter = 0;

    static const s32 histoBins = 20;
    H2D histo;

    Arena histArena(Kilobytes(256));

    auto storage = push_param_vector(&histArena, histoBins * histoBins, 0.0);

    histo.data = storage.data;
    histo.size = storage.size;

    histo.binCounts[H2D::XAxis] = histoBins;
    histo.binnings[H2D::XAxis].min = 0.0;
    histo.binnings[H2D::XAxis].range = 20.0;
    histo.binningFactors[H2D::XAxis] = histoBins / 20.0;

    histo.binCounts[H2D::YAxis] = histoBins;
    histo.binnings[H2D::YAxis].min = 0.0;
    histo.binnings[H2D::YAxis].range = 20.0;
    histo.binningFactors[H2D::YAxis] = histoBins / 20.0;

    Arena arena(Kilobytes(256));

    // one value per iteration

    PipeVectors xInput;
    xInput.data = { xValues, 1 };
    xInput.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    xInput.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    PipeVectors yInput;
    yInput.data = { yValues, 1 };
    yInput.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    yInput.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    auto sink = make_h2d_sink(
        &arena,
        xInput,
        yInput,
        0, // xIndex
        0, // yIndex
        histo);

    while (state.KeepRunning())
    {
        h2d_sink_step(&sink);
        bytesProcessed += sizeof(double) * 2;
        moduleCounter++;

        // next x/y values
        sink.inputs[0].data++;
        sink.inputs[1].data++;

        if (sink.inputs[0].data >= xValues + inputSize)
            sink.inputs[0].data = xValues;

        if (sink.inputs[1].data >= yValues + inputSize)
            sink.inputs[1].data = yValues;

        //print_param_vector(d->histo);
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["hMem"] = Counter(histArena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
}
BENCHMARK(BM_h2d_sink_step);

#if 0
static void BM_binary_equation_step(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputDataA[] =
    {
        0.0, 1.0, 5.0, 10.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };
    static const s32 inputSize = ArrayCount(inputDataA);
    static const s32 invalidIndex = 13;
    double bytesProcessed = 0;
    double moduleCounter = 0;

    static double inputDataB[inputSize];
#ifndef NDEBUG
    static double resultData[inputSize];
#endif

    for (s32 i = 0; i < inputSize; ++i)
    {
        inputDataB[i] = inputDataA[i] * 2 * (i % 2 == 0 ? 1 : -1);
#ifndef NDEBUG
        resultData[i] = inputDataA[i] - inputDataB[i];
#endif
    }

    PipeVectors inputA;
    inputA.data = ParamVec{inputDataA, inputSize};
    inputA.lowerLimits = push_param_vector(&arena, inputSize, 0.0);
    inputA.upperLimits = push_param_vector(&arena, inputSize, 20.0);

    PipeVectors inputB;
    inputB.data = ParamVec{inputDataB, inputSize};
    inputB.lowerLimits = inputA.lowerLimits;
    inputB.upperLimits = inputA.upperLimits;

    auto diff = make_difference(&arena, inputA, inputB);

    auto op make_binary_equation(
        memory::Arena *arena,
        PipeVectors inputA,
        PipeVectors inputB,
        u32 equationIndex, // stored right inside the d pointer so it can be at least u32 in size
        double outputLowerLimit,
        double outputUpperLimit);

    while (state.KeepRunning())
    {
        difference_step(&diff);
        bytesProcessed += sizeof(inputDataA);
        moduleCounter++;
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);

    //print_param_vector(diff.outputs[0]);
}
BENCHMARK(BM_binary_equation_step);
#endif

//#warning "missing test for binary_equation_step"


#if 0
static void TEST_expression_operator_create(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };

    static const s32 inputSize = ArrayCount(inputData);
    static const s32 invalidIndex = 13;
    double bytesProcessed = 0;
    double moduleCounter = 0;

    std::string begin_expr =
        "var lower_limits[input_lower_limits[]] := input_lower_limits + 1;"
        "var upper_limits[input_upper_limits[]] := input_upper_limits * 2;"
        "return [lower_limits, upper_limits];"
        ;

    std::string step_expr =
        "output := input * 2;"
        ;

    auto expr_op = make_expression_operator(
        &arena,
        {
            ParamVec{inputData, inputSize},
            push_param_vector(&arena, inputSize, 0.0),  // lower limits
            push_param_vector(&arena, inputSize, 20.0), // upper limits
        },
        begin_expr,
        step_expr);

    assert(expr_op.outputCount == 1);
    assert(expr_op.outputs[0].size == inputSize);

    for (s32 i = 0; i < inputSize; i++)
    {
        assert(expr_op.outputLowerLimits[0][i] == 1.0);
        assert(expr_op.outputUpperLimits[0][i] == 40.0);
    }
}
BENCHMARK(TEST_expression_operator_create);

static void TEST_expression_operator_step(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static double inputData[] =
    {
        0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
        8.0, 9.0, 10.0, 11.0, 12.0, invalid_param() /* @[13] */, 14.0, 15.0,
    };

    static const s32 inputSize = ArrayCount(inputData);
    static const s32 invalidIndex = 13;
    double bytesProcessed = 0;
    double moduleCounter = 0;

    std::string begin_expr =
        "var lower_limits[input_lower_limits[]] := input_lower_limits + 1;"
        "var upper_limits[input_upper_limits[]] := input_upper_limits * 2;"
        "return [lower_limits, upper_limits];"
        ;

    std::string step_expr =
#if 0 // operation on the whole vector
        "output := input * 2;"
#else // manual loop through the vectors
        "for (var i := 0; i < input[]; i += 1)"
        "{"
        "   output[i] := input[i] * 2;"
        "}"
#endif
        ;

    auto expr_op = make_expression_operator(
        &arena,
        {
            ParamVec{inputData, inputSize},
            push_param_vector(&arena, inputSize, 0.0),  // lower limits
            push_param_vector(&arena, inputSize, 20.0), // upper limits
        },
        begin_expr,
        step_expr);

    while (state.KeepRunning())
    {
        expression_operator_step(&expr_op);

        bytesProcessed += sizeof(inputData);
        moduleCounter++;

        for (s32 i = 0; i < expr_op.outputs[0].size; i++)
        {
            //fprintf(stderr, "outputs[0][%d] = %lf\n",
            //        i, expr_op.outputs[0][i]);

            if (i != 13)
            {
                assert(expr_op.outputs[0][i] == inputData[i] * 2);
            }
            else
            {
                assert(std::isnan(expr_op.outputs[0][i]));
            }
        }
    }

    state.counters["mem"] = Counter(arena.used());
    state.counters["bR"] = Counter(bytesProcessed, Counter::kIsRate);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
}
BENCHMARK(TEST_expression_operator_step);
#endif

#if 0
static void TEST_condition_filter_step(benchmark::State &state)
{
    assert(!"implement me!");
}
BENCHMARK(TEST_condition_filter_step);
#endif


BENCHMARK_MAIN();
