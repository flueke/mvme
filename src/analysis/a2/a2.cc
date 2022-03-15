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
#include "mpmc_queue.cc"
#include "a2_impl.h"
#include "util/assert.h"
#include "util/perf.h"
#include <cpp11-on-multicore/common/benaphore.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <queue>
#include <random>
#include <vector>
#include <zstr.hpp>

/* Circumvent compile errors related to the 'Q' numeric literal suffix.
 * See https://svn.boost.org/trac10/ticket/9240 and
 * https://www.boost.org/doc/libs/1_68_0/libs/math/doc/html/math_toolkit/config_macros.html
 * for details. */
#define BOOST_MATH_DISABLE_FLOAT128
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>

#define ArrayCount(x) (sizeof(x) / sizeof(*x))

//#ifndef NDEBUG
#if 0

// printf style trace macro
#define a2_trace(fmt, ...)\
do\
{\
    fprintf(stderr, "a2::%s() " fmt, __FUNCTION__, ##__VA_ARGS__);\
} while (0);

// "NoPrefix" verison of the trace macro
#define a2_trace_np(fmt, ...)\
do\
{\
    fprintf(stderr, fmt, ##__VA_ARGS__);\
} while (0);

#else

#define a2_trace(...)
#define a2_trace_np(...)

#endif

#include <iostream>

using std::cerr;
using std::endl;

namespace a2
{

using namespace data_filter;
using namespace memory;

/* TODO list
 * - Add tests for range_filter_step(). Test it in mvme.
 * - Test aggregate mean and meanx
 * - Add logic to force internal input/output vectors to be rounded up to a
 *   specific power of 2. This is needed to efficiently use vector instructions
 *   in the _step() loops I think.
 *
 * - Try an extractor for single word filters. Use the same system as for
 *   operators: a global function table. This means that
 *   a2_process_module_data() has to do a lookup and dispatch instead of
 *   passing directly to the extractor.
 * - Better tests. Test edge cases using nan, inf, -inf. Document the behaviour.
 * - Test and document the behaviour for invalids.
 * - Support negative axis values (works for h2d)
 * - Think about aligning operators to cache lines (in addition to the double
 *   arrays).
 */

/* Alignment in bytes of all double vectors created by the system.
 * SSE requires 16 byte alignment (128 bit registers).
 * AVX wants 32 bytes (256 bit registers).
 *
 * Another factor is the cache line size. On Skylake it's 64 bytes.
 */
static const size_t ParamVecAlignment = 64;

/* Asserted in extractor_process_module_data(). */
static const size_t ModuleDataAlignment = alignof(u32);

void print_param_vector(ParamVec pv)
{
    printf("pv data@%p, size=%d, %lu bytes\n",
           pv.data, pv.size, pv.size * sizeof(double));

    for (s32 i = 0; i < pv.size; i++)
    {
        if (is_param_valid(pv.data[i]))
        {
            printf("  [%2d] %lf\n", i, pv.data[i]);
        }
        else
        {
            printf("  [%2d] %lf, payload=0x%x\n",
                   i, pv.data[i], get_payload(pv.data[i]));
        }
    }
}

ParamVec push_param_vector(Arena *arena, s32 size)
{
    assert(size >= 0);

    ParamVec result;

    result.data = arena->pushArray<double>(size, ParamVecAlignment);
    result.size = result.data ? size : 0;
    assert(is_aligned(result.data, ParamVecAlignment));

    return result;
}

ParamVec push_param_vector(Arena *arena, s32 size, double value)
{
    assert(size >= 0);
    ParamVec result = push_param_vector(arena, size);
    fill(result, value);
    return result;
}

void assign_input(Operator *op, PipeVectors input, s32 inputIndex)
{
    assert(inputIndex < op->inputCount);
    op->inputs[inputIndex] = input.data;
    op->inputLowerLimits[inputIndex] = input.lowerLimits;
    op->inputUpperLimits[inputIndex] = input.upperLimits;
}

void invalidate_outputs(Operator *op)
{
    for (u8 outIdx = 0; outIdx < op->outputCount; outIdx++)
    {
        invalidate_all(op->outputs[outIdx]);
    }
}

/* ===============================================
 * Extractors / DataSources
 * =============================================== */
static std::uniform_real_distribution<double> RealDist01(0.0, 1.0);

/** Creates a DataSource with the specified type, moduleIndex and outputCount.
 *
 * To make the DataSource functional output parameter vectors have to be
 * created and setup using push_output_vectors().
 */
DataSource make_datasource(Arena *arena, u8 type, u8 moduleIndex, u8 outputCount)
{
    DataSource result = {};

    result.outputs = arena->pushArray<ParamVec>(outputCount);
    result.outputLowerLimits = arena->pushArray<ParamVec>(outputCount);
    result.outputUpperLimits = arena->pushArray<ParamVec>(outputCount);
    result.hitCounts = arena->pushArray<ParamVec>(outputCount);

    result.type = type;
    result.moduleIndex = moduleIndex;
    result.outputCount = outputCount;
    result.d = nullptr;

    return result;
}


size_t get_address_count(DataSource *ds)
{
    switch (static_cast<DataSourceType>(ds->type))
    {
        case DataSource_Extractor:
            {
                auto ex = reinterpret_cast<Extractor *>(ds->d);
                return get_address_count(ex);
            } break;

        case DataSource_ListFilterExtractor:
            {
                auto ex = reinterpret_cast<ListFilterExtractor *>(ds->d);
                return get_address_count(ex);
            } break;

        case DataSource_MultiHitExtractor_ArrayPerHit:
        case DataSource_MultiHitExtractor_ArrayPerAddress:
            {
                auto ex = reinterpret_cast<MultiHitExtractor *>(ds->d);
                return get_address_count(ex);
            } break;

#if 0
        case DataSource_Copy:
            return ds->outputs[0].size;
#endif
    }

    assert(false);

    return 0u;
}

size_t get_address_count(Extractor *ex)
{
    u16 bits = get_extract_bits(&ex->filter, MultiWordFilter::CacheA);
    return 1u << bits;
}

size_t get_base_address_bits(ListFilterExtractor *ex)
{
    size_t baseAddressBits = get_extract_bits(&ex->listFilter, MultiWordFilter::CacheA);
    return baseAddressBits;
}

size_t get_repetition_address_bits(ListFilterExtractor *ex)
{
    size_t result = static_cast<size_t>(std::ceil(std::log2(ex->repetitions)));
    return result;
}

size_t get_address_bits(ListFilterExtractor *ex)
{
    size_t baseAddressBits = get_base_address_bits(ex);
    size_t repAddressBits  = get_repetition_address_bits(ex);
    size_t bits = baseAddressBits + repAddressBits;
    return bits;
}

size_t get_address_count(ListFilterExtractor *ex)
{
    return 1u << get_address_bits(ex);;
}

// Extractor

Extractor make_extractor(
    data_filter::MultiWordFilter filter,
    u32 requiredCompletions,
    u64 rngSeed,
    DataSourceOptions::opt_t options)
{
    Extractor ex = {};

    ex.filter = filter;
    ex.requiredCompletions = requiredCompletions;
    ex.currentCompletions = 0;
    ex.rng.seed(rngSeed);
    ex.options = options;

    return ex;
}

DataSource make_datasource_extractor(
    Arena *arena,
    MultiWordFilter filter,
    u32 requiredCompletions,
    u64 rngSeed,
    int moduleIndex,
    DataSourceOptions::opt_t options)
{
    auto result = make_datasource(arena, DataSource_Extractor, moduleIndex, 1);

    auto ex = arena->pushObject<Extractor>();
    *ex = make_extractor(filter, requiredCompletions, rngSeed, options);
    result.d = ex;

    size_t addrCount = get_address_count(&result);

    // The highest value the filter will yield is ((2^bits) - 1) but we're
    // adding a random in [0.0, 1.0) so the actual exclusive upper limit is
    // (2^bits).
    double upperLimit = std::pow(2.0, get_extract_bits(&ex->filter, MultiWordFilter::CacheD));

    push_output_vectors(arena, &result, 0, addrCount, 0.0, upperLimit);

    return  result;
}

void extractor_begin_event(DataSource *ds)
{
    assert(ds->type == DataSource_Extractor);
    auto ex = reinterpret_cast<Extractor *>(ds->d);
    clear_completion(&ex->filter);
    ex->currentCompletions = 0;
    invalidate_all(ds->outputs[0]);
}

void extractor_process_module_data(DataSource *ds, const u32 *data, u32 size)
{
    assert(memory::is_aligned(data, ModuleDataAlignment));
    assert(ds->type == DataSource_Extractor);

    auto ex = reinterpret_cast<Extractor *>(ds->d);

    for (u32 wordIndex = 0;
         wordIndex < size;
         wordIndex++)
    {
        u32 dataWord = data[wordIndex];

        if (process_data(&ex->filter, dataWord, wordIndex))
        {
            ex->currentCompletions++;

            if (ex->currentCompletions >= ex->requiredCompletions)
            {
                ex->currentCompletions = 0;
                u64  address = extract(&ex->filter, MultiWordFilter::CacheA);
                double value = static_cast<double>(extract(&ex->filter, MultiWordFilter::CacheD));

                assert(address < static_cast<u64>(ds->outputs[0].size));

                if (!is_param_valid(ds->outputs[0][address]))
                {
                    if (!(ex->options & DataSourceOptions::NoAddedRandom))
                        value += RealDist01(ex->rng);

                    ds->outputs[0][address] = value;
                    ds->hitCounts[0][address]++;
                }
            }

            clear_completion(&ex->filter);
        }
    }
}

// ListFilterExtractor
ListFilterExtractor make_listfilter_extractor(
    data_filter::ListFilter listFilter,
    u8 repetitions,
    u64 rngSeed,
    DataSourceOptions::opt_t options)
{
    ListFilterExtractor ex = {};

    ex.listFilter = listFilter;
    ex.rng.seed(rngSeed);
    ex.repetitions = repetitions;
    ex.options = options;

    return ex;
}

DataSource make_datasource_listfilter_extractor(
    memory::Arena *arena,
    data_filter::ListFilter listFilter,
    u8 repetitions,
    u64 rngSeed,
    u8 moduleIndex,
    DataSourceOptions::opt_t options)
{
    auto result = make_datasource(arena, DataSource_ListFilterExtractor, moduleIndex, 1);

    auto ex = arena->pushObject<ListFilterExtractor>();
    *ex = make_listfilter_extractor(listFilter, repetitions, rngSeed, options);
    result.d = ex;

    // This call works because listFilter and repetitionAddressCache have been
    // initialzed at this point.
    size_t addressCount = get_address_count(&result);

    auto databits = get_extract_bits(&listFilter.extractionFilter,
                                     MultiWordFilter::CacheD);

    double upperLimit = std::pow(2.0, databits);

    push_output_vectors(arena, &result, 0, addressCount, 0.0, upperLimit);

    return result;
}

void listfilter_extractor_begin_event(DataSource *ds)
{
    assert(ds->type == DataSource_ListFilterExtractor);
    invalidate_all(ds->outputs[0]);
}

const u32 *listfilter_extractor_process_module_data(DataSource *ds, const u32 *data, u32 dataSize)
{
    assert(ds->type == DataSource_ListFilterExtractor);

    const u32 *curPtr = data;
    u32 curSize = dataSize;

    auto ex = reinterpret_cast<ListFilterExtractor *>(ds->d);

    const u16 baseAddressBits = get_base_address_bits(ex);
    const u16 repetitionBits  = get_repetition_address_bits(ex);

    assert(ex->repetitions <= (1u << repetitionBits));

    for (u32 rep = 0; rep < ex->repetitions; rep++)
    {
        // Combine input data words and extract address and data values.
        u64 combined = combine(&ex->listFilter, curPtr, curSize);
        curPtr += ex->listFilter.wordCount;
        curSize -= ex->listFilter.wordCount;

        auto result = extract_address_and_value_from_combined(&ex->listFilter, combined);

        //printf("combined=%lx, addr=%lx, data=%lx\n", combined, result.address, result.value);

        if (!result.matched)
            continue;

        u64  address = result.address;
        double value = result.value;

        // Make the address bits from the repetition number contribute to the
        // final address value.
        if (ex->options & DataSourceOptions::RepetitionContributesLowAddressBits)
        {
            address = (address << repetitionBits) | rep;
        }
        else
        {
            address |= (rep << baseAddressBits);
        }

        assert(address < static_cast<u64>(ds->outputs[0].size));

        if (!is_param_valid(ds->outputs[0][address]))
        {
            if (!(ex->options & DataSourceOptions::NoAddedRandom))
                value += RealDist01(ex->rng);

            ds->outputs[0][address] = value;
            ds->hitCounts[0][address]++;
        }

        if (curPtr >= data + dataSize)
            break;
    }

    return curPtr;
}

// MultiHitExtractor
MultiHitExtractor make_multihit_extractor(
    MultiHitExtractor::Shape shape,
    const data_filter::DataFilter &filter,
    u16 maxHits,
    u64 rngSeed,
    DataSourceOptions::opt_t options)
{
    MultiHitExtractor mex = {};

    mex.shape = shape;
    mex.filter = filter;
    mex.cacheA = make_cache_entry(filter, 'A');
    mex.cacheD = make_cache_entry(filter, 'D');
    mex.maxHits = maxHits;
    mex.rng.seed(rngSeed);
    mex.options = options;

    return mex;
}

DataSource make_datasource_multihit_extractor(
    memory::Arena *arena,
    MultiHitExtractor::Shape shape,
    const data_filter::DataFilter &filter,
    u16 maxHits,
    u64 rngSeed,
    int moduleIndex,
    DataSourceOptions::opt_t options)
{
    static const double TotalHitsUpperLimit = 10.0;

    auto ex = arena->pushObject<MultiHitExtractor>();
    *ex = make_multihit_extractor(shape, filter, maxHits, rngSeed, options);
    size_t addressCount = get_address_count(ex);
    size_t dataBits = get_extract_bits(filter, 'D');
    double upperLimit = std::pow(2.0, dataBits);

    DataSource result = {};

    switch (shape)
    {
        case MultiHitExtractor::ArrayPerHit:
            {
                result = make_datasource(
                    arena, DataSource_MultiHitExtractor_ArrayPerHit,
                    moduleIndex, maxHits + 1);

                // Output arrays
                for (u16 hit=0; hit<maxHits; ++hit)
                    push_output_vectors(arena, &result, hit, addressCount, 0.0, upperLimit);

                // TotalHits output
                push_output_vectors(arena, &result, maxHits, addressCount, 0.0, TotalHitsUpperLimit);
                auto &totalHits = result.outputs[result.outputCount-1];
                std::fill(totalHits.data, totalHits.data + totalHits.size, 0.0);

            } break;

        case MultiHitExtractor::ArrayPerAddress:
            {
                result = make_datasource(
                    arena, DataSource_MultiHitExtractor_ArrayPerAddress,
                    moduleIndex, addressCount + 1);

                // Output arrays
                for (size_t addr=0; addr<addressCount; ++addr)
                    push_output_vectors(arena, &result, addr, maxHits, 0.0, upperLimit);

                // TotalHits output
                push_output_vectors(arena, &result, addressCount, addressCount, 0.0, TotalHitsUpperLimit);
                auto &totalHits = result.outputs[result.outputCount-1];
                std::fill(totalHits.data, totalHits.data + totalHits.size, 0.0);

            } break;

        default:
            throw std::runtime_error("Unknown MultiHitExtractor::Shape");
    }

    result.d = ex;

    return result;
}

void multihit_extractor_begin_event(DataSource *ds)
{
    assert(ds);
    assert(ds->type == DataSource_MultiHitExtractor_ArrayPerHit
           || ds->type == DataSource_MultiHitExtractor_ArrayPerAddress);

    // Invalidate all output arrays except for the totalHits array.
    for (u8 outIndex=0; outIndex<ds->outputCount-1; ++outIndex)
    {
        invalidate_all(ds->outputs[outIndex]);
    }

    // Set totalHits to 0.0
    auto &totalHits = ds->outputs[ds->outputCount-1];
    fill(totalHits, 0.0);
}

namespace
{

inline void multihit_extractor_process_module_data_array_per_hit(
    DataSource *ds, const u32 *data, u32 dataSize)
{
    auto ex = reinterpret_cast<MultiHitExtractor *>(ds->d);
    assert(ex->shape == MultiHitExtractor::Shape::ArrayPerHit);

    auto &totalHits = ds->outputs[ds->outputCount-1];
    assert(std::all_of(totalHits.data, totalHits.data+totalHits.size,
                       [] (double d) { return !std::isnan(d); }));

    for (u32 wordIndex = 0; wordIndex < dataSize; wordIndex++)
    {
        u32 dataWord = data[wordIndex];

        if (matches(ex->filter, dataWord, wordIndex))
        {
            u32 address = extract(ex->cacheA, dataWord);

            // Save the value in the first output array that does not
            // contain a valid value yet.
            for (u8 outIndex = 0; outIndex < ds->outputCount-1; ++outIndex)
            {
                if (!is_param_valid(ds->outputs[outIndex][address]))
                {
                    double value = extract(ex->cacheD, dataWord);

                    if (!(ex->options & DataSourceOptions::NoAddedRandom))
                        value += RealDist01(ex->rng);

                    ds->outputs[outIndex][address] = value;
                    ++ds->hitCounts[outIndex][address];
                    break;
                }
            }

            ++totalHits[address];
        }
    }
}

inline void multihit_extractor_process_module_data_array_per_address(
    DataSource *ds, const u32 *data, u32 dataSize)
{
    auto ex = reinterpret_cast<MultiHitExtractor *>(ds->d);
    assert(ex->shape == MultiHitExtractor::Shape::ArrayPerAddress);

    auto &totalHits = ds->outputs[ds->outputCount-1];
    assert(std::all_of(totalHits.data, totalHits.data+totalHits.size,
                       [] (double d) { return !std::isnan(d); }));

    for (u32 wordIndex = 0; wordIndex < dataSize; wordIndex++)
    {
        u32 dataWord = data[wordIndex];

        if (matches(ex->filter, dataWord, wordIndex))
        {
            // The extracted address specifies the output number to store the
            // value in.
            u32 address = extract(ex->cacheA, dataWord);
            auto &output = ds->outputs[address];

            // Store the value in the first non-valid entry of the output
            // array.
            for (s32 paramIndex=0; paramIndex<output.size; ++paramIndex)
            {
                if (!is_param_valid(output[paramIndex]))
                {
                    double value = extract(ex->cacheD, dataWord);

                    if (!(ex->options & DataSourceOptions::NoAddedRandom))
                        value += RealDist01(ex->rng);

                    output[paramIndex] = value;
                    ++ds->hitCounts[address][paramIndex];
                    break;
                }
            }

            ++totalHits[address];
        }
    }
}

} // end anon namespace

void multihit_extractor_process_module_data(DataSource *ds, const u32 *data, u32 dataSize)
{
    assert(ds);
    assert(ds->type == DataSource_MultiHitExtractor_ArrayPerHit
           || ds->type == DataSource_MultiHitExtractor_ArrayPerAddress);
    assert(memory::is_aligned(data, ModuleDataAlignment));

    switch (ds->type)
    {
        case DataSourceType::DataSource_MultiHitExtractor_ArrayPerHit:
            multihit_extractor_process_module_data_array_per_hit(ds, data, dataSize);
            break;

        case DataSourceType::DataSource_MultiHitExtractor_ArrayPerAddress:
            multihit_extractor_process_module_data_array_per_address(ds, data, dataSize);
            break;
    }
}

// DataSource_Copy

#if 0
DataSource make_datasource_copy(
    memory::Arena *arena,
    u32 outputSize,
    double outputLowerLimit,
    double outputUpperLimit,
    u32 dataStartIndex)
{
    auto dsc = arena->pushObject<DataSourceCopy>();
    dsc->startIndex = dataStartIndex;

    DataSource result = {};
    result.type = DataSource_Copy;
    result.d = dsc;
    result.output.data = push_param_vector(arena, outputSize, invalid_param());
    result.output.lowerLimits = push_param_vector(arena, outputSize, outputLowerLimit);
    result.output.upperLimits = push_param_vector(arena, outputSize, outputUpperLimit);
    result.hitCounts = push_param_vector(arena, outputSize, 0.0);

    return result;
}

void datasource_copy_begin_event(DataSource *ds)
{
    assert(ds->type == DataSource_Copy);
    invalidate_all(ds->output.data);
}

void datasource_copy_process_module_data(DataSource *ds, const u32 *data, u32 size)
{
    assert(memory::is_aligned(data, ModuleDataAlignment));
    assert(ds->type == DataSource_Copy);

    auto dsc = reinterpret_cast<DataSourceCopy *>(ds->d);

    const u32 *begin = data + dsc->startIndex;
    const u32 *dataEnd = data + size;
    const u32 *end = std::min(begin + ds->output.size(), dataEnd);
    double *dest = ds->output.data.data;

    for (const u32 *cur = begin; cur < end; ++cur, ++dest)
        *dest = *cur;
}
#endif

/* ===============================================
 * Operators
 * =============================================== */

/** Creates an operator with the specified type and input and output counts.
 *
 * To make the operator functional inputs have to be set using assign_input()
 * and output parameter vectors have to be created and setup using
 * push_output_vectors(). */
Operator make_operator(Arena *arena, u8 type, u8 inputCount, u8 outputCount)
{
    Operator result = {};

    result.inputs = arena->pushArray<ParamVec>(inputCount);
    result.inputLowerLimits = arena->pushArray<ParamVec>(inputCount);
    result.inputUpperLimits = arena->pushArray<ParamVec>(inputCount);

    result.outputs = arena->pushArray<ParamVec>(outputCount);
    result.outputLowerLimits = arena->pushArray<ParamVec>(outputCount);
    result.outputUpperLimits = arena->pushArray<ParamVec>(outputCount);

    result.type = type;
    result.inputCount = inputCount;
    result.outputCount = outputCount;
    result.conditionIndex = Operator::NoCondition;
    result.d = nullptr;

    return  result;
}

/* Calibration equation:
 * paramRange  = paramMax - paramMin    (the input range)
 * calibRange  = calibMax - calibMin    (the output range)
 * calibFactor = calibRange / paramRange
 * param = (param - paramMin) * (calibMax - calibMin) / (paramMax - paramMin) + calibMin;
 *       = (param - paramMin) * calibRange / paramRange + calibMin;
 *       = (param - paramMin) * (calibRange / paramRange) + calibMin;
 *       = (param - paramMin) * calibFactor + calibMin;
 *
 * -> 1 sub, 1 mul, 1 add
 */
inline double calibrate(
    double param, double paramMin,
    double calibMin, double calibFactor)
{
    if (is_param_valid(param))
    {
        param = (param - paramMin) * calibFactor + calibMin;
    }

    return param;
}

struct CalibrationData
{
    ParamVec calibFactors;
};

void calibration_step(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->inputCount == 1);
    assert(op->outputCount == 1);
    assert(op->inputs[0].size == op->outputs[0].size);
    assert(op->type == Operator_Calibration);

    auto d = reinterpret_cast<CalibrationData *>(op->d);
    s32 maxIdx = op->inputs[0].size;

    for (s32 idx = 0; idx < maxIdx; idx++)
    {
        op->outputs[0][idx] = calibrate(
            op->inputs[0][idx], op->inputLowerLimits[0][idx],
            op->outputLowerLimits[0][idx], d->calibFactors[idx]);

        if (!is_param_valid(op->inputs[0][idx]))
        {
            assert(!is_param_valid(op->outputs[0][idx]));
        }
    }
}

void calibration_sse_step(Operator *op, A2 *)
{
    /* Note: The partially transformed code below is slower than
     * calibration_step(). With the right compiler flags gcc seems to auto
     * SIMD'ize very well.
     * TODO: Finish this implementation using intrinsics and then compare
     * again.
     */
    assert(op->inputCount == 1);
    assert(op->outputCount == 1);
    assert(op->inputs[0].size == op->outputs[0].size);
    assert(op->type == Operator_Calibration_sse);

    auto d = reinterpret_cast<CalibrationData *>(op->d);
    s32 maxIdx = op->inputs[0].size;

    /* Below are attempts at transforming the loop into something that can be
     * handled with SIMD intrinsics. */
#if 0
    assert((op->inputs[0].size % 2) == 0);
    for (s32 idx = 0; idx < maxIdx; idx += 2)
    {
        double p0 = op->inputs[0][idx + 0];
        double p1 = op->inputs[0][idx + 1];
        double min0 = op->inputLowerLimits[0][idx + 0];
        double min1 = op->inputLowerLimits[0][idx + 1];
        double diff0 = p0 - min0;
        double diff1 = p1 - min1;
        double mul0 = diff0 * d->calibFactors[idx + 0];
        double mul1 = diff1 * d->calibFactors[idx + 1];
        double r0 =  mul0 + op->outputLowerLimits[0][idx + 0];
        double r1 =  mul1 + op->outputLowerLimits[0][idx + 1];

        if (is_param_valid(p0))
            op->outputs[0][idx + 0] = r0;

        if (is_param_valid(p1))
            op->outputs[0][idx + 1] = r1;
    }
#else
    assert((op->inputs[0].size % 4) == 0);
    for (s32 idx = 0; idx < maxIdx; idx += 4)
    {
        double p0 = op->inputs[0][idx + 0];
        double p1 = op->inputs[0][idx + 1];
        double p2 = op->inputs[0][idx + 2];
        double p3 = op->inputs[0][idx + 3];

        double min0 = op->inputLowerLimits[0][idx + 0];
        double min1 = op->inputLowerLimits[0][idx + 1];
        double min2 = op->inputLowerLimits[0][idx + 2];
        double min3 = op->inputLowerLimits[0][idx + 3];

        double diff0 = p0 - min0;
        double diff1 = p1 - min1;
        double diff2 = p2 - min2;
        double diff3 = p3 - min3;

        double mul0 = diff0 * d->calibFactors[idx + 0];
        double mul1 = diff1 * d->calibFactors[idx + 1];
        double mul2 = diff2 * d->calibFactors[idx + 2];
        double mul3 = diff3 * d->calibFactors[idx + 3];

        double r0 =  mul0 + op->outputLowerLimits[0][idx + 0];
        double r1 =  mul1 + op->outputLowerLimits[0][idx + 1];
        double r2 =  mul2 + op->outputLowerLimits[0][idx + 2];
        double r3 =  mul3 + op->outputLowerLimits[0][idx + 3];

        op->outputs[0][idx + 0] = is_param_valid(p0) ? r0 : p0;
        op->outputs[0][idx + 1] = is_param_valid(p1) ? r1 : p1;
        op->outputs[0][idx + 2] = is_param_valid(p2) ? r2 : p2;
        op->outputs[0][idx + 3] = is_param_valid(p3) ? r3 : p3;
    }
#endif
}

Operator make_calibration(
    Arena *arena,
    PipeVectors input,
    double unitMin, double unitMax)
{
    assert(input.data.size == input.lowerLimits.size);
    assert(input.data.size == input.upperLimits.size);

    auto result = make_operator(arena, Operator_Calibration, 1, 1);

    assign_input(&result, input, 0);
    push_output_vectors(arena, &result, 0, input.data.size, unitMin, unitMax);

    auto cdata = arena->pushStruct<CalibrationData>();
    cdata->calibFactors = push_param_vector(arena, input.data.size);

    double calibRange = unitMax - unitMin;

    for (s32 i = 0; i < input.data.size; i++)
    {
        double paramRange = input.upperLimits[i] - input.lowerLimits[i];
        cdata->calibFactors[i] = calibRange / paramRange;
    }

    result.d = cdata;

    return result;
}

Operator make_calibration(
    Arena *arena,
    PipeVectors input,
    ParamVec calibMinimums,
    ParamVec calibMaximums)
{
    a2_trace("input.lowerLimits.size=%d, input.data.size=%d\n",
             input.lowerLimits.size, input.data.size);

    a2_trace("calibMinimums.size=%d, input.data.size=%d\n",
             calibMinimums.size, input.data.size);

    assert(input.data.size == input.lowerLimits.size);
    assert(input.data.size == input.upperLimits.size);
    assert(calibMinimums.size == input.data.size);
    assert(calibMaximums.size == input.data.size);

    auto result = make_operator(arena, Operator_Calibration, 1, 1);

    assign_input(&result, input, 0);
    push_output_vectors(arena, &result, 0, input.data.size);

    auto cdata = arena->pushStruct<CalibrationData>();
    cdata->calibFactors = push_param_vector(arena, input.data.size);

    for (s32 i = 0; i < input.data.size; i++)
    {
        double calibRange = calibMaximums[i] - calibMinimums[i];
        double paramRange = input.upperLimits[i] - input.lowerLimits[i];
        cdata->calibFactors[i] = calibRange / paramRange;

        result.outputLowerLimits[0][i] = calibMinimums[i];
        result.outputUpperLimits[0][i] = calibMaximums[i];
    }

    result.d = cdata;

    return result;
}

struct CalibrationData_idx
{
    s32 inputIndex;
    double calibFactor;
};

Operator make_calibration_idx(
    Arena *arena,
    PipeVectors input,
    s32 inputIndex,
    double unitMin,
    double unitMax)
{
    assert(inputIndex < input.data.size);

    auto result = make_operator(arena, Operator_Calibration_idx, 1, 1);

    assign_input(&result, input, 0);

    push_output_vectors(arena, &result, 0, 1, unitMin, unitMax);

    auto d = arena->pushStruct<CalibrationData_idx>();
    result.d = d;

    double calibRange = unitMax - unitMin;
    double paramRange = input.upperLimits[inputIndex] - input.lowerLimits[inputIndex];

    d->inputIndex = inputIndex;
    d->calibFactor = calibRange / paramRange;

    return result;
}

void calibration_step_idx(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->inputCount == 1);
    assert(op->outputCount == 1);
    assert(op->outputs[0].size == 1);
    assert(op->type == Operator_Calibration_idx);

    auto d = reinterpret_cast<CalibrationData_idx *>(op->d);

    assert(d->inputIndex < op->inputs[0].size);

    op->outputs[0][0] = calibrate(
        op->inputs[0][d->inputIndex], op->inputLowerLimits[0][d->inputIndex],
        op->outputLowerLimits[0][0], d->calibFactor);

    if (!is_param_valid(op->inputs[0][d->inputIndex]))
    {
        assert(!is_param_valid(op->outputs[0][0]));
    }
}

struct KeepPreviousData
{
    ParamVec previousInput;
    u8 keepValid;
};

struct KeepPreviousData_idx: public KeepPreviousData
{
    s32 inputIndex;
};

void keep_previous_step(Operator *op, A2 *)
{
    assert(op->inputCount == 1);
    assert(op->outputCount == 1);
    assert(op->inputs[0].size == op->outputs[0].size);
    assert(op->type == Operator_KeepPrevious);

    auto d = reinterpret_cast<KeepPreviousData *>(op->d);

    s32 maxIdx = op->inputs[0].size;

    for (s32 idx = 0; idx < maxIdx; idx++)
    {
        op->outputs[0][idx] = d->previousInput[idx];
    }

    for (s32 idx = 0; idx < maxIdx; idx++)
    {
        double in = op->inputs[0][idx];

        if (!d->keepValid || is_param_valid(in))
        {
            d->previousInput[idx] = in;
        }
    }
}

void keep_previous_step_idx(Operator *op, A2 *)
{
    assert(op->inputCount == 1);
    assert(op->outputCount == 1);
    assert(op->outputs[0].size == 1);
    assert(op->type == Operator_KeepPrevious_idx);

    auto d = reinterpret_cast<KeepPreviousData_idx *>(op->d);

    op->outputs[0][0] = d->previousInput[0];

    double in = op->inputs[0][d->inputIndex];

    if (!d->keepValid || is_param_valid(in))
    {
        d->previousInput[0] = in;
    }
}

Operator make_keep_previous(
    Arena *arena, PipeVectors inPipe, bool keepValid)
{
    auto result = make_operator(arena, Operator_KeepPrevious, 1, 1);

    auto d = arena->pushStruct<KeepPreviousData>();
    d->previousInput = push_param_vector(arena, inPipe.data.size, invalid_param());
    d->keepValid = keepValid;
    result.d = d;

    assign_input(&result, inPipe, 0);
    push_output_vectors(arena, &result, 0, inPipe.data.size);

    return result;
}

Operator make_keep_previous_idx(
    memory::Arena *arena, PipeVectors inPipe,
    s32 inputIndex, bool keepValid)
{
    auto result = make_operator(arena, Operator_KeepPrevious_idx, 1, 1);

    auto d = arena->pushStruct<KeepPreviousData_idx>();
    d->previousInput = push_param_vector(arena, 1, invalid_param());
    d->keepValid = keepValid;
    d->inputIndex = inputIndex;
    result.d = d;

    assign_input(&result, inPipe, 0);
    push_output_vectors(arena, &result, 0, 1);

    return result;
}

Operator make_difference(
    Arena *arena,
    PipeVectors inPipeA,
    PipeVectors inPipeB)
{
    assert(inPipeA.data.size == inPipeB.data.size);

    auto result = make_operator(arena, Operator_Difference, 2, 1);

    assign_input(&result, inPipeA, 0);
    assign_input(&result, inPipeB, 1);

    push_output_vectors(arena, &result, 0, inPipeA.data.size);

    for (s32 idx = 0; idx < inPipeA.data.size; idx++)
    {
        result.outputLowerLimits[0][idx] = inPipeA.lowerLimits[idx] - inPipeB.upperLimits[idx];
        result.outputUpperLimits[0][idx] = inPipeA.upperLimits[idx] - inPipeB.lowerLimits[idx];
    }

    return result;
}

struct DifferenceData_idx
{
    s32 indexA;
    s32 indexB;
};

Operator make_difference_idx(
    Arena *arena,
    PipeVectors inPipeA,
    PipeVectors inPipeB,
    s32 indexA,
    s32 indexB)
{
    assert(indexA < inPipeA.data.size);
    assert(indexB < inPipeB.data.size);

    auto result = make_operator(arena, Operator_Difference_idx, 2, 1);

    result.d = arena->push<DifferenceData_idx>({indexA, indexB});

    assign_input(&result, inPipeA, 0);
    assign_input(&result, inPipeB, 1);

    push_output_vectors(arena, &result, 0, 1);

    result.outputLowerLimits[0][0] = inPipeA.lowerLimits[indexA] - inPipeB.upperLimits[indexB];
    result.outputUpperLimits[0][0] = inPipeA.upperLimits[indexA] - inPipeB.lowerLimits[indexB];

    return result;
}

void difference_step(Operator *op, A2 *)
{
    assert(op->inputCount == 2);
    assert(op->outputCount == 1);
    assert(op->inputs[0].size == op->outputs[0].size);
    assert(op->inputs[1].size == op->outputs[0].size);
    assert(op->type == Operator_Difference);


    auto inputA = op->inputs[0];
    auto inputB = op->inputs[1];
    auto maxIdx = inputA.size;

    for (auto idx = 0; idx < maxIdx; idx++)
    {
        if (is_param_valid(inputA[idx]) && is_param_valid(inputB[idx]))
        {
            op->outputs[0][idx] = inputA[idx] - inputB[idx];
        }
        else
        {
            op->outputs[0][idx] = invalid_param();
        }
    }
}

void difference_step_idx(Operator *op, A2 *)
{
    assert(op->inputCount == 2);
    assert(op->outputCount == 1);
    assert(op->type == Operator_Difference_idx);

    auto inputA = op->inputs[0];
    auto inputB = op->inputs[1];

    auto d = reinterpret_cast<DifferenceData_idx *>(op->d);

    if (is_param_valid(inputA[d->indexA]) && is_param_valid(inputB[d->indexB]))
    {
        op->outputs[0][0] = inputA[d->indexA] - inputB[d->indexB];
    }
    else
    {
        op->outputs[0][0] = invalid_param();
    }
}

/**
 * ArrayMap: Map elements of one or more input arrays to an output array.
 *
 * Can be used to concatenate multiple arrays and/or change the order of array
 * members.
 */

void array_map_step(Operator *op, A2 *)
{
    auto d = reinterpret_cast<ArrayMapData *>(op->d);

    s32 mappingCount = d->mappings.size;

    for (s32 mi = 0; mi < mappingCount; mi++)
    {
        auto mapping = d->mappings[mi];

        if (mapping.inputIndex < op->inputCount
            && 0 <= mapping.paramIndex
            && mapping.paramIndex < op->inputs[mapping.inputIndex].size)
        {
            op->outputs[0][mi] = op->inputs[mapping.inputIndex][mapping.paramIndex];
        }
        else
        {
            op->outputs[0][mi] = invalid_param();
        }
    }
}

/* Note: mappings are deep copied, inputs are assigned. */
Operator make_array_map(
    Arena *arena,
    TypedBlock<PipeVectors, s32> inputs,
    TypedBlock<ArrayMapData::Mapping, s32> mappings)
{
    auto result = make_operator(arena, Operator_ArrayMap, inputs.size, 1);

    for (s32 ii = 0; ii < inputs.size; ii++)
    {
        assign_input(&result, inputs[ii], ii);
    }

    auto d = arena->pushStruct<ArrayMapData>();
    d->mappings = push_typed_block<ArrayMapData::Mapping, s32>(arena, mappings.size);

    push_output_vectors(arena, &result, 0, mappings.size);

    for (s32 mi = 0; mi < mappings.size; mi++)
    {
        auto m = d->mappings[mi] = mappings[mi];

        double ll = make_quiet_nan();
        double ul = make_quiet_nan();

        if (m.inputIndex < inputs.size
            && 0 <= m.paramIndex
            && m.paramIndex < inputs[m.inputIndex].lowerLimits.size)
        {
            ll = inputs[m.inputIndex].lowerLimits[m.paramIndex];
            ul = inputs[m.inputIndex].upperLimits[m.paramIndex];
        }

        result.outputLowerLimits[0][mi] = ll;
        result.outputUpperLimits[0][mi] = ul;
    }

    result.d = d;

    return result;
}

using BinaryEquationFunction = void (*)(ParamVec a, ParamVec b, ParamVec out);

#define add_binary_equation(x) \
[](ParamVec a, ParamVec b, ParamVec o)\
{\
    for (s32 i = 0; i < a.size && i < b.size; ++i)\
    {\
        if (is_param_valid(a[i]) && is_param_valid(b[i])) \
        {\
            x;\
        }\
        else\
        {\
            o[i] = invalid_param();\
        }\
    }\
}

static BinaryEquationFunction BinaryEquationTable[] =
{
    add_binary_equation(o[i] = a[i] + b[i]),

    add_binary_equation(o[i] = a[i] - b[i]),

    add_binary_equation(o[i] = (a[i] + b[i]) / (a[i] - b[i])),

    add_binary_equation(o[i] = (a[i] - b[i]) / (a[i] + b[i])),

    add_binary_equation(o[i] = a[i] / (a[i] - b[i])),

    add_binary_equation(o[i] = (a[i] - b[i]) / a[i]),

    add_binary_equation(o[i] = (a[i] * b[i])),

    add_binary_equation(o[i] = (a[i] / b[i])),
};
#undef add_binary_equation

static const size_t BinaryEquationCount = ArrayCount(BinaryEquationTable);

using BinaryEquationFunction_idx = void (*)(ParamVec a, s32 ai, ParamVec b, s32 bi, ParamVec out);

#define add_binary_equation_idx(x) \
[](ParamVec a, s32 ai, ParamVec b, s32 bi, ParamVec o)\
{\
    if (is_param_valid(a[ai]) && is_param_valid(b[bi])) \
    {\
        x;\
    }\
    else\
    {\
        o[0] = invalid_param();\
    }\
}

static BinaryEquationFunction_idx BinaryEquationTable_idx[] =
{
    add_binary_equation_idx(o[0] = a[ai] + b[bi]),

    add_binary_equation_idx(o[0] = a[ai] - b[bi]),

    add_binary_equation_idx(o[0] = (a[ai] + b[bi]) / (a[ai] - b[bi])),

    add_binary_equation_idx(o[0] = (a[ai] - b[bi]) / (a[ai] + b[bi])),

    add_binary_equation_idx(o[0] = a[ai] / (a[ai] - b[bi])),

    add_binary_equation_idx(o[0] = (a[ai] - b[bi]) / a[ai]),

    add_binary_equation_idx(o[0] = (a[ai] * b[bi])),

    add_binary_equation_idx(o[0] = (a[ai] / b[bi])),
};
#undef add_binary_equation_idx

static const size_t BinaryEquationCount_idx = ArrayCount(BinaryEquationTable_idx);

static_assert(BinaryEquationCount == BinaryEquationCount_idx, "Expected same number of equations for non-index and index cases.");

void binary_equation_step(Operator *op, A2 *)
{
    // The equationIndex is stored directly in the d pointer.
    u32 equationIndex = (uintptr_t)op->d;

    BinaryEquationTable[equationIndex](
        op->inputs[0], op->inputs[1], op->outputs[0]);
}

Operator make_binary_equation(
    Arena *arena,
    PipeVectors inputA,
    PipeVectors inputB,
    u32 equationIndex,
    double outputLowerLimit,
    double outputUpperLimit)
{
    assert(equationIndex < ArrayCount(BinaryEquationTable));

    auto result = make_operator(arena, Operator_BinaryEquation, 2, 1);

    assign_input(&result, inputA, 0);
    assign_input(&result, inputB, 1);

    push_output_vectors(arena, &result, 0, std::min(inputA.data.size, inputB.data.size),
                        outputLowerLimit, outputUpperLimit);

    result.d = (void *)(uintptr_t)equationIndex;

    return result;
}

struct BinaryEquationIdxData
{
    u32 equationIndex;
    s32 inputIndexA;
    s32 inputIndexB;
};

Operator make_binary_equation_idx(
    memory::Arena *arena,
    PipeVectors inputA,
    PipeVectors inputB,
    s32 inputIndexA,
    s32 inputIndexB,
    u32 equationIndex,
    double outputLowerLimit,
    double outputUpperLimit)
{
    assert(equationIndex < ArrayCount(BinaryEquationTable));
    assert(0 <= inputIndexA && inputIndexA < inputA.data.size);
    assert(0 <= inputIndexB && inputIndexB < inputB.data.size);

    auto result = make_operator(arena, Operator_BinaryEquation_idx, 2, 1);
    assign_input(&result, inputA, 0);
    assign_input(&result, inputB, 1);

    auto d = arena->pushStruct<BinaryEquationIdxData>();
    result.d = d;

    d->equationIndex = equationIndex;
    d->inputIndexA   = inputIndexA;
    d->inputIndexB   = inputIndexB;

    push_output_vectors(arena, &result, 0, 1,
                        outputLowerLimit, outputUpperLimit);

    return result;
}

void binary_equation_step_idx(Operator *op, A2 *)
{
    auto d = reinterpret_cast<BinaryEquationIdxData *>(op->d);

    BinaryEquationTable_idx[d->equationIndex](
        op->inputs[0], d->inputIndexA,
        op->inputs[1], d->inputIndexB,
        op->outputs[0]);
}

/* ===============================================
 * AggregateOps
 * =============================================== */
inline bool is_valid_and_inside(double param, Thresholds thresholds)
{
    return (is_param_valid(param)
            && thresholds.min <= param
            && thresholds.max >= param);
}

static Operator make_aggregate_op(
    Arena *arena,
    PipeVectors input,
    u8 operatorType,
    Thresholds thresholds)
{
    auto result = make_operator(arena, operatorType, 1, 1);

    a2_trace("input thresholds: %lf, %lf\n", thresholds.min, thresholds.max);

    /* The min and max values must be set to the inputs lowest/highest limits if no
     * threshold filtering is wanted. This way a isnan() test can be saved. */
    if (std::isnan(thresholds.min))
    {
        thresholds.min = *std::min_element(std::begin(input.lowerLimits), std::end(input.lowerLimits));
    }

    if (std::isnan(thresholds.max))
    {
        thresholds.max = *std::max_element(std::begin(input.upperLimits), std::end(input.upperLimits));
    }

    a2_trace("resulting thresholds: %lf, %lf\n", thresholds.min, thresholds.max);

    auto d = arena->push(thresholds);
    result.d = d;
    *d = thresholds;

    assign_input(&result, input, 0);

    /* Note: output lower/upper limits are not set here. That's left to the
     * specific operatorType make_aggregate_X() implementation. */
    push_output_vectors(arena, &result, 0, 1);

    return result;
}

//
// aggregate_sum
//
Operator make_aggregate_sum(
    Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);

    auto result = make_aggregate_op(arena, input, Operator_Aggregate_Sum, thresholds);

    double outputLowerLimit = 0.0;
    double outputUpperLimit = 0.0;

    for (s32 i = 0; i < input.data.size; i++)
    {
        outputLowerLimit += std::min(input.lowerLimits[i], input.upperLimits[i]);
        outputUpperLimit += std::max(input.lowerLimits[i], input.upperLimits[i]);
    }

    result.outputLowerLimits[0][0] = outputLowerLimit;
    result.outputUpperLimits[0][0] = outputUpperLimit;

    return result;
}

