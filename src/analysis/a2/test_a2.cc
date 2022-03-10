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
#include "a2.cc"
#include "a2_data_filter.h"
#include "memory.h"
#include "multiword_datafilter.h"
#include "util/nan.h"
#include "util/sizes.h"

#include <benchmark/benchmark.h>
#include <iostream>

using namespace a2;
using namespace memory;
using std::cout;
using std::endl;
using benchmark::Counter;

static void BM_a2(benchmark::State &state)
{
    Arena arena(Kilobytes(256));

    static u32 testdata[] =
    {
        0x0001,
        0x0102,
        0x0203,
        0x0304,

        0x040a,
        0x050f,
        0x060f,
        0x070e,

        0x0801,
        0x0902,
        0x0a03,
        0x0b04,

        0x0c0a,
        0x0d0f,
        0x0e0f,
        0x0f0e,
    };

    s32 dataSize = sizeof(testdata) / sizeof(*testdata);
    int eventIndex = 0;
    int moduleIndex = 0;
    const int moduleIterations = 1; // "modules per event"

    auto a2 = make_a2(&arena, { 1 }, { 2 });
    assert(a2->dataSources[eventIndex]);
    assert(a2->operators[eventIndex]);

    MultiWordFilter filter = { make_filter("xxxx aaaa xxxx dddd") };
    u32 requiredCompletions = 0;
    u64 rngSeed = 1234;
    auto ex = make_datasource_extractor(&arena, filter, requiredCompletions, rngSeed, moduleIndex);

    a2->dataSources[eventIndex][a2->dataSourceCounts[eventIndex]] = ex;
    a2->dataSourceCounts[eventIndex]++;

    auto calib = make_calibration(
        &arena,
        {
            ex.outputs[0],
            ex.outputLowerLimits[0],
            ex.outputUpperLimits[0]
        },
        0.0,
        100.0);
    calib.type = Operator_Calibration;

    a2->operators[eventIndex][a2->operatorCounts[eventIndex]] = calib;
    a2->operatorRanks[eventIndex][a2->operatorCounts[eventIndex]] = 1;
    a2->operatorCounts[eventIndex]++;

#if 0 // keepPrev
    auto keepPrev = make_keep_previous(
        &arena,
        { calib.outputs[0], calib.outputLowerLimits[0], calib.outputUpperLimits[0] },
        false);

    a2->operators[eventIndex][a2->operatorCounts[eventIndex]] = keepPrev;
    a2->operatorRanks[eventIndex][a2->operatorCounts[eventIndex]] = 2;
    a2->operatorCounts[eventIndex]++;
#endif

    double bytesProcessed = 0;
    double moduleCounter = 0;
    double eventCounter = 0;

    while (state.KeepRunning())
    {
        a2_begin_event(a2, eventIndex);

        for (size_t mi=0; mi < moduleIterations; mi++)
        {
            a2_process_module_data(a2, eventIndex, moduleIndex, testdata, dataSize);
            benchmark::ClobberMemory();
            bytesProcessed += sizeof(testdata);
            moduleCounter++;
        }

        a2_end_event(a2, eventIndex);
        eventCounter++;

        //print_param_vector(ex.output);
        //print_param_vector(calib.outputs[0]);
        //print_param_vector(keepPrev.outputs[0]);
        //if (++stateIters > 100) break;
    }

    state.counters["byteRate"] = Counter(bytesProcessed, Counter::kIsRate);
    state.counters["totalBytes"] = Counter(bytesProcessed);
    state.counters["mR"] = Counter(moduleCounter, Counter::kIsRate);
    state.counters["mT"] = Counter(moduleCounter);
    state.counters["eR"] = Counter(eventCounter, Counter::kIsRate);
    state.counters["eT"] = Counter(eventCounter);
    state.counters["mem"] = Counter(arena.used());

    //std::cout << "testdata: " << dataSize << ", " << sizeof(testdata) << " bytes" << std::endl;

    if (state.thread_index == 0)
    {
        //print_param_vector(ex.output);
    }
}
BENCHMARK(BM_a2);

BENCHMARK_MAIN();
