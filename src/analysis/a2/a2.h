#ifndef __MVME_A2_H__
#define __MVME_A2_H__

#ifdef liba2_shared_EXPORTS
#include "a2_export.h"
#endif
#include "memory.h"
#include "multiword_datafilter.h"
#include "util/nan.h"

#include <cassert>
#include <pcg_random.hpp>

namespace a2
{
/* Note: a2 passes out_of_memory() from memory::Arena through. Use an external
 * mechanism to catch this, increase the memory size and try again. */


/* Bit used as payload of NaN values to identify an invalid parameter.
 * If the bit is not set the NaN was generated as the result of a calculation
 * and the parameter is considered valid.
 */
static const int ParamInvalidBit = 1u << 0;

inline bool is_param_valid(double param)
{
    return !(std::isnan(param) && (get_payload(param) & ParamInvalidBit));
}

inline double invalid_param()
{
    static const double result = make_nan(ParamInvalidBit);
    return result;
}

template<typename T, typename SizeType = size_t>
struct TypedBlock
{
    typedef SizeType size_type;
    static constexpr auto size_max = std::numeric_limits<SizeType>::max();

    T *data;
    size_type size;

    inline T operator[](size_type index) const
    {
        return data[index];
    }

    inline T &operator[](size_type index)
    {
        return data[index];
    }

    inline T *begin()
    {
        return data;
    }

    inline T *end()
    {
        return data + size;
    }
};

template<typename T, typename SizeType = size_t>
TypedBlock<T, SizeType> push_typed_block(
    memory::Arena *arena,
    SizeType size,
    size_t align = alignof(T))
{
    TypedBlock<T, SizeType> result;

    result.data = arena->pushArray<T>(size, align);
    result.size = result.data ? size : 0;
    assert(memory::is_aligned(result.data, align));

    return result;
};

template<typename T, typename SizeType = size_t>
TypedBlock<T, SizeType> make_typed_block(
    T *data,
    SizeType size)
{
    TypedBlock<T, SizeType> result;
    result.data = data;
    result.size = size;
    return result;
}

using ParamVec = TypedBlock<double, s32>;

void print_param_vector(ParamVec pv);

inline void fill(ParamVec pv, double value)
{
    for (s32 i = 0; i < pv.size; ++i)
    {
        pv.data[i] = value;
    }
}

inline void invalidate_all(ParamVec pv)
{
    fill(pv, invalid_param());
}

inline void invalidate_all(double *params, s32 count)
{
    invalidate_all({params, count});
}

ParamVec push_param_vector(memory::Arena *arena, s32 size);
ParamVec push_param_vector(memory::Arena *arena, s32 size, double value);

struct Thresholds
{
    double min;
    double max;
};

inline bool in_range(Thresholds t, double v)
{
    return (t.min <= v && v < t.max);
}

struct PipeVectors
{
    ParamVec data;
    ParamVec lowerLimits;
    ParamVec upperLimits;
};

struct Extractor
{
    data_filter::MultiWordFilter filter;
    pcg32_fast rng;
    PipeVectors output;
    ParamVec hitCounts;
    u32 requiredCompletions;
    u32 currentCompletions;
    u8 moduleIndex;
};

Extractor make_extractor(
    memory::Arena *arena,
    data_filter::MultiWordFilter filter,
    u32 requiredCompletions,
    u64 rngSeed,
    int moduleIndex);

struct Operator
{
    using count_type = u8;

    static const auto MaxInputCount = std::numeric_limits<count_type>::max();
    static const auto MaxOutputCount = std::numeric_limits<count_type>::max();

    ParamVec *inputs;
    ParamVec *inputLowerLimits;
    ParamVec *inputUpperLimits;
    ParamVec *outputs;
    ParamVec *outputLowerLimits;
    ParamVec *outputUpperLimits;
    void *d;
    u8 inputCount;
    u8 outputCount;
    u8 type;
};

void assign_input(Operator *op, PipeVectors input, s32 inputIndex);
void extractor_begin_event(Extractor *ex);
void extractor_process_module_data(Extractor *ex, const u32 *data, u32 size);

Operator make_calibration(
    memory::Arena *arena,
    PipeVectors input,
    double unitMin, double unitMax);

Operator make_calibration(
    memory::Arena *arena,
    PipeVectors input,
    ParamVec calibMinimums,
    ParamVec calibMaximums);

Operator make_keep_previous(
    memory::Arena *arena,
    PipeVectors input,
    bool keepValid);

Operator make_keep_previous_idx(
    memory::Arena *arena,
    PipeVectors input,
    s32 inputIndex,
    bool keepValid);

Operator make_difference(
    memory::Arena *arena,
    PipeVectors inPipeA,
    PipeVectors inPipeB);

Operator make_difference_idx(
    memory::Arena *arena,
    PipeVectors inPipeA,
    PipeVectors inPipeB,
    s32 indexA,
    s32 indexB);

struct ArrayMapData
{
    struct Mapping
    {
        u8 inputIndex;
        s32 paramIndex;
    };