void aggregate_sum_step(Operator *op, A2 *)
{
    a2_trace("\n");
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    double theSum = 0.0;
    bool validSeen = false;

    for (s32 i = 0; i < input.size; i++)
    {
        //a2_trace("i=%d, input[i]=%lf, thresholds.min=%lf, thresholds.max=%lf, is_valid_and_inside()=%d\n",
        //         i, input[i], thresholds.min, thresholds.max, is_valid_and_inside(input[i], thresholds));

        if (is_valid_and_inside(input[i], thresholds))
        {
            theSum += input[i];
            validSeen = true;
        }
    }

    output[0] = validSeen ? theSum : invalid_param();
}

//
// aggregate_multiplicity
//
Operator make_aggregate_multiplicity(
    Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);
    auto result = make_aggregate_op(arena, input, Operator_Aggregate_Multiplicity, thresholds);

    result.outputLowerLimits[0][0] = 0.0;
    result.outputUpperLimits[0][0] = input.data.size;

    return result;
}

void aggregate_multiplicity_step(Operator *op, A2 *)
{
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    output[0] = 0.0;

    for (s32 i = 0; i < input.size; i++)
    {
        if (is_valid_and_inside(input[i], thresholds))
        {
            output[0]++;
        }
    }
}

//
// aggregate_min
//
Operator make_aggregate_min(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);

    auto result = make_aggregate_op(arena, input, Operator_Aggregate_Min, thresholds);

    double llMin = std::min(
        *std::min_element(std::begin(input.lowerLimits), std::end(input.lowerLimits)),
        *std::min_element(std::begin(input.upperLimits), std::end(input.upperLimits)));

    double llMax = std::max(
        *std::max_element(std::begin(input.lowerLimits), std::end(input.lowerLimits)),
        *std::max_element(std::begin(input.upperLimits), std::end(input.upperLimits)));

    result.outputLowerLimits[0][0] = llMin;
    result.outputUpperLimits[0][0] = llMax;

    return result;
}

