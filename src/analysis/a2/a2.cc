#include "a2_impl.h"

#include <cstdlib>
#include <random>
#include <cstdio>

#define ArrayCount(x) (sizeof(x) / sizeof(*x))

namespace a2
{

using namespace data_filter;
using namespace memory;

/* TODO list
 * - Add logic to force internal input/output vectors to be rounded up to a
 *   specific power of 2. This is needed to efficiently use vector instructions
 *   in the _step() loops I think.
 *
 * - Write a test for keep_previous_step().
 * - Try an extractor for single word filters. Use the same system as for
 *   operators: a global function table. This means that
 *   a2_process_module_data() has to do a lookup and dispatch instead of
 *   passing directly to the extractor.
 */

/* Alignment in bytes of all double vectors created by the system.
 * SSE requires 16 byte alignment (128 bit registers).
 * AVX wants 32 bytes (256 bit registers).
 */
static const size_t ParamVecAlignment = 32;





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

ParamVec push_param_vector(Arena *arena, u32 size)
{
    ParamVec result;

    result.data = arena->pushArray<double>(size, ParamVecAlignment);
    result.size = result.data ? size : 0;
    assert(is_aligned(result.data, ParamVecAlignment));

    return result;
};

ParamVec push_param_vector(Arena *arena, u32 size, double value)
{
    ParamVec result = push_param_vector(arena, size);
    fill(result, value);
    return result;
};

Extractor make_extractor(Arena *arena, MultiWordFilter filter, u32 requiredCompletions, u64 rngSeed)
{
    Extractor result = {};

    result.filter = filter;
    result.requiredCompletions = requiredCompletions;
    result.currentCompletions = 0;
    result.rng.seed(rngSeed);
    size_t addrCount = 1u << get_extract_bits(&result.filter, MultiWordFilter::CacheA);
    result.output = push_param_vector(arena, addrCount);

    return  result;
}

void extractor_begin_event(Extractor *ex)
{
    clear_completion(&ex->filter);
    ex->currentCompletions = 0;
    invalidate_all(ex->output);
}

static std::uniform_real_distribution<double> RealDist01(0.0, 1.0);

void extractor_process_module_data(Extractor *ex, const u32 *data, u32 size)
{
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
                u64 address = extract(&ex->filter, MultiWordFilter::CacheA);
                u64 value   = extract(&ex->filter, MultiWordFilter::CacheD);

                assert(address < static_cast<u64>(ex->output.size));

                if (!is_param_valid(ex->output.data[address]))
                {
                    ex->output.data[address] = value + RealDist01(ex->rng);
                }
            }

            clear_completion(&ex->filter);
        }
    }
}

void assign_input(Operator *op, PipeVectors input, s32 inputIndex)
{
    assert(inputIndex < op->inputCount);
    op->inputs[inputIndex] = input.data;
    op->inputLowerLimits[inputIndex] = input.lowerLimits;
    op->inputUpperLimits[inputIndex] = input.upperLimits;
}

void push_output_vectors(
    Arena *arena,
    Operator *op,
    s32 outputIndex,
    s32 size,
    double lowerLimit = 0.0,
    double upperLimit = 0.0)
{
    op->outputs[outputIndex] = push_param_vector(arena, size);
    op->outputLowerLimits[outputIndex] = push_param_vector(arena, size, lowerLimit);
    op->outputUpperLimits[outputIndex] = push_param_vector(arena, size, upperLimit);
}

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
    result.d = nullptr;

    return  result;
}

struct OperatorFunctions
{
    using StepFunction = void (*)(Operator *op);

    StepFunction step;
};

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

void calibration_step(Operator *op)
{
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

void calibration_step_sse(Operator *op)
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

struct KeepPreviousData
{
    ParamVec previousInput;
    u8 keepValid;
};

void keep_previous_step(Operator *op)
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

Operator make_difference(
    Arena *arena, PipeVectors inPipeA, PipeVectors inPipeB)
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

void difference_step(Operator *op)
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
#if 0
        double a = inputA[idx];
        double b = inputB[idx];

        if (is_param_valid(a) && is_param_valid(b))
        {
            op->outputs[0][idx] = a - b;
        }
        else
        {
            op->outputs[0][idx] = invalid_param();
        }
#else
        if (is_param_valid(inputA[idx]) && is_param_valid(inputB[idx]))
        {
            op->outputs[0][idx] = inputA[idx] - inputB[idx];
        }
        else
        {
            op->outputs[0][idx] = invalid_param();
        }
#endif
    }
}