    TypedBlock<Mapping, s32> mappings;
};

Operator make_array_map(
    memory::Arena *arena,
    TypedBlock<PipeVectors, s32> inputs,
    TypedBlock<ArrayMapData::Mapping, s32> mappings);

Operator make_binary_equation(
    memory::Arena *arena,
    PipeVectors inputA,
    PipeVectors inputB,
    u32 equationIndex, // stored right inside the d pointer so it can be at least u32 in size
    double outputLowerLimit,
    double outputUpperLimit);

Operator make_range_filter(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds,
    bool invert);

Operator make_range_filter_idx(
    memory::Arena *arena,
    PipeVectors input,
    s32 inputIndex,
    Thresholds thresholds,
    bool invert);


enum class RectFilterOperation
{
    And,
    Or
};

Operator make_rect_filter(
    memory::Arena *arena,
    PipeVectors xInput,
    PipeVectors yInput,
    s32 xIndex,
    s32 yIndex,
    Thresholds xThresholds,
    Thresholds yThresholds,
    RectFilterOperation filterOp);

Operator make_condition_filter(
    memory::Arena *arena,
    PipeVectors dataInput,
    PipeVectors condInput,
    s32 dataIndex = -1,
    s32 condIndex = -1);

/* ===============================================
 * AggregateOps
 * =============================================== */

/* Thresholds to check each input parameter against. If the parameter is not
 * inside [min_threshold, max_threshold] it is not considered for the result.
 */

Operator make_aggregate_sum(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_multiplicity(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_min(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_max(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_sigma(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_mean(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_minx(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_maxx(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_meanx(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_sigmax(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

/* ===============================================
 * Histograms
 * =============================================== */
struct Binning
{
    static const s8 Underflow = -1;
    static const s8 Overflow = -2;
    double min;
    double range;
};

struct H1D: public ParamVec
{
    Binning binning;
    // binningFactor = binCount / binning.range
    double binningFactor;
    double entryCount;
    double underflow;
    double overflow;
};

Operator make_h1d_sink(
    memory::Arena *arena,
    PipeVectors inPipe,
    TypedBlock<H1D, s32> histos);

Operator make_h1d_sink_idx(
    memory::Arena *arena,
    PipeVectors inPipe,
    TypedBlock<H1D, s32> histos,
    s32 inputIndex);

struct H1DSinkData
{
    TypedBlock<H1D, s32> histos;
};

struct H1DSinkData_idx: public H1DSinkData
{
    s32 inputIndex;
};

struct H2D: public ParamVec
{
    enum Axis
    {
        XAxis,
        YAxis,
        AxisCount
    };

    s32 binCounts[AxisCount];
    Binning binnings[AxisCount];
    double binningFactors[AxisCount];
    double entryCount;
    double underflow;
    double overflow;
};

struct H2DSinkData
{
    H2D histo;
    s32 xIndex;
    s32 yIndex;
};

Operator make_h2d_sink(
    memory::Arena *arena,
    PipeVectors xInput,
    PipeVectors yInput,
    s32 xIndex,
    s32 yIndex,
    H2D histo);

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

void a2_begin_run(A2 *a2);
void a2_begin_event(A2 *a2, int eventIndex);
void a2_process_module_data(A2 *a2, int eventIndex, int moduleIndex, const u32 *data, u32 dataSize);
void a2_end_event(A2 *a2, int eventIndex);
void a2_end_run(A2 *a2);

//
// Stuff used for debugging and tests
//

template<typename Out, typename T>
void write_value(Out &out, T value)
{
    out.write(reinterpret_cast<const char *>(&value), sizeof(T));
}

template<typename Out, typename T>
void write_array(Out &out, T *data, size_t size)
{
    out.write(reinterpret_cast<const char *>(data), size * sizeof(T));
}
template<typename Out>
void write_histo(Out &out, H1D histo)
{
    // s32 size
    // double binning.min
    // double binning.range
    // double underflow
    // double overflow
    // double data[size]

    write_value(out, histo.size);
    write_value(out, histo.binning.min);
    write_value(out, histo.binning.range);
    write_value(out, histo.underflow);
    write_value(out, histo.overflow);
    write_array(out, histo.data, histo.size);
}

template<typename Out>
void write_histo_list(Out &out, TypedBlock<H1D, s32> histos)
{
    // s32 histoCount
    write_value(out, histos.size);

    for (s32 i = 0; i < histos.size; i++)
    {
        write_histo(out, histos[i]);
    }
}

} // namespace a2

#endif /* __MVME_A2_H__ */