void aggregate_min_step(Operator *op, A2 *)
{
    a2_trace("\n");
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    double result = invalid_param();

    for (s32 i = 0; i < input.size; i++)
    {
        if (is_valid_and_inside(input[i], thresholds))
        {
            if (!is_param_valid(result))
            {
                result = std::numeric_limits<double>::max();
            }

            result = std::min(result, input[i]);
        }
    }

    output[0] = result;
}

//
// aggregate_max
//
Operator make_aggregate_max(
    Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);
    auto result = make_aggregate_op(arena, input, Operator_Aggregate_Max, thresholds);

    double llMin = std::min(
        *std::min_element(std::begin(input.lowerLimits), std::end(input.lowerLimits)),
        *std::min_element(std::begin(input.upperLimits), std::end(input.upperLimits)));

    double llMax = std::max(
        *std::max_element(std::begin(input.lowerLimits), std::end(input.lowerLimits)),
        *std::max_element(std::begin(input.upperLimits), std::end(input.upperLimits)));

    result.outputLowerLimits[0][0] = llMin;
    result.outputUpperLimits[0][0] = llMax;

    return result;
}

void aggregate_max_step(Operator *op, A2 *)
{
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    double result = invalid_param();

    for (s32 i = 0; i < input.size; i++)
    {
        if (is_valid_and_inside(input[i], thresholds))
        {
            if (!is_param_valid(result))
            {
                result = std::numeric_limits<double>::lowest();
            }

            result = std::max(result, input[i]);
        }
    }

    output[0] = result;
}

//
// aggregate_mean
//

struct SumAndValidCount
{
    double sum;
    u32 validCount;

    inline double mean()
    {
        return sum / static_cast<double>(validCount);
    }
};

inline SumAndValidCount calculate_sum_and_valid_count(ParamVec input, Thresholds thresholds)
{
    SumAndValidCount result = {};

    for (s32 i = 0; i < input.size; i++)
    {
        if (is_valid_and_inside(input[i], thresholds))
        {
            result.sum += input[i];
            result.validCount++;
        }
    }

    return result;
}