/**
 * ArrayMap: Map elements of one or more input arrays to an output array.
 *
 * Can be used to concatenate multiple arrays and/or change the order of array
 * members.
 */

void array_map_step(Operator *op)
{
    auto d = reinterpret_cast<ArrayMapData *>(op->d);

    s32 mappingCount = d->mappings.size;

    for (s32 mi = 0; mi < mappingCount; mi++)
    {
        auto mapping = d->mappings[mi];
        op->outputs[0][mi] = op->inputs[mapping.inputIndex][mapping.paramIndex];
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
        result.outputLowerLimits[0][mi] = inputs[m.inputIndex].lowerLimits[m.paramIndex];
        result.outputUpperLimits[0][mi] = inputs[m.inputIndex].upperLimits[m.paramIndex];
    }

    result.d = d;

    return result;
}

using BinaryEquationFunction = void (*)(ParamVec a, ParamVec b, ParamVec out);

#define add_binary_equation(x) \
[](ParamVec a, ParamVec b, ParamVec o)\
{\
    for (s32 i = 0; i < a.size; ++i)\
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

static BinaryEquationFunction BinaryEquationFunctionTable[] =
{
    add_binary_equation(o[i] = a[i] + b[i]),

    add_binary_equation(o[i] = a[i] - b[i]),

    add_binary_equation(o[i] = (a[i] + b[i]) / (a[i] - b[i])),

    add_binary_equation(o[i] = (a[i] - b[i]) / (a[i] + b[i])),

    add_binary_equation(o[i] = a[i] / (a[i] - b[i])),

    add_binary_equation(o[i] = (a[i] - b[i]) / a[i]),
};
#undef add_binary_equation

static const size_t BinaryEquationCount = ArrayCount(BinaryEquationFunctionTable);

void binary_equation_step(Operator *op)
{
    // The equationIndex is stored directly in the d pointer.
    u32 equationIndex = (uintptr_t)op->d;

    BinaryEquationFunctionTable[equationIndex](
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
    auto result = make_operator(arena, Operator_BinaryEquation, 2, 1);

    assign_input(&result, inputA, 0);
    assign_input(&result, inputB, 1);

    push_output_vectors(arena, &result, 0, inputA.data.size,
                        outputLowerLimit, outputUpperLimit);

    result.d = (void *)(uintptr_t)equationIndex;

    return result;
}

/* Implementing AggregateOps
 * ===============================================
 *      enum Operation
 *      {
 *          Op_Sum,
 *          Op_Mean,
 *          Op_Sigma,
 *          Op_Min,
 *          Op_Max,
 *          Op_Multiplicity,
 *          Op_MinX,
 *          Op_MaxX,
 *          Op_MeanX,
 *          Op_SigmaX,
 *          NumOps
 *      };
 *
 */

//
// Histograms
//

enum class Axis
{
    X,
    Y,
};

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

inline void fill_h1d(H1D *histo, double x)
{
    /* Instead of calculating the bin and then checking if it under/overflows
     * this code decides by comparing x to the binnings min and max values.
     * This is faster. */
    if (x < histo->binning.min)
    {
        assert(get_bin(*histo, x) == Binning::Underflow);
        histo->underflow++;
    }
    else if (x >= histo->binning.min + histo->binning.range)
    {
        assert(get_bin(*histo, x) == Binning::Overflow);
        histo->overflow++;
    }
    else
    {
        assert(0 <= get_bin(*histo, x) && get_bin(*histo, x) < histo->size);

        //s32 bin = static_cast<s32>(get_bin_unchecked(histo->binning, histo->size, x));
        s32 bin = static_cast<s32>(get_bin_unchecked(x, histo->binning.min, histo->binningFactor));

        double value = ++histo->data[bin];
        histo->entryCount++;

        if (value > histo->maxValue)
        {
            histo->maxValue = value;
            histo->maxBin = bin;
        }
    }
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
    histo->maxValue = 0.0;
    histo->maxBin = 0.0;
    histo->underflow = 0.0;
    histo->overflow = 0.0;
    for (s32 i = 0; i < histo->size; i++)
    {
        histo->data[i] = 0.0;
    }
}

/* Note: Histos are copied. This means during runtime only the H1D structures
 * inside H1DSinkData are updated. Histogram storage itself is not copied. It
 * is assumed that this storage is handled separately.
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
    d->histos = push_typed_block<H1D, s32>(arena, histos.size);
    for (s32 i = 0; i < histos.size; i++)
    {
        d->histos[i] = histos[i];
    }
    result.d = d;

    return result;
}

void h1d_sink_step(Operator *op)
{
    auto d = reinterpret_cast<H1DSinkData *>(op->d);
    s32 maxIdx = op->inputs[0].size;

    for (s32 idx = 0; idx < maxIdx; idx++)
    {
        if (is_param_valid(op->inputs[0][idx]))
        {
            fill_h1d(&d->histos[idx], op->inputs[0][idx]);
        }
        else
        {
            // could keep track of the invalid count here if needed
        }
    }
}

struct H2D: public ParamVec
{
    Binning binnings[2];
};

static OperatorFunctions OperatorFunctionTable[OperatorTypeMax] =
{
    [Operator_Calibration] = { calibration_step },
    [Operator_Calibration_sse] = { calibration_step_sse },
    [Operator_KeepPrevious] = { keep_previous_step },
    [Operator_Difference] = { difference_step },
    [Operator_ArrayMap] = { array_map_step },
    [Operator_BinaryEquation] = { binary_equation_step },
    [Operator_AggregateOps] = { nullptr },
    [Operator_H1DSink] = { h1d_sink_step },
    [Operator_H2DSink] = { nullptr },
};

inline void step_operator(Operator *op)
{
    OperatorFunctionTable[op->type].step(op);
}

static const int MaxVMEEvents  = 12;
static const int MaxVMEModules = 20;

struct A2
{
    std::array<u8, MaxVMEEvents> extractorCounts;
    std::array<Extractor *, MaxVMEEvents> extractors;

    std::array<u8, MaxVMEEvents> operatorCounts;
    std::array<Operator *, MaxVMEEvents> operators;
    std::array<u8 *, MaxVMEEvents> operatorRanks;
};

A2 make_a2(
    Arena *arena,
    std::initializer_list<u8> extractorCounts,
    std::initializer_list<u8> operatorCounts)
{
    assert(extractorCounts.size() < MaxVMEEvents);
    assert(operatorCounts.size() < MaxVMEEvents);

    A2 result = {};

    result.extractorCounts.fill(0);
    result.extractors.fill(nullptr);
    result.operatorCounts.fill(0);
    result.operators.fill(nullptr);
    result.operatorRanks.fill(0);

    const u8 *ec = extractorCounts.begin();

    for (size_t ei = 0; ei < extractorCounts.size(); ++ei, ++ec)
    {
        //printf("%s: %lu -> %u\n", __PRETTY_FUNCTION__, ei, (u32)*ec);
        result.extractors[ei] = arena->pushArray<Extractor>(*ec);
    }

    for (size_t ei = 0; ei < operatorCounts.size(); ++ei)
    {
        result.operators[ei] = arena->pushArray<Operator>(operatorCounts.begin()[ei]);
        result.operatorRanks[ei] = arena->pushArray<u8>(operatorCounts.begin()[ei]);
    }

    return result;
}

// run begin_event() on all sources for the given eventIndex
void a2_begin_event(A2 *a2, int eventIndex)
{
    assert(eventIndex < MaxVMEEvents);
    int exCount = a2->extractorCounts[eventIndex];
    for (int exIdx = 0; exIdx < exCount; exIdx++)
    {
        Extractor *ex = a2->extractors[eventIndex] + exIdx;
        extractor_begin_event(ex);
    }
}

// hand module data to all sources for eventIndex and moduleIndex
void a2_process_module_data(A2 *a2, int eventIndex, int moduleIndex, const u32 *data, u32 dataSize)
{
    assert(eventIndex < MaxVMEEvents);
    assert(moduleIndex < MaxVMEModules);

    int exCount = a2->extractorCounts[eventIndex];

    for (int exIdx = 0; exIdx < exCount; exIdx++)
    {
        Extractor *ex = a2->extractors[eventIndex] + exIdx;
        if (ex->moduleIndex == moduleIndex)
        {
            extractor_process_module_data(ex, data, dataSize);
        }
        else if (ex->moduleIndex > moduleIndex)
        {
            break;
        }
    }
}

// step operators for the eventIndex
// operators must be sorted by rank
void a2_end_event(A2 *a2, int eventIndex)
{
    assert(eventIndex < MaxVMEEvents);

    int opCount = a2->operatorCounts[eventIndex];
    Operator *operators = a2->operators[eventIndex];

    for (int opIdx = 0; opIdx < opCount; opIdx++)
    {
        Operator *op = operators + opIdx;
        OperatorFunctionTable[op->type].step(op);
    }
}

} // namespace a2