// mean = (sum(x for x in input) / validCount)
Operator make_aggregate_mean(
    Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);
    auto result = make_aggregate_op(arena, input, Operator_Aggregate_Mean, thresholds);

    double outputLowerLimit = 0.0;
    double outputUpperLimit = 0.0;

    for (s32 i = 0; i < input.data.size; i++)
    {
        auto mm = std::minmax(input.lowerLimits[i], input.upperLimits[i]);

        outputLowerLimit += mm.first;
        outputUpperLimit += mm.second;
    }

    outputLowerLimit /= input.data.size;
    outputUpperLimit /= input.data.size;

    result.outputLowerLimits[0][0] = outputLowerLimit;
    result.outputUpperLimits[0][0] = outputUpperLimit;

    return result;
}

void aggregate_mean_step(Operator *op, A2 *)
{
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    auto sv = calculate_sum_and_valid_count(input, thresholds);

    output[0] = sv.validCount ? sv.mean() : invalid_param();
}

//
// aggregate_sigma
//
Operator make_aggregate_sigma(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);

    auto result = make_aggregate_op(arena, input, Operator_Aggregate_Sigma, thresholds);

    double llMin = std::numeric_limits<double>::max();
    double ulMax = std::numeric_limits<double>::lowest();

    for (s32 i = 0; i < input.data.size; i++)
    {
        llMin = std::min(llMin, std::min(input.lowerLimits[i], input.upperLimits[i]));
        ulMax = std::max(ulMax, std::max(input.lowerLimits[i], input.upperLimits[i]));
    }

    result.outputLowerLimits[0][0] = 0.0;
    result.outputUpperLimits[0][0] = std::sqrt(ulMax - llMin);

    return result;
}

void aggregate_sigma_step(Operator *op, A2 *)
{
    a2_trace("\n");
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    auto sv = calculate_sum_and_valid_count(input, thresholds);
    double mean = sv.mean();
    double sigma = 0.0;

    for (s32 i = 0; i < input.size; i++)
    {
        if (is_valid_and_inside(input[i], thresholds))
        {
            double d = input[i] - mean;
            sigma += d * d;
        }
    }

    if (sv.validCount)
    {
        sigma = std::sqrt(sigma / static_cast<double>(sv.validCount));
        output[0] = sigma;
    }
    else
    {
        output[0] = invalid_param();
    }
}

//
// aggregate_minx
//
Operator make_aggregate_minx(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);

    auto result = make_aggregate_op(arena, input, Operator_Aggregate_MinX, thresholds);

    result.outputLowerLimits[0][0] = 0.0;
    result.outputUpperLimits[0][0] = input.data.size;

    return result;
}

void aggregate_minx_step(Operator *op, A2 *)
{
    a2_trace("\n");
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    output[0] = invalid_param();
    s32 minIndex = 0;

    for (s32 i = 0; i < input.size; i++)
    {
        if (is_valid_and_inside(input[i], thresholds))
        {
            if (input[i] < input[minIndex] || std::isnan(input[minIndex]))
            {
                minIndex = i;
            }
        }
    }

    if (is_valid_and_inside(input[minIndex], thresholds))
        output[0] = minIndex;
}

//
// aggregate_maxx
//
Operator make_aggregate_maxx(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);

    auto result = make_aggregate_op(arena, input, Operator_Aggregate_MaxX, thresholds);

    result.outputLowerLimits[0][0] = 0.0;
    result.outputUpperLimits[0][0] = input.data.size;

    return result;
}

void aggregate_maxx_step(Operator *op, A2 *)
{
    a2_trace("\n");
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    output[0] = invalid_param();
    s32 maxIndex = 0;

    for (s32 i = 0; i < input.size; i++)
    {
        if (is_valid_and_inside(input[i], thresholds))
        {
            if (input[i] > input[maxIndex] || std::isnan(input[maxIndex]))
            {
                maxIndex = i;
            }
        }
    }

    if (is_valid_and_inside(input[maxIndex], thresholds))
    {
        output[0] = maxIndex;
    }
}

//
// aggregate_meanx
//
Operator make_aggregate_meanx(
    Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);
    auto result = make_aggregate_op(arena, input, Operator_Aggregate_MeanX, thresholds);

    result.outputLowerLimits[0][0] = 0.0;
    result.outputUpperLimits[0][0] = input.data.size;

    return result;
}

/*
 * meanx = sum(A * x) / sum(A)
 * meanx = sum(input[i] * i) / sum(input[i])
 */
struct MeanXResult
{
    double meanx;
    double sumx;
};

inline MeanXResult calculate_meanx(ParamVec input, Thresholds thresholds)
{
    MeanXResult result = {};

    double numerator   = 0.0;
    double denominator = 0.0;
    bool validSeen = false;

    for (s32 x = 0; x < input.size; x++)
    {
        double A = input[x];

        if (is_valid_and_inside(A, thresholds))
        {
            numerator += A * x;
            denominator += A;
            validSeen = true;
        }
    }

    if (validSeen)
    {
        result.meanx = numerator / denominator;
        result.sumx  = denominator;
    }
    else
    {
        result.meanx = result.sumx = invalid_param();
    }

    return result;
}

void aggregate_meanx_step(Operator *op, A2 *)
{
    a2_trace("\n");
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    output[0] = calculate_meanx(input, thresholds).meanx;
}

//
// aggregate_sigmax
//
Operator make_aggregate_sigmax(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds)
{
    a2_trace("thresholds: %lf, %lf\n", thresholds.min, thresholds.max);

    auto result = make_aggregate_op(arena, input, Operator_Aggregate_SigmaX, thresholds);

    result.outputLowerLimits[0][0] = 0.0;
    result.outputUpperLimits[0][0] = input.data.size;

    return result;
}

void aggregate_sigmax_step(Operator *op, A2 *)
{
    a2_trace("\n");
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto thresholds = *reinterpret_cast<Thresholds *>(op->d);

    assert(input.size);
    assert(output.size);
    assert(!std::isnan(thresholds.min));
    assert(!std::isnan(thresholds.max));

    double sigmax = invalid_param();
    auto meanxResult = calculate_meanx(input, thresholds);

    if (is_param_valid(meanxResult.meanx))
    {
        sigmax = 0.0;

        for (s32 x = 0; x < input.size; x++)
        {
            double A = input[x];

            if (is_valid_and_inside(A, thresholds))
            {
                double d = x - meanxResult.meanx;
                d *= d;
                sigmax += d * A;
            }
        }

        sigmax = std::sqrt(sigmax / meanxResult.sumx);
    }

    output[0] = sigmax;
}

//
// range_filter
//
struct RangeFilterData
{
    Thresholds thresholds;
    bool invert;
};


struct RangeFilterData_idx
{
    Thresholds thresholds;
    bool invert;
    s32 inputIndex;
};

Operator make_range_filter(
    Arena *arena,
    PipeVectors input,
    Thresholds thresholds,
    bool invert)
{
    auto result = make_operator(arena, Operator_RangeFilter, 1, 1);

    auto d = arena->push<RangeFilterData>({ thresholds, invert });
    result.d = d;

    assign_input(&result, input, 0);

    push_output_vectors(arena, &result, 0, input.data.size);


    for (s32 pi = 0; pi < input.data.size; pi++)
    {
        if (invert)
        {
            result.outputLowerLimits[0][pi] = input.lowerLimits[pi];
            result.outputUpperLimits[0][pi] = input.upperLimits[pi];
        }
        else
        {
            result.outputLowerLimits[0][pi] = thresholds.min;
            result.outputUpperLimits[0][pi] = thresholds.max;
        }
    }

    return result;
}

Operator make_range_filter_idx(
    Arena *arena,
    PipeVectors input,
    s32 inputIndex,
    Thresholds thresholds,
    bool invert)
{
    assert(0 <= inputIndex && inputIndex < input.data.size);

    auto result = make_operator(arena, Operator_RangeFilter_idx, 1, 1);

    auto d = arena->push<RangeFilterData_idx>({ thresholds, invert, inputIndex });
    result.d = d;

    assign_input(&result, input, 0);

    push_output_vectors(arena, &result, 0, 1);

    if (invert)
    {
        result.outputLowerLimits[0][0] = input.lowerLimits[inputIndex];
        result.outputUpperLimits[0][0] = input.upperLimits[inputIndex];
    }
    else
    {
        result.outputLowerLimits[0][0] = thresholds.min;
        result.outputUpperLimits[0][0] = thresholds.max;
    }

    return result;
}

void range_filter_step(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->inputCount == 1);
    assert(op->outputCount == 1);
    assert(op->inputs[0].size == op->outputs[0].size);
    assert(op->type == Operator_RangeFilter);

    const double invalid_p = invalid_param();
    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto data = *reinterpret_cast<RangeFilterData *>(op->d);

    if (data.invert)
    {
        for (s32 pi = 0; pi < input.size; pi++)
        {
            if (!in_range(data.thresholds, input[pi]))
            {
                output[pi] = input[pi];
            }
            else
            {
                output[pi] = invalid_p;
            }
        }
    }
    else
    {
        for (s32 pi = 0; pi < input.size; pi++)
        {
            if (in_range(data.thresholds, input[pi]))
            {
                output[pi] = input[pi];
            }
            else
            {
                output[pi] = invalid_p;
            }
        }
    }
}

void range_filter_step_idx(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->inputCount == 1);
    assert(op->outputCount == 1);
    assert(op->outputs[0].size == 1);
    assert(op->type == Operator_RangeFilter_idx);

    auto input = op->inputs[0];
    auto output = op->outputs[0];
    auto data = *reinterpret_cast<RangeFilterData_idx *>(op->d);

    if (data.invert)
    {
        if (!in_range(data.thresholds, input[data.inputIndex]))
        {
            output[0] = input[data.inputIndex];
        }
        else
        {
            output[0] = invalid_param();
        }
    }
    else
    {
        if (in_range(data.thresholds, input[data.inputIndex]))
        {
            output[0] = input[data.inputIndex];
        }
        else
        {
            output[0] = invalid_param();
        }
    }
}

//
// RectFilter
//

struct RectFilterData
{
    Thresholds xThresholds;
    Thresholds yThresholds;
    s32 xIndex;
    s32 yIndex;
    RectFilterOperation filterOp;
};

Operator make_rect_filter(
    memory::Arena *arena,
    PipeVectors xInput,
    PipeVectors yInput,
    s32 xIndex,
    s32 yIndex,
    Thresholds xThresholds,
    Thresholds yThresholds,
    RectFilterOperation filterOp)
{
    assert(0 <= xIndex && xIndex < xInput.data.size);
    assert(0 <= yIndex && yIndex < yInput.data.size);

    auto result = make_operator(arena, Operator_RectFilter, 2, 1);

    auto d = arena->push<RectFilterData>({ xThresholds, yThresholds, xIndex, yIndex, filterOp });
    result.d = d;

    assign_input(&result, xInput, 0);
    assign_input(&result, yInput, 1);

    push_output_vectors(arena, &result, 0, 1);

    return result;
}

void rect_filter_step(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->inputCount == 2);
    assert(op->outputCount == 1);
    assert(op->type == Operator_RectFilter);

    auto xInput = op->inputs[0];
    auto yInput = op->inputs[1];
    auto output = op->outputs[0];
    auto d = reinterpret_cast<RectFilterData *>(op->d);

    double x = xInput[d->xIndex];
    double y = yInput[d->yIndex];

    bool xInside = in_range(d->xThresholds, x);
    bool yInside = in_range(d->yThresholds, y);

    bool valid = (d->filterOp == RectFilterOperation::And
                  ? (xInside && yInside)
                  : (xInside || yInside));

    output[0] = valid ? 0.0 : invalid_param();
}

//
// ConditionFilter
//

struct ConditionFilterData
{
    s32 dataIndex;
    s32 condIndex;
    bool inverted;
};

Operator make_condition_filter(
    memory::Arena *arena,
    PipeVectors dataInput,
    PipeVectors condInput,
    bool inverted,
    s32 dataIndex,
    s32 condIndex)
{
    assert(dataIndex < 0 || dataIndex < dataInput.data.size);
    assert(condIndex < 0 || condIndex < condInput.data.size);

    if (dataIndex >= 0 && condIndex < 0)
    {
        /* Data is a single element, condition an array. Multiple things could
         * be done:
         * 1) Use the dataIndex to index into the condition array if the
         *    condition array is big enough. Otherwise error out.
         * 2) Use the first parameter of the condition array.
         * 3) Error out.
         * This code implements the second version.  */
        assert(condInput.data.size >= 1);
        condIndex = 0;
    }

    auto result = make_operator(arena, Operator_ConditionFilter, 2, 1);

    auto d = arena->push<ConditionFilterData>({ dataIndex, condIndex, inverted });
    result.d = d;

    assign_input(&result, dataInput, 0);
    assign_input(&result, condInput, 1);

    // either the whole input or the selected element
    s32 outSize = (dataIndex < 0 ? dataInput.data.size : 1);

    push_output_vectors(arena, &result, 0, outSize);

    if (dataIndex < 0)
    {
        for (s32 i = 0; i < outSize; i++)
        {
            result.outputLowerLimits[0][i] = dataInput.lowerLimits[i];
            result.outputUpperLimits[0][i] = dataInput.upperLimits[i];
        }
    }
    else
    {
        result.outputLowerLimits[0][0] = dataInput.lowerLimits[dataIndex];
        result.outputUpperLimits[0][0] = dataInput.upperLimits[dataIndex];
    }

    return result;
}

void condition_filter_step(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->inputCount == 2);
    assert(op->outputCount == 1);
    assert(op->type == Operator_ConditionFilter);

    auto dataInput = op->inputs[0];
    auto condInput = op->inputs[1];
    auto output = op->outputs[0];
    auto d = reinterpret_cast<ConditionFilterData *>(op->d);

    if (d->dataIndex < 0)
    {
        // data input is an array
        assert(output.size == dataInput.size);

        for (s32 pi = 0; pi < dataInput.size; pi++)
        {
            /* The index into the condition array can be out of range if the
             * condition array is smaller than the data array. In that case an
             * invalid_param() is used. */
            double condParam = invalid_param();

            if (d->condIndex < 0 && pi < condInput.size)
            {
                condParam = condInput[pi];
            }
            else if (d->condIndex >= 0)
            {
                assert(d->condIndex < condInput.size);
                condParam = condInput[d->condIndex];
            }

            bool condValid = is_param_valid(condParam);

            if (condValid && !d->inverted)
            {
                output[pi] = dataInput[pi];
            }
            else if (!condValid && d->inverted)
            {
                output[pi] = dataInput[pi];
            }
            else
            {
                output[pi] = invalid_param();
            }
        }
    }
    else
    {
        /* Data input is a single value. Condition can be a single value or an
         * array. If it was an array d->condIndex will have been set to 0 in
         * make_condition_filter(). */
        assert(d->dataIndex < dataInput.size);
        assert(d->condIndex < condInput.size);
        assert(output.size == 1);

        double condParam = condInput[d->condIndex];
        bool condValid   = is_param_valid(condParam);

        if (condValid && !d->inverted)
        {
            output[0] = dataInput[d->dataIndex];
        }
        else if (!condValid && d->inverted)
        {
            output[0] = dataInput[d->dataIndex];
        }
        else
        {
            output[0] = invalid_param();
        }
    }
}

//
// ExpressionOperator
//

a2_exprtk::SymbolTable make_expression_operator_runtime_library()
{
    a2_exprtk::SymbolTable result;

    /* Note: the conversion from lambda to function pointer works because the
     * lambdas are non-capturing. */

    result.addFunction(
        "is_valid", [](double p) { return static_cast<double>(is_param_valid(p)); });

    result.addFunction(
        "is_invalid", [](double p) { return static_cast<double>(!is_param_valid(p)); });

    result.addFunction(
        "make_invalid", invalid_param);

    result.addFunction(
        "is_nan", [](double d) { return static_cast<double>(std::isnan(d)); });

    result.addFunction(
        "valid_or", [](double p, double def_value) {
            return is_param_valid(p) ? p : def_value;
    });

    return result;
}

#define register_symbol(table, meth, sym, ...) table.meth(sym, ##__VA_ARGS__)

namespace
{
struct OutputSpec
{
    std::string name;
    std::string unit;
    std::vector<double> lowerLimits;
    std::vector<double> upperLimits;
};

using Result = a2_exprtk::Expression::Result;
using SemanticError = ExpressionOperatorSemanticError;


OutputSpec build_output_spec(size_t out_idx, size_t result_idx,
                             const Result &res_name,
                             const Result &res_unit,
                             const Result &res_size,
                             const Result &res_ll,
                             const Result &res_ul)

{
    OutputSpec result = {};
    std::ostringstream ss;

#define expect_result_type(res, expected_type)\
do\
    if (res.type != expected_type)\
    {\
        std::ostringstream ss;\
        ss << "Unexpected result type: result #" << result_idx\
        << ", output #" << out_idx <<  ": expected type is " << #expected_type;\
        throw SemanticError(ss.str());\
    }\
while(0)

    expect_result_type(res_name, Result::String);
    expect_result_type(res_unit, Result::String);
    expect_result_type(res_size, Result::Scalar);

#undef expect_result_type

    result.name = res_name.string;
    result.unit = res_unit.string;

    s32 outputSize = std::lround(res_size.scalar);

    if (outputSize <= 0)
    {
        ss << "output#" << out_idx << ", name=" << result.name
            << ": Invalid output size returned (" << outputSize << ")";
        throw SemanticError(ss.str());
    }

    if (res_ll.type == Result::Scalar && res_ul.type == Result::Scalar)
    {
        result.lowerLimits.resize(outputSize);
        std::fill(result.lowerLimits.begin(), result.lowerLimits.end(), res_ll.scalar);

        result.upperLimits.resize(outputSize);
        std::fill(result.upperLimits.begin(), result.upperLimits.end(), res_ul.scalar);
    }
    else if (res_ll.type == Result::Vector && res_ul.type == Result::Vector)
    {
        if(res_ll.vector.size() != res_ul.vector.size())
        {
            ss << "output#" << out_idx << ", name=" << result.name
               << ": Different sizes of limit specifications"
                << ": lower_limits[]: " << res_ll.vector.size()
                << ", upper_limits[]: " << res_ul.vector.size();
            throw SemanticError(ss.str());
        }

        if (res_ll.vector.size() != static_cast<size_t>(outputSize))
        {
            ss << "output#" << out_idx << ", name=" << result.name
                << ": Output size and size of limit arrays differ!"
                << " output size =" << outputSize
                << ", limits size =" << res_ll.vector.size();
            throw SemanticError(ss.str());
        }

        result.lowerLimits = res_ll.vector;
        result.upperLimits = res_ul.vector;
    }
    else
    {
        ss << "output#" << out_idx << ", name=" << result.name
            << ": Limit definitions must either both be scalars or both be arrays.";
        throw SemanticError(ss.str());
    }

    return result;
}

template<typename M, typename K>
bool map_contains(const M &theMap, const K &theKey)
{
    return theMap.find(theKey) != std::end(theMap);
}

struct FuncMakeStatic: public a2_exprtk::GenericFunction
{
    using VarDef = a2_exprtk::TypeStore;

    FuncMakeStatic()
        : GenericFunction("ST|SV|STT")
    {}

    double operator()(const ParameterList &params, size_t paramSeqIndex) override
    {
        std::string varname = params[0].string;

        if (map_contains(vardefs, varname))
            throw SemanticError("Duplicate static variable definition");

        if (paramSeqIndex == 0 || paramSeqIndex == 1)
        {
            vardefs[varname] = params[1];
        }
        else
        {
            VarDef vardef = {};
            vardef.type = VarDef::Vector;
            vardef.vector.resize(params[1].scalar);
            std::fill(std::begin(vardef.vector), std::end(vardef.vector), params[2].scalar);
            vardefs[varname] = vardef;
        }


        return {};
    }

    std::map<std::string, VarDef> vardefs;
};

} // end anon namspace

Operator make_expression_operator(
    memory::Arena *arena,
    const std::vector<PipeVectors> &inputs,
    const std::vector<s32> &input_param_indexes,
    const std::vector<std::string> &input_prefixes,
    const std::vector<std::string> &input_units,
    const std::string &expr_begin_str,
    const std::string &expr_step_str,
    ExpressionOperatorBuildOptions options)
{
    assert(inputs.size() > 0);
    assert(inputs.size() < static_cast<size_t>(std::numeric_limits<s32>::max()));
    assert(inputs.size() == input_prefixes.size());
    assert(inputs.size() == input_units.size());

    auto d = arena->pushObject<ExpressionOperatorData>();

    /* Fill the begin expression symbol table with unit and limit information. */
    for (size_t i = 0; i < inputs.size(); i++)
    {
        const auto &input  = inputs[i];
        const auto &prefix = input_prefixes[i];
        const auto &unit   = input_units[i];
        const auto &pi     = input_param_indexes[i];

        register_symbol(d->symtab_begin, createString, prefix + ".unit",
                        unit);

        if (pi == NoParamIndex)
        {
            // The input variable is an array.

            register_symbol(d->symtab_begin, addVector, prefix + ".lower_limits",
                            input.lowerLimits.data, input.lowerLimits.size);

            register_symbol(d->symtab_begin, addVector, prefix + ".upper_limits",
                            input.upperLimits.data, input.upperLimits.size);

            register_symbol(d->symtab_begin, addConstant, prefix + ".size",
                            input.lowerLimits.size);
        }
        else
        {
            // The input variable is a single value.

            register_symbol(d->symtab_begin, addScalar, prefix + ".lower_limit",
                            input.lowerLimits.data[pi]);

            register_symbol(d->symtab_begin, addScalar, prefix + ".upper_limit",
                            input.upperLimits.data[pi]);

            // For compatibility with array inputs these additional variables
            // are also defined for single value inputs.
            // Note that SymbolTable::addVector() works on a reference to the
            // underlying data which means creating a std::vector<double> on
            // the stack and passing that to addVector() does not work! Instead
            // memory from the arena is used to store the vector data.

            auto lowerLimits = arena->pushArray<double>(1);
            auto upperLimits = arena->pushArray<double>(1);

            lowerLimits[0] = input.lowerLimits[pi];
            upperLimits[0] = input.upperLimits[pi];

            register_symbol(d->symtab_begin, addVector, prefix + ".lower_limits",
                            lowerLimits, 1);

            register_symbol(d->symtab_begin, addVector, prefix + ".upper_limits",
                            upperLimits, 1);

            register_symbol(d->symtab_begin, addConstant, prefix + ".size", 1);
        }
    }

    // Add the make_static() function to the symbol table.
    FuncMakeStatic makeStatic;
    d->symtab_begin.addFunction("make_static", makeStatic);

    /* Setup and evaluate the begin expression. */
    d->expr_begin.registerSymbolTable(make_expression_operator_runtime_library());
    d->expr_begin.registerSymbolTable(d->symtab_begin);
    d->expr_begin.setExpressionString(expr_begin_str);
    d->expr_begin.compile();
    d->expr_begin.eval();

    /* Build outputs from the information returned from the begin expression.
     *
     * The result format is a list of tuples with 5 elements per tuple. A tuple
     * defines a single output array. Each tuple must have the following form
     * and datatypes:
     *
     * output_var_name, output_unit, output_size, lower_limit_spec, upper_limit_spec
     * string,          string,      scalar,      scalar/array,     scalar/array
     *
     */
    static const size_t ElementsPerOutput = 5;

    auto begin_results = d->expr_begin.results();

    if (begin_results.size() == 0)
    {
        throw SemanticError("Empty result list from BeginExpression");
    }

    if (begin_results.size() % ElementsPerOutput != 0)
    {
        std::ostringstream ss;
        ss << "BeginExpression returned an invalid number of results ("
            << begin_results.size() << ")";
        throw SemanticError(ss.str());
    }

    const size_t outputCount = begin_results.size() / ElementsPerOutput;

    assert(outputCount < static_cast<size_t>(std::numeric_limits<s32>::max()));

    auto result = make_operator(arena, Operator_Expression, inputs.size(), outputCount);
    result.d = d;

    /* Assign operator inputs and create input symbol in the step symbol table. */
    for (size_t in_idx = 0; in_idx < inputs.size(); in_idx++)
    {
        assign_input(&result, inputs[in_idx], in_idx);

        const auto &input  = inputs[in_idx];
        const auto &prefix = input_prefixes[in_idx];
        const auto &unit   = input_units[in_idx];
        const auto &pi     = input_param_indexes[in_idx];

        register_symbol(d->symtab_step, createString, prefix + ".unit",
                        unit);

        if (pi == NoParamIndex)
        {
            // The input variable is an array.

            register_symbol(d->symtab_step, addVector, prefix,
                            input.data.data, input.data.size);

            register_symbol(d->symtab_step, addVector, prefix + ".lower_limits",
                            input.lowerLimits.data, input.lowerLimits.size);

            register_symbol(d->symtab_step, addVector, prefix + ".upper_limits",
                            input.upperLimits.data, input.upperLimits.size);

            register_symbol(d->symtab_step, addConstant, prefix + ".size",
                            input.lowerLimits.size);
        }
        else
        {
            // The input variable is a single value.

            register_symbol(d->symtab_step, addScalar, prefix,
                            input.data.data[pi]);

            register_symbol(d->symtab_step, addScalar, prefix + ".lower_limit",
                            input.lowerLimits.data[pi]);

            register_symbol(d->symtab_step, addScalar, prefix + ".upper_limit",
                            input.upperLimits.data[pi]);

            // For compatibility with array inputs these additional variables
            // are also defined for single value inputs.
            // Note that SymbolTable::addVector() works on a reference to the
            // underlying data which means creating a std::vector<double> on
            // the stack and passing that to addVector() does not work! Instead
            // memory from the arena is used to store the vector data.

            auto lowerLimits = arena->pushArray<double>(1);
            auto upperLimits = arena->pushArray<double>(1);

            lowerLimits[0] = input.lowerLimits[pi];
            upperLimits[0] = input.upperLimits[pi];

            register_symbol(d->symtab_step, addVector, prefix + ".lower_limits",
                            lowerLimits, 1);

            register_symbol(d->symtab_step, addVector, prefix + ".upper_limits",
                            upperLimits, 1);

            register_symbol(d->symtab_step, addConstant, prefix + ".size", 1);
        }
    }

    /* Interpret the results returned from the begin expression and build the
     * output vectors accordingly. */
    for (size_t out_idx = 0, result_idx = 0;
         out_idx < outputCount;
         out_idx++, result_idx += ElementsPerOutput)
    {
        auto outSpec = build_output_spec(
            out_idx, result_idx,
            begin_results[result_idx + 0],
            begin_results[result_idx + 1],
            begin_results[result_idx + 2],
            begin_results[result_idx + 3],
            begin_results[result_idx + 4]);

        push_output_vectors(arena, &result, out_idx, outSpec.lowerLimits.size());

        for (size_t paramIndex = 0;
             paramIndex < outSpec.lowerLimits.size();
             paramIndex++)
        {
            result.outputLowerLimits[out_idx][paramIndex] = outSpec.lowerLimits[paramIndex];
            result.outputUpperLimits[out_idx][paramIndex] = outSpec.upperLimits[paramIndex];
        }

        d->output_names.push_back(outSpec.name);
        d->output_units.push_back(outSpec.unit);

        //fprintf(stderr, "output[%lu] variable name = %s\n", out_idx, res_name.string.c_str());

        register_symbol(d->symtab_step, addVector, outSpec.name,
                        result.outputs[out_idx].data, result.outputs[out_idx].size);

        register_symbol(d->symtab_step, addVector, outSpec.name + ".lower_limits",
                        result.outputLowerLimits[out_idx].data, result.outputLowerLimits[out_idx].size);

        register_symbol(d->symtab_step, addVector, outSpec.name + ".upper_limits",
                        result.outputUpperLimits[out_idx].data, result.outputUpperLimits[out_idx].size);

        register_symbol(d->symtab_step, addConstant, outSpec.name + ".size",
                        result.outputs[out_idx].size);

        register_symbol(d->symtab_step, createString, outSpec.name + ".unit",
                        outSpec.unit);
    }

    // Create permanent storage for the static variables and register them with
    // the symbol table. The memory locations of the static var store may not
    // change after this because the symbol table directly references them!
    d->static_vars = makeStatic.vardefs;

    for (auto &kv: d->static_vars)
    {
        auto &varname = kv.first;
        auto &varstore = kv.second;

        if (varstore.type == a2_exprtk::TypeStore::Scalar)
            register_symbol(d->symtab_step, addScalar, varname, varstore.scalar);
        else if (varstore.type == a2_exprtk::TypeStore::Vector)
            register_symbol(d->symtab_step, addVector, varname,
                            varstore.vector.data(), varstore.vector.size());
    }

    d->expr_step.registerSymbolTable(make_expression_operator_runtime_library());
    d->expr_step.registerSymbolTable(d->symtab_step);
    d->expr_step.setExpressionString(expr_step_str);

    if (options == ExpressionOperatorBuildOptions::FullBuild)
    {
        expression_operator_compile_step_expression(&result);
    }

    return result;
}

#undef register_symbol

void expression_operator_compile_step_expression(Operator *op)
{
    assert(op->type == Operator_Expression);

    auto d = reinterpret_cast<ExpressionOperatorData *>(op->d);

    d->expr_step.compile();
}

void expression_operator_step(Operator *op, A2 *)
{
    assert(op->type == Operator_Expression);

    auto d = reinterpret_cast<ExpressionOperatorData *>(op->d);

    /* References to the input and output have been bound in
     * make_expression_operator(). No need to pass anything here, just evaluate
     * the step expression. */
    d->expr_step.eval();
}

//
// ScalerOverflow
//
struct ScalerOverflowData
{
    TypedBlock<double, s32> previousValues;
    TypedBlock<unsigned, s32> overflowCounts;
};

Operator make_scaler_overflow(
    memory::Arena *arena,
    const PipeVectors &input)
{
    auto result = make_operator(arena, Operator_ScalerOverflow, 1, 2);

    // data
    auto d = arena->push<ScalerOverflowData>({});
    result.d = d;

    d->previousValues = push_typed_block<double, s32>(arena, input.size());
    std::fill(std::begin(d->previousValues), std::end(d->previousValues), 0.0);

    d->overflowCounts = push_typed_block<unsigned, s32>(arena, input.size());
    std::fill(std::begin(d->overflowCounts), std::end(d->overflowCounts), 0u);

    // input/outputs
    assign_input(&result, input, 0);

    push_output_vectors(arena, &result, 0, input.size(), 0.0, std::numeric_limits<double>::max()); // value out
    push_output_vectors(arena, &result, 1, input.size(), 0.0, std::numeric_limits<double>::max()); // overflow count out

    return result;
}

struct ScalerOverflowData_idx
{
    s32 inputIndex;
    double previousValue;
    unsigned overflowCount;
};

Operator make_scaler_overflow_idx(
    memory::Arena *arena,
    const PipeVectors &input,
    s32 inputIndex)
{
    assert(0 <= inputIndex && inputIndex < input.size());

    auto result = make_operator(arena, Operator_ScalerOverflow_idx, 1, 2);

    // data
    auto d = arena->push<ScalerOverflowData_idx>({});
    result.d = d;

    d->inputIndex = inputIndex;
    d->previousValue = 0.0;
    d->overflowCount = 0;

    // input/outputs
    assign_input(&result, input, 0);

    push_output_vectors(arena, &result, 0, 1, 0.0, std::numeric_limits<double>::max()); // value out
    push_output_vectors(arena, &result, 1, 1, 0.0, std::numeric_limits<double>::max()); // overflow count out

    return result;
}

void scaler_overflow_step(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->type == Operator_ScalerOverflow);

    auto input = op->inputs[0];
    auto inputUpperLimits = op->inputUpperLimits[0];
    auto valueOutput = op->outputs[0];
    auto overflowCountOutput = op->outputs[1];
    auto d = reinterpret_cast<ScalerOverflowData *>(op->d);

    for (s32 i=0; i<input.size; ++i)
    {
        double inputValue = input[i];

        if (is_param_valid(inputValue))
        {
            double diff = inputValue - d->previousValues[i];

            if (diff < 0.0)
                ++d->overflowCounts[i];

            d->previousValues[i] = inputValue;

            double maxValue = inputUpperLimits[i];
            double result = inputValue + d->overflowCounts[i] * maxValue;
            valueOutput[i] = result;
            overflowCountOutput[i] = d->overflowCounts[i];
        }
    }
}

void scaler_overflow_step_idx(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->type == Operator_ScalerOverflow_idx);

    auto input = op->inputs[0];
    auto inputUpperLimits = op->inputUpperLimits[0];
    auto valueOutput = op->outputs[0];
    auto overflowCountOutput = op->outputs[1];
    auto d = reinterpret_cast<ScalerOverflowData_idx *>(op->d);

    double inputValue = input[d->inputIndex];

    if (is_param_valid(inputValue))
    {
        double diff = inputValue - d->previousValue;

        if (diff < 0.0)
            ++d->overflowCount;

        d->previousValue = inputValue;

        double maxValue = inputUpperLimits[d->inputIndex];
        double result = inputValue + d->overflowCount * maxValue;
        valueOutput[0] = result;
        overflowCountOutput[0] = d->overflowCount;
    }
}

/* ===============================================
 * Conditions
 * =============================================== */

struct ConditionIntervalData: public ConditionBaseData
{
    TypedBlock<Interval, s32> intervals;
};

struct ConditionRectangleData: public ConditionBaseData
{
    Interval xInterval;
    Interval yInterval;
    s32 xIndex;
    s32 yIndex;
};

namespace bg = boost::geometry;

using Point = bg::model::d2::point_xy<double>;

/* Note: I would have liked to use arena allocation for the polygon ring
 * storage but the boost polygon implementation does not support passing in
 * instances of stateful allocators (yet?). */
#if 0
using Polygon = bg::model::polygon<
    Point, true, true,              // Point, ClockWise, Closed
    std::vector, std::vector,       // PointList, RingList
    memory::ArenaAllocator,         // PointAlloc
    memory::ArenaAllocator>;        // RingAlloc
#else
using Polygon = bg::model::polygon<
    Point, true, true>;             // Point, ClockWise, Closed
#endif

struct ConditionPolygonData: public ConditionBaseData
{
    Polygon polygon;
    s32 xIndex;
    s32 yIndex;
};

bool is_condition_operator(const Operator &op)
{
    switch (op.type)
    {
        case Operator_ConditionInterval:
        case Operator_ConditionRectangle:
        case Operator_ConditionPolygon:
            return true;

        default:
            break;
    }

    return false;
}

u32 get_number_of_condition_bits_used(const Operator &op)
{
    assert(op.inputCount >= 1);

    switch (op.type)
    {
        case Operator_ConditionInterval:
            return op.inputs[0].size;

        case Operator_ConditionRectangle:
        case Operator_ConditionPolygon:
            return 1;

        default:
            break;
    }

    return 0;
}

Operator make_condition_interval(
    memory::Arena *arena,
    PipeVectors input,
    const std::vector<Interval> &intervals)
{
    auto result = make_operator(arena, Operator_ConditionInterval, 1, 0);

    assign_input(&result, input, 0);

    auto d = arena->pushStruct<ConditionIntervalData>();
    result.d = d;

    d->firstBitIndex = ConditionBaseData::InvalidIndex;
    d->intervals = push_copy_typed_block<Interval, s32>(arena, intervals);

    return result;
}

Operator make_condition_rectangle(
    memory::Arena *arena,
    PipeVectors xInput,
    PipeVectors yInput,
    s32 xIndex,
    s32 yIndex,
    Interval xInterval,
    Interval yInterval)
{
    auto result = make_operator(arena, Operator_ConditionRectangle, 2, 0);

    assign_input(&result, xInput, 0);
    assign_input(&result, yInput, 1);

    auto d = arena->pushStruct<ConditionRectangleData>();
    result.d = d;

    d->firstBitIndex = ConditionBaseData::InvalidIndex;
    d->xIndex = xIndex;
    d->yIndex = yIndex;
    d->xInterval = xInterval;
    d->yInterval = yInterval;

    return result;
}

Operator make_condition_polygon(
    memory::Arena *arena,
    PipeVectors xInput,
    PipeVectors yInput,
    s32 xIndex,
    s32 yIndex,
    const std::vector<std::pair<double, double>> &polygon)
{
    auto result = make_operator(arena, Operator_ConditionPolygon, 2, 0);

    assign_input(&result, xInput, 0);
    assign_input(&result, yInput, 1);

    // Note: pushObject because ConditionPolygonData is non-trivial
    auto d = arena->pushObject<ConditionPolygonData>();
    result.d = d;

    d->firstBitIndex = ConditionBaseData::InvalidIndex;
    d->xIndex = xIndex;
    d->yIndex = yIndex;

    d->polygon.outer().reserve(polygon.size());

    for (const auto &p: polygon)
    {
        bg::append(d->polygon, Point{p.first, p.second});
    }

    return result;

}

void condition_interval_step(Operator *op, A2 *a2)
{
    a2_trace("\n");
    assert(op->inputCount == 1);
    assert(op->outputCount == 0);
    assert(op->type == Operator_ConditionInterval);

    auto d = reinterpret_cast<ConditionIntervalData *>(op->d);

    assert(op->inputs[0].size == d->intervals.size);
    assert(0 <= d->firstBitIndex);
    assert(static_cast<size_t>(d->firstBitIndex) < a2->conditionBits.size());
    assert(static_cast<size_t>(d->firstBitIndex) + d->intervals.size <= a2->conditionBits.size());

    const s32 maxIdx = op->inputs[0].size;

    for (s32 idx = 0; idx < maxIdx; idx++)
    {
        bool condResult = in_range(d->intervals[idx], op->inputs[0][idx]);

        a2->conditionBits.set(d->firstBitIndex + idx, condResult);
    }
}

void condition_rectangle_step(Operator *op, A2 *a2)
{
    a2_trace("\n");
    assert(op->inputCount == 2);
    assert(op->outputCount == 0);
    assert(op->type == Operator_ConditionRectangle);

    auto d = reinterpret_cast<ConditionRectangleData *>(op->d);

    assert(0 <= d->firstBitIndex);
    assert(static_cast<size_t>(d->firstBitIndex) < a2->conditionBits.size());
    assert(d->xIndex < op->inputs[0].size);
    assert(d->yIndex < op->inputs[1].size);

    bool xInside = in_range(d->xInterval, op->inputs[0][d->xIndex]);
    bool yInside = in_range(d->yInterval, op->inputs[1][d->yIndex]);

    a2->conditionBits.set(d->firstBitIndex, xInside && yInside);
}

void condition_polygon_step(Operator *op, A2 *a2)
{
    a2_trace("\n");
    assert(op->inputCount == 2);
    assert(op->outputCount == 0);
    assert(op->type == Operator_ConditionPolygon);

    auto d = reinterpret_cast<ConditionPolygonData *>(op->d);

    assert(0 <= d->firstBitIndex);
    assert(static_cast<size_t>(d->firstBitIndex) < a2->conditionBits.size());
    assert(d->xIndex < op->inputs[0].size);
    assert(d->yIndex < op->inputs[1].size);

    Point p = { op->inputs[0][d->xIndex], op->inputs[1][d->yIndex] };

    bool condResult = bg::within(p, d->polygon);

    a2->conditionBits.set(d->firstBitIndex, condResult);
}

/*
struct ConditionLogicData
{
};
*/

/* ===============================================
 * Sinks: Histograms/RateMonitor/ExportSink
 * =============================================== */

inline double get_bin_unchecked(Binning binning, s32 binCount, double x)
{
    return (x - binning.min) * binCount / binning.range;
}

// binMin = binning.min
// binFactor = binCount / binning.range
inline double get_bin_unchecked(double x, double binMin, double binFactor)
{
    return (x - binMin) * binFactor;
}

inline s32 get_bin(Binning binning, s32 binCount, double x)
{
    double bin = get_bin_unchecked(binning, binCount, x);

    if (bin < 0.0)
        return Binning::Underflow;

    if (bin >= binCount)
        return Binning::Overflow;

    return static_cast<s32>(bin);
}

inline s32 get_bin(H1D histo, double x)
{
    return get_bin(histo.binning, histo.size, x);
}

inline s32 get_bin(H2D histo, H2D::Axis axis, double v)
{
    return get_bin(histo.binnings[axis], histo.binCounts[axis], v);
}

//
// HistoFillDirect
//

// Returns true if the value is not NaN and falls within the histos binning.
// Otherwise updates overflow/underflow of the histo and returns false.
inline bool range_check_update(H1D *histo, double x)
{
    assert(histo);

    /* Instead of calculating the bin and then checking if it under/overflows
     * this code decides by comparing x to the binnings min and max values.
     * This is faster. */

    if (x < histo->binning.min)
    {
#if 0
        cerr << __PRETTY_FUNCTION__
            << " histo=" << histo << ", x < min, x=" << x << ", get_bin=" << get_bin(*histo, x) << endl;
#endif

        assert(get_bin(*histo, x) == Binning::Underflow);
        if (histo->underflow)
            ++(*histo->underflow);
    }
    else if (x >= histo->binning.min + histo->binning.range)
    {
#if 0
        if (get_bin(*histo, x) != Binning::Overflow)
        {
            cerr << __PRETTY_FUNCTION__
                << " histo=" << histo << ", x >= max, x=" << x << ", get_bin=" << get_bin(*histo, x)
                << ", binning.min=" << histo->binning.min
                << ", binning.range=" << histo->binning.range
                << " => binning.max=" << histo->binning.min + histo->binning.range
                << endl;
        }
#endif

        assert(histo->binning.range == 0.0 || get_bin(*histo, x) == Binning::Overflow);
        if (histo->overflow)
            ++(*histo->overflow);
    }
    else
        return !std::isnan(x);

    return false;
}

inline void HistoFillDirect::fill_h1d(H1D *histo, double x)
{
    assert(histo);

    if (range_check_update(histo, x))
    {
        assert(0 <= get_bin(*histo, x) && get_bin(*histo, x) < histo->size);

        //s32 bin = static_cast<s32>(get_bin_unchecked(histo->binning, histo->size, x));
        s32 bin = static_cast<s32>(get_bin_unchecked(x, histo->binning.min, histo->binningFactor));

        histo->data[bin]++;
        histo->entryCount++;
    }
}

inline void HistoFillDirect::fill_h2d(H2D *histo, double x, double y)
{
    if (x < histo->binnings[H2D::XAxis].min)
    {
        assert(get_bin(*histo, H2D::XAxis, x) == Binning::Underflow);
        histo->underflow++;
    }
    else if (x >= histo->binnings[H2D::XAxis].min + histo->binnings[H2D::XAxis].range)
    {
        assert(get_bin(*histo, H2D::XAxis, x) == Binning::Overflow);
        histo->overflow++;
    }
    else if (y < histo->binnings[H2D::YAxis].min)
    {
        assert(get_bin(*histo, H2D::YAxis, y) == Binning::Underflow);
        histo->underflow++;
    }
    else if (y >= histo->binnings[H2D::YAxis].min + histo->binnings[H2D::YAxis].range)
    {
        assert(get_bin(*histo, H2D::YAxis, y) == Binning::Overflow);
        histo->overflow++;
    }
    else if (std::isnan(x) || std::isnan(y))
    {
        // pass for now
    }
    else if (likely(1))
    {
        assert(0 <= get_bin(*histo, H2D::XAxis, x)
               && get_bin(*histo, H2D::XAxis, x) < histo->binCounts[H2D::XAxis]);

        assert(0 <= get_bin(*histo, H2D::YAxis, y)
               && get_bin(*histo, H2D::YAxis, y) < histo->binCounts[H2D::YAxis]);

        s32 xBin = static_cast<s32>(get_bin_unchecked(
                x,
                histo->binnings[H2D::XAxis].min,
                histo->binningFactors[H2D::XAxis]));

        s32 yBin = static_cast<s32>(get_bin_unchecked(
                y,
                histo->binnings[H2D::YAxis].min,
                histo->binningFactors[H2D::YAxis]));

        s32 linearBin = yBin * histo->binCounts[H2D::XAxis] + xBin;

        a2_trace("x=%lf, y=%lf, xBin=%d, yBin=%d, linearBin=%d\n",
                 x, y, xBin, yBin, linearBin);


        assert(0 <= linearBin && linearBin < histo->size);

        histo->data[linearBin]++;
        histo->entryCount++;
    }
}

//
// HistoFillBuffered
//

inline bool is_full(const FillBuffer &buffer)
{
    return buffer.used >= buffer.bins.size();
}

void flush(H1D *histo, FillBuffer &buffer)
{
    //std::sort(std::begin(buffer.bins), std::end(buffer.bins));

    for (size_t i=0; i<buffer.used; i++)
    {
        s32 bin = buffer.bins[i];
        histo->data[bin]++;
    }

    histo->entryCount += buffer.used;
    buffer.used = 0;
}

HistoFillBuffered::HistoFillBuffered()
    : m_arena(1024 * 1024 * 256)
{}

void HistoFillBuffered::begin_run(A2 *a2)
{
    m_arena.reset();

    std::vector<H1D *> histos_1d;

    for (unsigned ei=0; ei<a2->operatorCounts.size(); ++ei)
    {
        const auto opCount = a2->operatorCounts[ei];

        for (unsigned opIdx = 0; opIdx < opCount; ++opIdx)
        {
            const auto &op = a2->operators[ei][opIdx];

            if (op.type == Operator_H1DSink)
            {
                auto d = reinterpret_cast<H1DSinkData *>(op.d);

                for (auto &h1d: d->histos)
                    histos_1d.push_back(&h1d);

            }
            else if (op.type == Operator_H1DSink_idx)
            {
                auto d = reinterpret_cast<H1DSinkData_idx *>(op.d);

                histos_1d.push_back(&d->histos[d->inputIndex]);
            }
        }
    }

    // Sort so that std::lower_bound() works.
    std::sort(std::begin(histos_1d), std::end(histos_1d));

    m_histos =  push_copy_typed_block(&m_arena, histos_1d);
    m_buffers = push_typed_block<FillBuffer, size_t>(&m_arena, m_histos.size);
}

void HistoFillBuffered::end_run(A2 *)
{
    for (size_t i=0; i<m_histos.size; i++)
    {
        auto histo = m_histos[i];
        auto &buffer = m_buffers[i];
        flush(histo, buffer);
    }
}

void HistoFillBuffered::fill_h1d(H1D *histo, double x)
{
    if (range_check_update(histo, x))
    {
        auto it = std::lower_bound(m_histos.begin(), m_histos.end(), histo);
        assert(it != m_histos.end());
        auto idx = it - m_histos.begin();

        auto &buffer = m_buffers[idx];
        assert(buffer.used < buffer.bins.size());

        assert(0 <= get_bin(*histo, x) && get_bin(*histo, x) < histo->size);

        s32 bin = static_cast<s32>(get_bin_unchecked(x, histo->binning.min, histo->binningFactor));
        buffer.bins[buffer.used++] = bin;

        if (is_full(buffer))
            flush(histo, buffer);
    }
}

void HistoFillBuffered::fill_h2d(H2D *histo, double x, double y)
{
    HistoFillDirect().fill_h2d(histo, x, y);
}

inline double get_value(H1D histo, double x)
{
    s32 bin = get_bin(histo, x);
    return (bin < 0) ? 0.0 : histo.data[bin];
}

void clear_histo(H1D *histo)
{
    histo->binningFactor = 0.0;
    histo->entryCount = 0.0;

    if (histo->underflow)
        *histo->underflow = 0.0;

    if (histo->overflow)
        *histo->overflow = 0.0;

    for (s32 i = 0; i < histo->size; i++)
    {
        histo->data[i] = 0.0;
    }
}

/* Note: The H1D instances in the 'histos' variable are copied. This means
 * during runtime only the H1D structures inside H1DSinkData are updated.
 * Histogram storage itself is not copied. It is assumed that this storage is
 * handled separately.
 * The implementation could be changed to store an array of pointers to H1D.
 * Then the caller would have to keep the H1D instances around too.
 */
Operator make_h1d_sink(
    Arena *arena,
    PipeVectors inPipe,
    TypedBlock<H1D, s32> histos)
{
    assert(inPipe.data.size == histos.size);
    auto result = make_operator(arena, Operator_H1DSink, 1, 0);
    assign_input(&result, inPipe, 0);

    auto d = arena->pushStruct<H1DSinkData>();
    result.d = d;

    d->histos = push_typed_block<H1D, s32>(arena, histos.size);

    for (s32 i = 0; i < histos.size; i++)
    {
        d->histos[i] = histos[i];
    }

    return result;
}

void h1d_sink_step(Operator *op, A2 *a2)
{
    a2_trace("\n");
    auto d = reinterpret_cast<H1DSinkData *>(op->d);
    s32 maxIdx = op->inputs[0].size;

    for (s32 idx = 0; idx < maxIdx; idx++)
    {
        a2->histoFillStrategy.fill_h1d(&d->histos[idx], op->inputs[0][idx]);
    }
}

void h1d_sink_step_idx(Operator *op, A2 *a2)
{
    a2_trace("\n");
    auto d = reinterpret_cast<H1DSinkData_idx *>(op->d);

    assert(d->histos.size == 1);
    assert(d->inputIndex < op->inputs[0].size);

    a2->histoFillStrategy.fill_h1d(&d->histos[0], op->inputs[0][d->inputIndex]);
}

Operator make_h1d_sink_idx(
    Arena *arena,
    PipeVectors inPipe,
    TypedBlock<H1D, s32> histos,
    s32 inputIndex)
{
    assert(histos.size == 1);
    assert(inputIndex < inPipe.data.size);

    auto result = make_operator(arena, Operator_H1DSink_idx, 1, 0);
    assign_input(&result, inPipe, 0);

    auto d = arena->pushStruct<H1DSinkData_idx>();
    result.d = d;

    d->histos = push_typed_block<H1D, s32>(arena, histos.size);
    d->inputIndex = inputIndex;

    for (s32 i = 0; i < histos.size; i++)
    {
        d->histos[i] = histos[i];
    }

    return result;
}

Operator make_h2d_sink(
    Arena *arena,
    PipeVectors xInput,
    PipeVectors yInput,
    s32 xIndex,
    s32 yIndex,
    H2D histo)
{
    assert(0 <= xIndex && xIndex < xInput.data.size);
    assert(0 <= yIndex && yIndex < yInput.data.size);

    auto result = make_operator(arena, Operator_H2DSink, 2, 0);

    assign_input(&result, xInput, 0);
    assign_input(&result, yInput, 1);

    auto d = arena->push<H2DSinkData>({ histo, xIndex, yIndex });
    result.d = d;

    return result;
};

void h2d_sink_step(Operator *op, A2 *a2)
{
    a2_trace("\n");

    auto d = reinterpret_cast<H2DSinkData *>(op->d);

    a2->histoFillStrategy.fill_h2d(
        &d->histo,
        op->inputs[0][d->xIndex],
        op->inputs[1][d->yIndex]);
}

//
// RateMonitor
//

static OperatorType operator_type(RateMonitorType rateMonitorType)
{
    switch (rateMonitorType)
    {
        case RateMonitorType::CounterDifference:
            return Operator_RateMonitor_CounterDifference;

        case RateMonitorType::PrecalculatedRate:
            return Operator_RateMonitor_PrecalculatedRate;

        case RateMonitorType::FlowRate:
            return Operator_RateMonitor_FlowRate;

        InvalidDefaultCase;
    };

    return OperatorTypeCount;
}

struct RateMonitorData
{
    TypedBlock<RateSampler *, s32> samplers;
    TypedBlock<s32, s32> input_param_indexes;
};

struct RateMonitorData_FlowRate: public RateMonitorData
{
    ParamVec hitCounts;
};

static void debug_samplers(const TypedBlock<RateSampler *, s32> &samplers, const std::string &prefix)
{
    (void) prefix;

    for (s32 i = 0; i < samplers.size; i++)
    {
        RateSampler *sampler = samplers[i];
        (void) sampler;

        a2_trace("%s: sampler[%d]@%p, rateHistory@%p, capacity=%lu, size=%lu\n",
                prefix.c_str(),
                i,
                sampler,
                &sampler->rateHistory,
                sampler->rateHistory.capacity(),
                sampler->rateHistory.size()
               );
    }
}

Operator make_rate_monitor(
    memory::Arena *arena,
    TypedBlock<PipeVectors, s32> inputs,
    TypedBlock<s32, s32> input_param_indexes,
    TypedBlock<RateSampler *, s32> samplers,
    RateMonitorType type)
{
    assert(inputs.size == input_param_indexes.size);

    s32 expectedSamplerCount = 0u;

    for (s32 ii = 0; ii < inputs.size; ii++)
    {
        debug_samplers(samplers, "input" + std::to_string(ii));

        if (input_param_indexes[ii] < 0)
            expectedSamplerCount += inputs[ii].data.size;
        else
            expectedSamplerCount++;
    }

    assert(samplers.size == expectedSamplerCount);

    auto result = make_operator(arena, operator_type(type), inputs.size, 0);

    switch (type)
    {
        case RateMonitorType::CounterDifference:
        case RateMonitorType::PrecalculatedRate:
            {
                auto d = arena->pushStruct<RateMonitorData>();
                result.d = d;
                d->samplers = push_copy_typed_block(arena, samplers);
                d->input_param_indexes = push_copy_typed_block(arena, input_param_indexes);
            } break;

        case RateMonitorType::FlowRate:
            {
                auto d = arena->pushStruct<RateMonitorData_FlowRate>();
                result.d = d;
                d->samplers = push_copy_typed_block(arena, samplers);
                d->input_param_indexes = push_copy_typed_block(arena, input_param_indexes);
                d->hitCounts = push_param_vector(arena, samplers.size, 0.0);
            } break;
    }


    for (s32 ii = 0; ii < inputs.size; ii++)
    {
        const auto &input  = inputs[ii];

        assign_input(&result, input, ii);
    }

    return result;
}

void rate_monitor_step(Operator *op, A2 *)
{
    a2_trace("\n");

    auto d = reinterpret_cast<RateMonitorData *>(op->d);
    s32 samplerIndex = 0;

    for (s32 ii = 0; ii < op->inputCount; ii++)
    {
        assert(samplerIndex < d->samplers.size);

        auto &input = op->inputs[ii];
        const auto pi = d->input_param_indexes[ii];

        switch (op->type)
        {
            case Operator_RateMonitor_PrecalculatedRate:
                {
                    //a2_trace("recording %d precalculated rates\n", maxIdx);

                    if (pi == NoParamIndex)
                    {
                        for (s32 paramIndex = 0; paramIndex < input.size; paramIndex++)
                        {
                            double value = input[paramIndex];

                            //a2_trace_np("  [%d] recording value %lf\n", idx, value);

                            d->samplers[samplerIndex++]->recordRate(value);
                        }
                    }
                    else
                    {
                        assert(pi < input.size);

                        double value = input[pi];

                        //a2_trace_np("  [%d] recording value %lf\n", idx, value);

                        d->samplers[samplerIndex++]->recordRate(value);
                    }
                } break;

            case Operator_RateMonitor_CounterDifference:
                {
                    //a2_trace("recording %d counter differences\n", maxIdx);

                    if (pi == NoParamIndex)
                    {
                        for (s32 paramIndex = 0; paramIndex < input.size; paramIndex++)
                        {
                            //a2_trace_np("  [%d] sampling value %lf, lastValue=%lf, delta=%lf\n",
                            //            idx, op->inputs[0][idx],
                            //            d->samplers[idx]->lastValue,
                            //            op->inputs[0][idx] - d->samplers[idx]->lastValue
                            //           );

                            double value = input[paramIndex];
                            d->samplers[samplerIndex++]->sample(value);
                        }
                    }
                    else
                    {
                        assert(pi < input.size);

                        double value = input[pi];

                        //a2_trace_np("  [%d] recording value %lf\n", idx, value);

                        d->samplers[samplerIndex++]->sample(value);
                    }
                } break;

            case Operator_RateMonitor_FlowRate:
                {
                    auto d = reinterpret_cast<RateMonitorData_FlowRate *>(op->d);
                    assert(d->hitCounts.size == d->samplers.size);

                    //a2_trace("incrementing %d hitCounts\n", maxIdx);
                    if (pi == NoParamIndex)
                    {
                        for (s32 paramIndex = 0; paramIndex < input.size; paramIndex++)
                        {
                            if (is_param_valid(input[paramIndex]))
                            {
                                d->hitCounts[samplerIndex]++;
                            }

                            samplerIndex++;
                        }
                    }
                    else
                    {
                        if (is_param_valid(input[pi]))
                        {
                            d->hitCounts[samplerIndex]++;
                        }

                        samplerIndex++;
                    }

                } break;

                InvalidDefaultCase;
        }
    }

    assert(samplerIndex == d->samplers.size);
}

void rate_monitor_sample_flow(Operator *op)
{
    assert(op->type == Operator_RateMonitor_FlowRate);

    auto d = reinterpret_cast<RateMonitorData_FlowRate *>(op->d);

    assert(d->hitCounts.size == d->samplers.size);

    a2_trace("recording %d flow rates\n", d->hitCounts.size);

    for (s32 idx = 0; idx < d->hitCounts.size; idx++)
    {
        auto sampler = d->samplers[idx];
        auto count   = d->hitCounts[idx];

        sampler->sample(count);

        a2_trace_np("  [%d] lastRate=%lf, history size =%lf, history capacity=%lf\n",
                    idx, sampler->lastRate,
                    static_cast<double>(sampler->rateHistory.size()),
                    static_cast<double>(sampler->rateHistory.capacity())
                   );
    }
}

//
// ExportSink
//

Operator make_export_sink(
    memory::Arena *arena,
    const std::string &output_filename,
    int compressionLevel,
    ExportSinkFormat format,
    TypedBlock<PipeVectors, s32> dataInputs,
    std::vector<std::string> csvColumns
    )
{
    return make_export_sink(
        arena,
        output_filename,
        compressionLevel,
        format,
        dataInputs,
        PipeVectors(),
        -1,
        csvColumns);
}

Operator make_export_sink(
    memory::Arena *arena,
    const std::string &output_filename,
    int compressionLevel,
    ExportSinkFormat format,
    TypedBlock<PipeVectors, s32> dataInputs,
    PipeVectors condInput,
    s32 condIndex,
    std::vector<std::string> csvColumns
    )
{
    Operator result = {};

    s32 inputCount = dataInputs.size;

    if (condIndex >= 0)
        inputCount++;

    switch (format)
    {
        case ExportSinkFormat::Full:
            result = make_operator(arena, Operator_ExportSinkFull, inputCount, 0);
            break;
        case ExportSinkFormat::Sparse:
            result = make_operator(arena, Operator_ExportSinkSparse, inputCount, 0);
            break;
        case ExportSinkFormat::CSV:
            result = make_operator(arena, Operator_ExportSinkCsv, inputCount, 0);
            break;
    }

    auto d = arena->pushObject<ExportSinkData>();
    result.d = d;

    d->filename         = output_filename;
    d->compressionLevel = compressionLevel;
    d->condIndex        = condIndex;
    d->csvColumns       = csvColumns;

    // Assign data inputs.
    for (s32 ii = 0; ii < dataInputs.size; ii++)
    {
        assign_input(&result, dataInputs[ii], ii);
    }

    // The optional condition input is the last input. It's only used if
    // condIndex is valid.
    if (condIndex >= 0)
    {
        assign_input(&result, condInput, inputCount - 1);
    }

    return result;
}

/* NOTE: About error handling in the ExportSink:
 * - std::ofstream by default has exceptions disabled. The method rdstate() can
 *   be used to query the status of the error bits after each operation.
 *
 * - The zstr implementation enables exceptions by default and from looking at
 *   the code the implementation assumes that exceptions stay enabled.
 *
 * > The export sink code enables exceptions both for the low level ofstream
 *   and for the zstr ostream.
 *
 *   All I/O operations must be wrapped in a try/catch block. Additionally any
 *   further I/O operations are only performed if the good() method of the
 *   stream returns true. This means after the first I/O exception is caught no
 *   further attempts at writing to the file are performed.
 */

void export_sink_begin_run(Operator *op, Logger logger)
{
    a2_trace("\n");
    assert(op->type == Operator_ExportSinkFull
           || op->type == Operator_ExportSinkSparse
           || op->type == Operator_ExportSinkCsv
           );

    auto d = reinterpret_cast<ExportSinkData *>(op->d);

    if (op->type == Operator_ExportSinkCsv)
        d->ostream.reset(new std::ofstream(d->filename, std::ios::trunc));
    else
        d->ostream.reset(new std::ofstream(d->filename, std::ios::binary | std::ios::trunc));

    // Enable ios exceptions for the lowest level output stream. This operation can throw.
    try
    {
        d->ostream->exceptions(std::ios::failbit | std::ios::badbit);

        if (d->compressionLevel != 0)
        {
            d->z_ostream.reset(new zstr::ostream(*d->ostream));
        }

        std::ostringstream ss;
        ss << "File Export: Opened output file " << d->filename;
        logger(ss.str());

        if (op->type == Operator_ExportSinkCsv)
        {
            auto outp = d->getOstream();
            for (const auto &col: d->csvColumns)
                *outp << col << ",";
            *outp << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::ostringstream ss;
        ss << "File Export: Error opening output file " << d->filename << ": " << e.what();
        logger(ss.str());
        d->setLastError(ss.str());
    }
}

void export_sink_full_step(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->type == Operator_ExportSinkFull);

    auto d = reinterpret_cast<ExportSinkData *>(op->d);

    auto outp = d->getOstream();

    if (!outp) return;

    s32 dataInputCount = op->inputCount;

    // Test the condition input if it's used
    if (d->condIndex >= 0)
    {
        assert(d->condIndex < op->inputs[op->inputCount - 1].size);

        if (!is_param_valid(op->inputs[op->inputCount - 1][d->condIndex]))
            return;

        dataInputCount = op->inputCount - 1;
    }

    try
    {
        if (outp->good())
        {
            for (s32 inputIndex = 0;
                 inputIndex < dataInputCount && outp->good();
                 inputIndex++)
            {
                auto input = op->inputs[inputIndex];
                assert(input.size <= std::numeric_limits<u16>::max());

                size_t bytes = input.size * sizeof(double);

                outp->write(reinterpret_cast<char *>(input.data), bytes);

                d->bytesWritten += bytes;
            }

            d->eventsWritten++;
        }
    }
    catch (const std::exception &e)
    {
        std::ostringstream ss;
        ss << "Error writing to output file " << d->filename << ": " << e.what();
        d->setLastError(ss.str());
    }
}

static size_t write_indexed_parameter_vector(std::ostream &out, const ParamVec &vec)
{
    assert(vec.size >= 0);
    assert(vec.size <= std::numeric_limits<u16>::max());

    size_t bytesWritten = 0;
    u16 validCount = 0;

    for (s32 i = 0; i < vec.size; i++)
    {
        if (is_param_valid(vec[i]))
            validCount++;
    }

    // Write a size prefix and two arrays with length 'validCount', one
    // containing 16-bit index values, the other containing the corresponding
    // parameter values.
    out.write(reinterpret_cast<char *>(&validCount), sizeof(validCount));
    bytesWritten += sizeof(validCount);

    for (u16 i = 0; i < static_cast<u16>(vec.size); i++)
    {
        if (is_param_valid(vec[i]))
        {
            // 16-bit index value
            out.write(reinterpret_cast<char *>(&i), sizeof(i));
            bytesWritten += sizeof(i);
        }
    }

    for (u16 i = 0; i < static_cast<u16>(vec.size); i++)
    {
        if (is_param_valid(vec[i]))
        {
            // 64-bit double value
            out.write(reinterpret_cast<char *>(vec.data + i), sizeof(double));
            bytesWritten += sizeof(double);
        }
    }

    return bytesWritten;
}

void export_sink_sparse_step(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->type == Operator_ExportSinkSparse);

    auto d = reinterpret_cast<ExportSinkData *>(op->d);

    auto outp = d->getOstream();

    if (!outp) return;

    s32 dataInputCount = op->inputCount;

    // Test the condition input if it's used
    if (d->condIndex >= 0)
    {
        assert(d->condIndex < op->inputs[op->inputCount - 1].size);

        if (!is_param_valid(op->inputs[op->inputCount - 1][d->condIndex]))
            return;

        dataInputCount = op->inputCount - 1;
    }

    try
    {
        if (outp->good())
        {
            for (s32 inputIndex = 0;
                 inputIndex < dataInputCount && outp->good();
                 inputIndex++)
            {
                auto input = op->inputs[inputIndex];
                assert(input.size <= std::numeric_limits<u16>::max());

                size_t bytes = write_indexed_parameter_vector(*outp, input);
                d->bytesWritten += bytes;
            }

            d->eventsWritten++;
        }
    }
    catch (const std::exception &e)
    {
        std::ostringstream ss;
        ss << "Error writing to output file " << d->filename << ": " << e.what();
        d->setLastError(ss.str());
    }
}

void export_sink_csv_step(Operator *op, A2 *)
{
    a2_trace("\n");
    assert(op->type == Operator_ExportSinkCsv);

    auto d = reinterpret_cast<ExportSinkData *>(op->d);

    auto outp = d->getOstream();

    if (!outp) return;

    s32 dataInputCount = op->inputCount;

    // Test the condition input if it's used
    if (d->condIndex >= 0)
    {
        assert(d->condIndex < op->inputs[op->inputCount - 1].size);

        if (!is_param_valid(op->inputs[op->inputCount - 1][d->condIndex]))
            return;

        dataInputCount = op->inputCount - 1;
    }

    try
    {
        if (outp->good())
        {
            for (s32 inputIndex = 0;
                 inputIndex < dataInputCount && outp->good();
                 inputIndex++)
            {
                auto input = op->inputs[inputIndex];
                assert(input.size <= std::numeric_limits<u16>::max());

                for (s32 i=0; i<input.size; ++i)
                {
                    if (is_param_valid(input[i]))
                        *outp << input[i];
                    *outp << ",";
                }

                //d->bytesWritten += bytes; // FIXME
            }

            *outp << "\n";

            d->eventsWritten++;
        }
    }
    catch (const std::exception &e)
    {
        std::ostringstream ss;
        ss << "Error writing to output file " << d->filename << ": " << e.what();
        d->setLastError(ss.str());
    }
}

void export_sink_end_run(Operator *op)
{
    a2_trace("\n");
    assert(op->type == Operator_ExportSinkFull
           || op->type == Operator_ExportSinkSparse
           || op->type == Operator_ExportSinkCsv
           );

    auto d = reinterpret_cast<ExportSinkData *>(op->d);

    // The destructors being called as a result of clearing the unique_ptrs
    // should not throw.
    d->z_ostream = {};
    d->ostream   = {};
}

/* ===============================================
 * A2 implementation
 * =============================================== */

namespace
{

struct OperatorFunctions
{
    using StepFunction      = void (*)(Operator *op, A2 *a2);
    using BeginRunFunction  = void (*)(Operator *op, Logger logger);
    using EndRunFunction    = void (*)(Operator *op);

    StepFunction step;
    BeginRunFunction begin_run = nullptr;
    EndRunFunction end_run = nullptr;
};

const std::array<OperatorFunctions, OperatorTypeCount> &get_operator_table()
{
    static std::array<OperatorFunctions, OperatorTypeCount> result = {};

    result[Invalid_OperatorType] = { nullptr };

    result[Operator_Calibration] = { calibration_step };
    result[Operator_Calibration_sse] = { calibration_sse_step };
    result[Operator_Calibration_idx] = { calibration_step_idx };
    result[Operator_KeepPrevious] = { keep_previous_step };
    result[Operator_KeepPrevious_idx] = { keep_previous_step_idx };
    result[Operator_Difference] = { difference_step };
    result[Operator_Difference_idx] = { difference_step_idx };
    result[Operator_ArrayMap] = { array_map_step };
    result[Operator_BinaryEquation] = { binary_equation_step };
    result[Operator_BinaryEquation_idx] = { binary_equation_step_idx };

    result[Operator_H1DSink] = { h1d_sink_step };
    result[Operator_H1DSink_idx] = { h1d_sink_step_idx };
    result[Operator_H2DSink] = { h2d_sink_step };

    result[Operator_RateMonitor_PrecalculatedRate] = { rate_monitor_step };
    result[Operator_RateMonitor_CounterDifference] = { rate_monitor_step };
    result[Operator_RateMonitor_FlowRate] = { rate_monitor_step };

    result[Operator_ExportSinkFull]   = { export_sink_full_step,   export_sink_begin_run, export_sink_end_run };
    result[Operator_ExportSinkSparse] = { export_sink_sparse_step, export_sink_begin_run, export_sink_end_run };
    result[Operator_ExportSinkCsv] = { export_sink_csv_step, export_sink_begin_run, export_sink_end_run };

    result[Operator_RangeFilter] = { range_filter_step };
    result[Operator_RangeFilter_idx] = { range_filter_step_idx };
    result[Operator_RectFilter] = { rect_filter_step };
    result[Operator_ConditionFilter] = { condition_filter_step };

    result[Operator_Aggregate_Sum] = { aggregate_sum_step };
    result[Operator_Aggregate_Multiplicity] = { aggregate_multiplicity_step };

    result[Operator_Aggregate_Min] = { aggregate_min_step };
    result[Operator_Aggregate_Max] = { aggregate_max_step };
    result[Operator_Aggregate_Mean] = { aggregate_mean_step };
    result[Operator_Aggregate_Sigma] = { aggregate_sigma_step };

    result[Operator_Aggregate_MinX] = { aggregate_minx_step };
    result[Operator_Aggregate_MaxX] = { aggregate_maxx_step };
    result[Operator_Aggregate_MeanX] = { aggregate_meanx_step };
    result[Operator_Aggregate_SigmaX] = { aggregate_sigmax_step };

    result[Operator_Expression] = { expression_operator_step };
    result[Operator_ScalerOverflow] = { scaler_overflow_step };
    result[Operator_ScalerOverflow_idx] = { scaler_overflow_step_idx };

    result[Operator_ConditionInterval] = { condition_interval_step };
    result[Operator_ConditionRectangle] = { condition_rectangle_step };
    result[Operator_ConditionPolygon] = { condition_polygon_step };

    return result;
}

} // end anon namespace

A2::A2(memory::Arena *arena)
    : conditionBits(BitsetAllocator(arena))
{
    //fprintf(stderr, "%s@%p\n", __PRETTY_FUNCTION__, this);

    dataSourceCounts.fill(0);
    dataSources.fill(nullptr);
    operatorCounts.fill(0);
    operators.fill(nullptr);
    operatorRanks.fill(0);
}

A2::~A2()
{
    //fprintf(stderr, "%s@%p\n", __PRETTY_FUNCTION__, this);
}

A2 *make_a2(
    Arena *arena,
    std::initializer_list<u8> dataSourceCounts,
    std::initializer_list<u8> operatorCounts)
{
    assert(dataSourceCounts.size() < MaxVMEEvents);
    assert(operatorCounts.size() < MaxVMEEvents);

    auto result = arena->pushObject<A2>(arena);

    const u8 *ec = dataSourceCounts.begin();

    for (size_t ei = 0; ei < dataSourceCounts.size(); ++ei, ++ec)
    {
        //printf("%s: %lu -> %u\n", __PRETTY_FUNCTION__, ei, (u32)*ec);
        result->dataSources[ei] = arena->pushArray<DataSource>(*ec);
    }

    for (size_t ei = 0; ei < operatorCounts.size(); ++ei)
    {
        result->operators[ei] = arena->pushArray<Operator>(operatorCounts.begin()[ei]);
        result->operatorRanks[ei] = arena->pushArray<u8>(operatorCounts.begin()[ei]);
    }

    return result;
}

// run begin_event() on all sources for the given eventIndex
void a2_begin_event(A2 *a2, int eventIndex)
{
    assert(eventIndex < MaxVMEEvents);

    int srcCount = a2->dataSourceCounts[eventIndex];

    a2_trace("ei=%d, dataSources=%d\n", eventIndex, srcCount);

    for (int srcIdx = 0; srcIdx < srcCount; srcIdx++)
    {
        DataSource *ds = a2->dataSources[eventIndex] + srcIdx;

        switch (static_cast<DataSourceType>(ds->type))
        {
            case DataSource_Extractor:
                extractor_begin_event(ds);
                break;

            case DataSource_ListFilterExtractor:
                listfilter_extractor_begin_event(ds);
                break;

            case DataSource_MultiHitExtractor_ArrayPerHit:
            case DataSource_MultiHitExtractor_ArrayPerAddress:
                multihit_extractor_begin_event(ds);
                break;

#if 0
            case DataSource_Copy:
                datasource_copy_begin_event(ds);
                break;
#endif
        }
    }
}

// hand module data to all sources for eventIndex and moduleIndex
void a2_process_module_data(A2 *a2, int eventIndex, int moduleIndex, const u32 *data, u32 dataSize)
{
    assert(eventIndex < MaxVMEEvents);
    assert(moduleIndex < MaxVMEModules);

#ifndef NDEBUG
    int nprocessed = 0;
#endif

    const int srcCount = a2->dataSourceCounts[eventIndex];

    // State for the data consuming ListFilterExtractors
    const u32 *curPtr = data;
    const u32 *endPtr = data + dataSize;

    for (int srcIdx = 0; srcIdx < srcCount; srcIdx++)
    {
        DataSource *ds = a2->dataSources[eventIndex] + srcIdx;

        if (ds->moduleIndex != moduleIndex)
            continue;

        switch (static_cast<DataSourceType>(ds->type))
        {
            case DataSource_Extractor:
                {
                    extractor_process_module_data(ds, data, dataSize);
                } break;
            case DataSource_ListFilterExtractor:
                {
                    if (curPtr < endPtr)
                    {
                        curPtr = listfilter_extractor_process_module_data(ds, curPtr, endPtr - curPtr);
                    }
                } break;

            case DataSource_MultiHitExtractor_ArrayPerHit:
            case DataSource_MultiHitExtractor_ArrayPerAddress:
                multihit_extractor_process_module_data(ds, data, dataSize);
                break;
#if 0
            case DataSource_Copy:
                datasource_copy_process_module_data(ds, data, dataSize);
                break;
#endif
        }
#ifndef NDEBUG
        nprocessed++;
#endif
    }

#ifndef NDEBUG
    a2_trace("ei=%d, mi=%d, processed %d dataSources\n", eventIndex, moduleIndex, nprocessed);
#endif
}

inline u32 step_operator_range(Operator *first, Operator *last, A2 *a2)
{
    u32 opSteppedCount = 0;

    for (auto op = first; op < last; ++op)
    {
        a2_trace("    op@%p\n", op);

        assert(op);
        assert(op->type < get_operator_table().size());

        if (likely(op->type != Invalid_OperatorType))
        {
            assert(get_operator_table()[op->type].step);

            get_operator_table()[op->type].step(op, a2);
            opSteppedCount++;
        }
    }

    return opSteppedCount;
}

struct OperatorRangeWork
{
    Operator *begin = nullptr;
    Operator *end = nullptr;
};

static const s32 WorkQueueSize = 32;

struct OperatorRangeWorkQueue
{
    mpmc_bounded_queue<OperatorRangeWork> queue;
    //NonRecursiveBenaphore mutex;
    LightweightSemaphore taskSem;
    LightweightSemaphore tasksDoneSem;
    //std::atomic<int> tasksDone;

    using Guard = std::lock_guard<NonRecursiveBenaphore>;

    explicit OperatorRangeWorkQueue(size_t size)
        : queue(size)
    {}
};

void a2_begin_run(A2 *a2, Logger logger)
{
    // call begin_run functions stored in the OperatorTable
    for (s32 ei = 0; ei < MaxVMEEvents; ei++)
    {
        const int opCount = a2->operatorCounts[ei];

        for (int opIdx = 0; opIdx < opCount; opIdx++)
        {
            Operator *op = a2->operators[ei] + opIdx;

            assert(op);
            assert(op->type < get_operator_table().size());

            if (get_operator_table()[op->type].begin_run)
            {
                get_operator_table()[op->type].begin_run(op, logger);
            }
        }
    }

    a2->histoFillStrategy.begin_run(a2);
}

void a2_end_run(A2 *a2)
{
    a2->histoFillStrategy.end_run(a2);

    // call end_run functions stored in the OperatorTable
    for (s32 ei = 0; ei < MaxVMEEvents; ei++)
    {
        const int opCount = a2->operatorCounts[ei];

        for (int opIdx = 0; opIdx < opCount; opIdx++)
        {
            Operator *op = a2->operators[ei] + opIdx;

            assert(op);
            assert(op->type < get_operator_table().size());

            if (get_operator_table()[op->type].end_run)
            {
                get_operator_table()[op->type].end_run(op);
            }
        }
    }

    a2_trace("done");
    //fprintf(stderr, "a2::%s() done\n", __FUNCTION__);
}

// step operators for the eventIndex
// operators must be sorted by increasing rank otherwise the behavior is
// undefined
void a2_end_event(A2 *a2, int eventIndex)
{
    assert(eventIndex < MaxVMEEvents);

    const int opCount = a2->operatorCounts[eventIndex];
    Operator *operators = a2->operators[eventIndex];
    s32 opSteppedCount = 0;
    s32 opCondSkipped  = 0;

    a2_trace("ei=%d, stepping %d operators\n", eventIndex, opCount);

    for (int opIdx = 0; opIdx < opCount; opIdx++)
    {
        Operator *op = operators + opIdx;

        a2_trace("  op@%p\n", op);

        assert(op);
        assert(op->type < get_operator_table().size());

        if (likely(op->type != Invalid_OperatorType))
        {
            assert(get_operator_table()[op->type].step);

            if (op->conditionIndex >= 0)
            {
                assert(static_cast<size_t>(op->conditionIndex) < a2->conditionBits.size());
            }

            if (op->conditionIndex < 0
                || a2->conditionBits.test(op->conditionIndex))
            {
                // no active condition or the condition is true
                get_operator_table()[op->type].step(op, a2);
                opSteppedCount++;
            }
            else
            {
                // condition is false -> invalidate all outputs
                invalidate_outputs(op);
                opCondSkipped++;
            }
        }
        else
        {
            InvalidCodePath;
        }
    }

    assert(opSteppedCount + opCondSkipped == opCount);

    a2_trace("ei=%d, operators stepped=%d, condSkipped=%d\n",
             eventIndex, opSteppedCount, opCondSkipped);

    // condition debug output
    for (int opIdx = 0; opIdx < opCount; opIdx++)
    {
        Operator *op = operators + opIdx;

        if (is_condition_operator(*op))
        {
            auto d = reinterpret_cast<ConditionBaseData *>(op->d);
            u32 bitCount = get_number_of_condition_bits_used(*op);

            assert(d->firstBitIndex >= 0);

            a2_trace("ei=%d, condOp@%p, condType=%u, firstBit=%d, bitCount=%u:\n  ",
                     eventIndex, op, op->type, d->firstBitIndex, bitCount);


            for (u32 pos = d->firstBitIndex; pos < d->firstBitIndex + bitCount; pos++)
            {
                assert(pos < a2->conditionBits.size());
                a2_trace_np("%d, ", a2->conditionBits.test(pos));
            }

            a2_trace_np("\n");
        }
    }
}

void a2_timetick(A2 *a2)
{
    a2_trace("\n");

    for (int ei = 0; ei < MaxVMEEvents; ei++)
    {
        const int opCount = a2->operatorCounts[ei];

        for (int opIdx = 0; opIdx < opCount; opIdx++)
        {
            Operator *op = a2->operators[ei] + opIdx;

            assert(op);
            assert(op->type < get_operator_table().size());

            if (op->type == Operator_RateMonitor_FlowRate)
            {
                rate_monitor_sample_flow(op);
            }
        }
    }
}

/* Threaded histosink implementation.
-----------------------------------------------------------------------------

Push Histo1DSink or Histo2DSink work on a bounded queue. If the enqueue
blocks it means histo filling is slowing us down.

Somehow bundle more insert work together.

Then try to sort by increasing memory address of insertions.
Sort by histo address, then by bin number.
Start inserting.

Bundling.
-----------------------------

Collect lots of values to insert into the same sink (meaning array of H1D or a
single H2D).  Values are consecutive steps of the sinks input. Always the same
size.

Buffering is always bad in the case of low rates. Could introduce a clock to
the a2 system which would have to be periodically triggered between calls to
a2_begin_event() and a2_end_event(). On clock tick buffers would be flushed.

The clock tick could be set to 1.0s. We'd buffer up data in the system until
the buffers are capped and thus handed off to the workers, or the clock tick
interval has passed and the buffers are flushed.

In single step mode a clock tick would be generated after each step, not
periodically.

For a batch run no ticks would need to be generated until the very end. A
final tick is needed to flush the remaining buffer contents out.
This final tick should probably be done in Analysis::endRun().

If the analysis system takes longer than the set clock interval to process
one event, a single "late" tick is enough to make the event visible in the
histograms. No additional ticks need to be performed.

More bundling and buffering.
-----------------------------

Just collect all the work from all the sinks in a step.

Known at build time, fixed size at runtime:

The number of sinks. The histo addresses. The numbers of histos and their sizes.

Have space for N times the input size of a sink. Reserve that space for all the sinks.
Sort sinks by histo address and assign pointers into the sink buffer in that order.

So for each sink there's a begin pointer, a current pointer, the sinks size
and the constant N.

Sink buffer sizes:
- Histo1DSink: input.size * sizeof(double) * N
- Histo2DSink: 2 * sizeof(double) * N

On a2_end_event()

 */

} // namespace a2
