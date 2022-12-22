#include "a2_adapter.h"
#include "a2.h"
#include "a2/a2_impl.h"
#include "analysis.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include <QMetaObject>
#include <QMetaClassInfo>

#ifndef NDEBUG
//#if 0

#define LOG(fmt, ...)\
do\
{\
    fprintf(stderr, "a2_adapter::%s() " fmt, __FUNCTION__, ##__VA_ARGS__);\
    fprintf(stderr, "\n");\
} while (0)

// NoNewline version
#define LOGNN(fmt, ...)\
do\
{\
    fprintf(stderr, "a2_adapter::%s() " fmt, __FUNCTION__, ##__VA_ARGS__);\
} while (0)

#else

#define LOG(...)
#define LOGNN(...)

#endif

namespace
{

//#ifndef NDEBUG
#if 0
inline QDebug a2_adapter_qlog(const char *func)
{
    return (qDebug().nospace() << "a2_adapter::" << func << "()").space();
}

#define QLOG(x) a2_adapter_qlog(__FUNCTION__) << x
#else
#define QLOG(x)
#endif

using analysis::A2AdapterState;

inline std::pair<a2::PipeVectors, bool>
find_output_pipe(
    const A2AdapterState *state,
    const analysis::PipeSourceInterface *pipeSource,
    u8 outputIndex)
{
    //QLOG("pipeSource to find:" << pipeSource.get() << ", outputIndex=" << (u32)outputIndex);

    if (a2::DataSource *ds_a2 = state->sourceMap.value(
            qobject_cast<analysis::SourceInterface *>(pipeSource),
            nullptr))
    {
        assert(outputIndex < ds_a2->outputCount);
        a2::PipeVectors result = {};
        result.data = ds_a2->outputs[outputIndex];
        result.lowerLimits = ds_a2->outputLowerLimits[outputIndex];
        result.upperLimits = ds_a2->outputUpperLimits[outputIndex];
        return std::make_pair(result, true);
    }
    else if (a2::Operator *op_a2 = state->operatorMap.value(
            qobject_cast<analysis::OperatorInterface *>(pipeSource),
            nullptr))
    {
        if (op_a2->type != a2::Invalid_OperatorType)
        {
            assert(outputIndex < op_a2->outputCount);

            a2::PipeVectors result = {};
            result.data = op_a2->outputs[outputIndex];
            result.lowerLimits = op_a2->outputLowerLimits[outputIndex];
            result.upperLimits = op_a2->outputUpperLimits[outputIndex];
            return std::make_pair(result, true);
        }
        else
        {
            QLOG("invalid operator type");
            return std::make_pair(a2::PipeVectors(), false);
        }
    }
    else
    {
        //QLOG(pipeSource << pipeSource->getId());
        //assert(!"no source mapping");
    }

    return std::make_pair(a2::PipeVectors(), false);
}

inline std::pair<a2::PipeVectors, bool>
find_output_pipe(const A2AdapterState *state, analysis::Slot *slot)
{
    return find_output_pipe(
        state,
        slot->inputPipe->source,
        slot->inputPipe->sourceOutputIndex);
}

#define assert_slot(slot)                                   \
do                                                          \
{                                                           \
    assert(slot->inputPipe);                                \
    assert(slot->inputPipe->source);                        \
    assert(slot->inputPipe->source->shared_from_this());    \
} while (0)

using InputSlots = QVector<analysis::Slot *>;
using OutputPipes = QVector<analysis::Pipe *>;

#define DEF_OP_MAGIC(name) a2::Operator name(\
    memory::Arena *arena,\
    A2AdapterState *adapterState,\
    analysis::OperatorPtr op,\
    InputSlots inputSlots,\
    OutputPipes outputPipes,\
    const RunInfo &runInfo)

typedef DEF_OP_MAGIC(OperatorMagic);

#define OP_MAGIC_NOWARN\
    (void) arena;\
    (void) adapterState;\
    (void) op;\
    (void) inputSlots;\
    (void) outputPipes;\
    (void) runInfo;

DEF_OP_MAGIC(calibration_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");
    assert(inputSlots.size() == 1);
    assert_slot(inputSlots[0]);

    auto calib = qobject_cast<analysis::CalibrationMinMax *>(op.get());

    assert(calib);

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]).first;

    auto calibs = calib->getCalibrations();

    QVector<double> calibMinimums(calibs.size());
    QVector<double> calibMaximums(calibs.size());

    for (s32 i = 0; i < calibs.size(); i++)
    {
        calibMinimums[i] = calibs[i].unitMin;
        calibMaximums[i] = calibs[i].unitMax;
    }

    a2::Operator result = {};

    if (inputSlots[0]->paramIndex == analysis::Slot::NoParamIndex)
    {
        result = a2::make_calibration(
            arena,
            a2_input,
            { calibMinimums.data(), calibMinimums.size() },
            { calibMaximums.data(), calibMaximums.size() });
    }
    else
    {
        assert(calibMinimums.size() > 0);
        assert(calibMaximums.size() > 0);

        result = a2::make_calibration_idx(
            arena,
            a2_input,
            inputSlots[0]->paramIndex,
            calibMinimums[0],
            calibMaximums[0]);
    }

    return result;
};

DEF_OP_MAGIC(difference_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");
    assert(inputSlots.size() == 2);
    assert_slot(inputSlots[0]);
    assert_slot(inputSlots[1]);

    [[maybe_unused]] auto diff = qobject_cast<analysis::Difference *>(op.get());

    assert(diff);

    auto a2_inputA = find_output_pipe(adapterState, inputSlots[0]).first;
    auto a2_inputB = find_output_pipe(adapterState, inputSlots[1]).first;

    a2::Operator result = {};

    if (inputSlots[0]->paramIndex == analysis::Slot::NoParamIndex)
    {
        // Both inputs are arrays
        assert(inputSlots[1]->paramIndex == analysis::Slot::NoParamIndex);
        assert(inputSlots[0]->acceptedInputTypes & analysis::InputType::Array);
        assert(inputSlots[1]->acceptedInputTypes & analysis::InputType::Array);
        result = make_difference(arena, a2_inputA, a2_inputB);
    }
    else
    {
        // Both inputs are indexed
        assert(inputSlots[0]->paramIndex != analysis::Slot::NoParamIndex);
        assert(inputSlots[0]->paramIndex < a2_inputA.data.size);
        assert(inputSlots[1]->paramIndex != analysis::Slot::NoParamIndex);
        assert(inputSlots[1]->paramIndex < a2_inputB.data.size);

        result = make_difference_idx(
            arena,
            a2_inputA,
            a2_inputB,
            inputSlots[0]->paramIndex,
            inputSlots[1]->paramIndex
            );
    }

    return result;
};

template<typename T, typename SizeType = size_t>
struct QVectorBlock
{
    TypedBlock<T, SizeType> block;
    QVector<T> store;
};

DEF_OP_MAGIC(array_map_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto arrayMap = qobject_cast<analysis::ArrayMap *>(op.get());
    assert(arrayMap);

    QVector<a2::PipeVectors> a2_inputs(inputSlots.size());

    for (s32 si = 0; si < inputSlots.size(); si++)
    {
        a2_inputs[si] = find_output_pipe(adapterState, inputSlots[si]).first;
    }

    auto mappings = arrayMap->m_mappings;

    QVector<a2::ArrayMapData::Mapping> a2_data(mappings.size());

    for (s32 mi = 0; mi < mappings.size(); mi++)
    {
        assert(mappings[mi].slotIndex <= std::numeric_limits<u8>::max());

        a2::ArrayMapData::Mapping a2_mapping;
        a2_mapping.inputIndex = static_cast<u8>(mappings[mi].slotIndex);
        a2_mapping.paramIndex = mappings[mi].paramIndex;

        a2_data[mi] = a2_mapping;
    }


    a2::Operator result = a2::make_array_map(
        arena,
        { a2_inputs.data(), a2_inputs.size() },
        { a2_data.data(), a2_data.size() });

    return result;
}

DEF_OP_MAGIC(aggregate_ops_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    using analysis::AggregateOps;
    auto agOps = qobject_cast<AggregateOps *>(op.get());
    assert(agOps);

    a2::Thresholds thresholds =
    {
        agOps->getMinThreshold(),
        agOps->getMaxThreshold()
    };

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]).first;

    a2::Operator result = {};

    switch (agOps->getOperation())
    {
        case AggregateOps::Op_Sum:
            result = make_aggregate_sum(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_Mean:
            result = make_aggregate_mean(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_Min:
            result = make_aggregate_min(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_Max:
            result = make_aggregate_max(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_Multiplicity:
            result = make_aggregate_multiplicity(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_Sigma:
            result = make_aggregate_sigma(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_MinX:
            result = make_aggregate_minx(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_MaxX:
            result = make_aggregate_maxx(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_MeanX:
            result = make_aggregate_meanx(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_SigmaX:
            result = make_aggregate_sigmax(arena, a2_input, thresholds);
            break;

        default:
            qDebug() << "analysis::AggregateOps::Operation =" << agOps->getOperation();
            assert(!"unsupported AggregateOps::Operation");
    }

    return result;
}

/* Maps analysis::Sum to a2::Operator_Aggregate_Sum or
 * a2::Operator_Aggregate_Mean depending on the setting of
 * analysis::Sum::m_calculateMean. The thresholds are set to NaN as the Sum
 * operator doesn't have thresholds. */
DEF_OP_MAGIC(sum_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto sumOp = qobject_cast<analysis::Sum *>(op.get());
    assert(sumOp);

    a2::Thresholds thresholds =
    {
        make_quiet_nan(),
        make_quiet_nan()
    };

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]).first;

    a2::Operator result = {};

    if (sumOp->m_calculateMean)
    {
        result = make_aggregate_mean(arena, a2_input, thresholds);
    }
    else
    {
        result = make_aggregate_sum(arena, a2_input, thresholds);
    }

    return result;
}

DEF_OP_MAGIC(binary_equation_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto binSumDiff = qobject_cast<analysis::BinarySumDiff *>(op.get());
    assert(binSumDiff);

    auto a2_inputA = find_output_pipe(adapterState, inputSlots[0]).first;
    auto a2_inputB = find_output_pipe(adapterState, inputSlots[1]).first;

    a2::Operator result = {};

    if (inputSlots[0]->isParameterConnection())
    {
        result = make_binary_equation_idx(
            arena,
            a2_inputA,
            a2_inputB,
            inputSlots[0]->paramIndex,
            inputSlots[1]->paramIndex,
            binSumDiff->getEquation(),
            binSumDiff->getOutputLowerLimit(),
            binSumDiff->getOutputUpperLimit());
    }
    else
    {
        result = make_binary_equation(
            arena,
            a2_inputA,
            a2_inputB,
            binSumDiff->getEquation(),
            binSumDiff->getOutputLowerLimit(),
            binSumDiff->getOutputUpperLimit());
    }

    return result;
}

DEF_OP_MAGIC(keep_previous_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");
    assert(inputSlots.size() == 1);
    assert_slot(inputSlots[0]);

    auto prevValue = qobject_cast<analysis::PreviousValue *>(op.get());

    assert(prevValue);

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]).first;

    a2::Operator result = {};

    if (inputSlots[0]->paramIndex == analysis::Slot::NoParamIndex)
    {
        result = a2::make_keep_previous(
            arena,
            a2_input,
            prevValue->m_keepValid);
    }
    else
    {
        result = a2::make_keep_previous_idx(
            arena,
            a2_input,
            inputSlots[0]->paramIndex,
            prevValue->m_keepValid);
    }

    return result;
}

DEF_OP_MAGIC(range_filter_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");
    assert(inputSlots.size() == 1);
    assert_slot(inputSlots[0]);

    auto rangeFilter = qobject_cast<analysis::RangeFilter1D *>(op.get());

    assert(rangeFilter);

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]).first;

    a2::Operator result = {};

    if (inputSlots[0]->paramIndex == analysis::Slot::NoParamIndex)
    {
        result = a2::make_range_filter(
            arena,
            a2_input,
            { rangeFilter->m_minValue, rangeFilter->m_maxValue },
            rangeFilter->m_keepOutside);
    }
    else
    {
        result = a2::make_range_filter_idx(
            arena,
            a2_input,
            inputSlots[0]->paramIndex,
            { rangeFilter->m_minValue, rangeFilter->m_maxValue },
            rangeFilter->m_keepOutside);
    }

    return result;
}

DEF_OP_MAGIC(rect_filter_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");
    assert(inputSlots.size() == 2);
    assert_slot(inputSlots[0]);
    assert_slot(inputSlots[1]);
    assert(inputSlots[0]->paramIndex != analysis::Slot::NoParamIndex);
    assert(inputSlots[1]->paramIndex != analysis::Slot::NoParamIndex);

    auto rectFilter = qobject_cast<analysis::RectFilter2D *>(op.get());

    assert(rectFilter);

    auto a2_xInput = find_output_pipe(adapterState, inputSlots[0]).first;
    auto a2_yInput = find_output_pipe(adapterState, inputSlots[1]).first;

    assert(inputSlots[0]->paramIndex < a2_xInput.data.size);
    assert(inputSlots[1]->paramIndex < a2_yInput.data.size);

    s32 xIndex = inputSlots[0]->paramIndex;
    s32 yIndex = inputSlots[1]->paramIndex;

    a2::Thresholds xThresholds =
    {
        rectFilter->getXInterval().minValue(),
        rectFilter->getXInterval().maxValue()
    };

    a2::Thresholds yThresholds =
    {
        rectFilter->getYInterval().minValue(),
        rectFilter->getYInterval().maxValue()
    };

    a2::RectFilterOperation filterOp = (rectFilter->getConditionOp() == analysis::RectFilter2D::OpAnd
                                    ? a2::RectFilterOperation::And
                                    : a2::RectFilterOperation::Or);

    a2::Operator result = make_rect_filter(
        arena,
        a2_xInput,
        a2_yInput,
        xIndex,
        yIndex,
        xThresholds,
        yThresholds,
        filterOp);

    return result;
}

DEF_OP_MAGIC(condition_filter_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");
    assert(inputSlots.size() == 2);
    assert_slot(inputSlots[0]);
    assert_slot(inputSlots[1]);

    auto condFilter = qobject_cast<analysis::ConditionFilter *>(op.get());

    assert(condFilter);

    auto a2_dataInput = find_output_pipe(adapterState, inputSlots[0]).first;
    auto a2_condInput = find_output_pipe(adapterState, inputSlots[1]).first;

    s32 dataIndex = inputSlots[0]->paramIndex;
    s32 condIndex = inputSlots[1]->paramIndex;

    a2::Operator result = make_condition_filter(
        arena,
        a2_dataInput,
        a2_condInput,
        condFilter->m_invertedCondition,
        dataIndex,
        condIndex);

    return result;
}

DEF_OP_MAGIC(expression_operator_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto a1_op = qobject_cast<analysis::ExpressionOperator *>(op.get());

    assert(a1_op);

    std::vector<a2::PipeVectors> a2_inputs;
    std::vector<std::string> inputUnits;
    std::vector<std::string> inputPrefixes;
    std::vector<s32> inputIndexes;

    for (s32 si = 0; si < inputSlots.size(); si++)
    {
        a2_inputs.emplace_back(find_output_pipe(adapterState, inputSlots[si]).first);
        inputUnits.emplace_back(inputSlots[si]->inputPipe->parameters.unit.toStdString());
        inputPrefixes.emplace_back(a1_op->getInputPrefix(si).toStdString());
        inputIndexes.emplace_back(inputSlots[si]->paramIndex);
    }

    a2::Operator result = {};

    try
    {
        result = make_expression_operator(
            arena,
            a2_inputs,
            inputIndexes,
            inputPrefixes,
            inputUnits,
            a1_op->getBeginExpression().toStdString(),
            a1_op->getStepExpression().toStdString());
    } catch (const std::runtime_error &e)
    {
        LOG("EE Exception when making operator: %s", e.what());
        throw;
    }

    return result;
}

DEF_OP_MAGIC(scaler_overflow_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    [[maybe_unused]] auto a1_op = qobject_cast<analysis::ScalerOverflow *>(op.get());

    assert(a1_op);

    a2::PipeVectors a2_input = find_output_pipe(adapterState, inputSlots[0]).first;

    a2::Operator result = {};

    if (inputSlots[0]->paramIndex == analysis::Slot::NoParamIndex)
    {
        result = a2::make_scaler_overflow(arena, a2_input);
    }
    else
    {
        result = a2::make_scaler_overflow_idx(arena, a2_input, inputSlots[0]->paramIndex);
    }

    return result;
}

//
// Sinks
//

DEF_OP_MAGIC(histo1d_sink_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");
    assert(inputSlots.size() == 1);
    assert_slot(inputSlots[0]);

    auto histoSink = qobject_cast<analysis::Histo1DSink *>(op.get());

    assert(histoSink);

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]).first;

    QVector<a2::H1D> histos(histoSink->m_histos.size());

    for (s32 i = 0; i < histos.size(); i++)
    {
        auto histo = histoSink->m_histos[i].get();

        assert(histo->getNumberOfBins() < a2::H1D::size_max);

        a2::H1D a2_histo = {};
        a2_histo.data = histo->data();
        a2_histo.size = histo->getNumberOfBins();
        a2_histo.binning.min = histo->getXMin();
        a2_histo.binning.range = histo->getXMax() - histo->getXMin();
        // binningFactor = binCount / binning.range
        a2_histo.binningFactor = a2_histo.size / a2_histo.binning.range;
        a2_histo.entryCount = histo->getEntryCountPtr();
        a2_histo.underflow = histo->getUnderflowPtr();
        a2_histo.overflow = histo->getOverflowPtr();

        histos[i] = a2_histo;
    }

    a2::Operator result = {};

    if (inputSlots[0]->paramIndex == analysis::Slot::NoParamIndex)
    {
        result = a2::make_h1d_sink(
            arena,
            a2_input,
            { histos.data(), histos.size()});
    }
    else
    {
        result = a2::make_h1d_sink_idx(
            arena,
            a2_input,
            { histos.data(), histos.size()},
            inputSlots[0]->paramIndex);
    }

    return result;
};

DEF_OP_MAGIC(histo2d_sink_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");
    assert(inputSlots.size() == 2);
    assert_slot(inputSlots[0]);
    assert_slot(inputSlots[1]);

    auto histoSink = qobject_cast<analysis::Histo2DSink *>(op.get());

    assert(histoSink);

    auto a2_xInput = find_output_pipe(adapterState, inputSlots[0]).first;
    auto a2_yInput = find_output_pipe(adapterState, inputSlots[1]).first;

    assert(inputSlots[0]->paramIndex != analysis::Slot::NoParamIndex);
    assert(inputSlots[1]->paramIndex != analysis::Slot::NoParamIndex);

    assert(inputSlots[0]->paramIndex < a2_xInput.data.size);
    assert(inputSlots[1]->paramIndex < a2_yInput.data.size);

    s32 xIndex = inputSlots[0]->paramIndex;
    s32 yIndex = inputSlots[1]->paramIndex;

    auto histo = histoSink->m_histo;

    using a2::H2D;

    AxisBinning binnings[H2D::AxisCount] =
    {
        histo->getAxisBinning(Qt::XAxis),
        histo->getAxisBinning(Qt::YAxis)
    };

    assert(binnings[H2D::XAxis].getBins() * binnings[H2D::YAxis].getBins() < a2::H2D::size_max);

    a2::H2D a2_histo = {};

    a2_histo.data = histo->data();
    a2_histo.size = binnings[H2D::XAxis].getBins() * binnings[H2D::YAxis].getBins();

    for (s32 axis = 0; axis < H2D::AxisCount; axis++)
    {
        double absMin = std::min(binnings[axis].getMin(), binnings[axis].getMax());
        double absMax = std::max(binnings[axis].getMin(), binnings[axis].getMax());
        double absRange = std::abs(absMax - absMin);

        a2_histo.binCounts[axis] = binnings[axis].getBins();
        a2_histo.binnings[axis].min = absMin;
        a2_histo.binnings[axis].range = absRange;
        a2_histo.binningFactors[axis] = a2_histo.binCounts[axis] / a2_histo.binnings[axis].range;
    }

    a2::Operator result = a2::make_h2d_sink(
        arena,
        a2_xInput,
        a2_yInput,
        xIndex,
        yIndex,
        a2_histo);

    return result;
}

DEF_OP_MAGIC(rate_monitor_sink_magic)
{
    OP_MAGIC_NOWARN;
    using a2::RateSampler;

    LOG("");
    assert(inputSlots.size() >= 1);

    auto rms = qobject_cast<analysis::RateMonitorSink *>(op.get());

    assert(rms);

    auto shared_samplers = rms->getRateSamplers();
    QVector<RateSampler *> samplers;
    samplers.reserve(shared_samplers.size());
    std::transform(shared_samplers.begin(), shared_samplers.end(),
                   std::back_inserter(samplers), [](auto &shared_sampler) {
                       return shared_sampler.get();
                   });

    assert(samplers.size() == shared_samplers.size());

    QVector<a2::PipeVectors> a2_inputs(inputSlots.size());
    QVector<s32> a2_input_indexes(inputSlots.size());

    for (s32 si = 0; si < inputSlots.size(); si++)
    {
        a2_inputs[si] = find_output_pipe(adapterState, inputSlots[si]).first;
        a2_input_indexes[si] = inputSlots[si]->paramIndex;
    }

    a2::Operator result = a2::make_rate_monitor(
        arena,
        { a2_inputs.data(), a2_inputs.size() },
        { a2_input_indexes.data(), a2_input_indexes.size() },
        { samplers.data(), samplers.size() },
        rms->getType()
        );

    return result;
}

DEF_OP_MAGIC(export_sink_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto sink = qobject_cast<analysis::ExportSink *>(op.get());
    assert(sink);
    assert(inputSlots.size() > 1);

    a2::PipeVectors a2_condInput;
    s32 condIndex = -1;
    auto *condSlot = inputSlots.at(0);

    if (condSlot->isParamIndexInRange())
    {
        a2_condInput = find_output_pipe(adapterState, condSlot).first;
        condIndex    = condSlot->paramIndex;
    }

    QVector<a2::PipeVectors> a2_dataInputs(inputSlots.size() - 1);
    std::vector<std::string> csvColumns;

    for (s32 si = 0; si < inputSlots.size() - 1; si++)
    {
        a2_dataInputs[si] = find_output_pipe(adapterState, inputSlots[si + 1]).first;
        auto pipe = inputSlots[si+1]->inputPipe;

        for (s32 pi = 0; pi<pipe->getSize(); ++pi)
        {

            auto pipeSource = pipe->getSource();
            auto pathParts = analysis::make_parent_path_list(pipeSource->shared_from_this());
            pathParts.push_back(pipeSource->objectName());
            pathParts.push_back(QString::number(pi));
            auto colName = pathParts.join(".");
            colName.replace(" ", "_");
            csvColumns.emplace_back(colName.toStdString());
        }
    }

    QString outputFilename = sink->getDataFilePath(runInfo);

    a2::Operator result = a2::make_export_sink(
        arena,
        outputFilename.toStdString(),
        sink->getCompressionLevel(),
        sink->getFormat(),
        { a2_dataInputs.data(), a2_dataInputs.size() },
        a2_condInput,
        condIndex,
        csvColumns
        );

    return result;
}

DEF_OP_MAGIC(interval_condition_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto cond = qobject_cast<analysis::IntervalCondition *>(op.get());
    assert(cond);
    assert(inputSlots.size() == 1);

    std::vector<a2::Interval> a2_intervals;
    a2_intervals.reserve(cond->getIntervals().size());

    for (const auto &interval: cond->getIntervals())
    {
        a2_intervals.push_back({ interval.minValue(), interval.maxValue() });
    }

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]).first;

    a2::Operator result = make_interval_condition(
        arena,
        a2_input,
        a2_intervals);

    return result;
}

DEF_OP_MAGIC(rectangle_condition_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto cond = qobject_cast<analysis::RectangleCondition *>(op.get());
    assert(cond);
    assert(inputSlots.size() == 2);

    const auto rect = cond->getRectangle();

    a2::Interval xInterval = { rect.left(), rect.right() };
    a2::Interval yInterval = { rect.bottom(), rect.top() };

    a2::Operator result = make_rectangle_condition(
        arena,
        find_output_pipe(adapterState, inputSlots[0]).first,
        find_output_pipe(adapterState, inputSlots[1]).first,
        inputSlots[0]->paramIndex,
        inputSlots[1]->paramIndex,
        xInterval,
        yInterval);

    return result;
}

DEF_OP_MAGIC(polygon_condition_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto cond = qobject_cast<analysis::PolygonCondition *>(op.get());
    assert(cond);
    assert(inputSlots.size() == 2);

    auto polygon = cond->getPolygon();

    std::vector<std::pair<double, double>> a2_polygon;
    a2_polygon.reserve(polygon.size());

    for (const auto &point: polygon)
    {
        a2_polygon.push_back({ point.x(), point.y() });
    }

    a2::Operator result = make_polygon_condition(
        arena,
        find_output_pipe(adapterState, inputSlots[0]).first,
        find_output_pipe(adapterState, inputSlots[1]).first,
        inputSlots[0]->paramIndex,
        inputSlots[1]->paramIndex,
        a2_polygon);

    return result;
}

DEF_OP_MAGIC(lut_condition_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto cond = qobject_cast<analysis::LutCondition *>(op.get());
    assert(cond);

    std::vector<a2::PipeVectors> inputs;
    std::vector<s32> inputIndexes;

    for (s32 si = 0; si < inputSlots.size(); si++)
    {
        inputs.emplace_back(find_output_pipe(adapterState, inputSlots[si]).first);
        inputIndexes.emplace_back(inputSlots[si]->paramIndex);
    }

    a2::Operator result = make_lut_condition(
        arena,
        inputs,
        inputIndexes,
        cond->getLUT());

    return result;
}

DEF_OP_MAGIC(expression_condition_magic)
{
    OP_MAGIC_NOWARN;
    LOG("");

    auto cond = qobject_cast<analysis::ExpressionCondition *>(op.get());
    assert(cond);

    std::vector<a2::PipeVectors> inputs;
    std::vector<s32> inputIndexes;
    std::vector<std::string> inputNames;

    for (s32 si = 0; si < inputSlots.size(); si++)
    {
        inputs.emplace_back(find_output_pipe(adapterState, inputSlots[si]).first);
        inputIndexes.emplace_back(inputSlots[si]->paramIndex);
        inputNames.emplace_back(cond->getInputPrefix(si).toStdString());
    }

    auto expression = cond->getExpression().toStdString();

    a2::Operator result = make_expression_condition(
        arena,
        inputs,
        inputIndexes,
        inputNames,
        expression);

    return result;
}

static const QHash<const QMetaObject *, OperatorMagic *> OperatorMagicTable =
{
    { &analysis::CalibrationMinMax::staticMetaObject,       calibration_magic },
    { &analysis::Difference::staticMetaObject,              difference_magic },
    { &analysis::ArrayMap::staticMetaObject,                array_map_magic },
    { &analysis::AggregateOps::staticMetaObject,            aggregate_ops_magic },
    { &analysis::BinarySumDiff::staticMetaObject,           binary_equation_magic },
    { &analysis::PreviousValue::staticMetaObject,           keep_previous_magic },
    { &analysis::RangeFilter1D::staticMetaObject,           range_filter_magic },
    { &analysis::RectFilter2D::staticMetaObject,            rect_filter_magic },
    { &analysis::ConditionFilter::staticMetaObject,         condition_filter_magic },
    { &analysis::Sum::staticMetaObject,                     sum_magic },
    { &analysis::ExpressionOperator::staticMetaObject,      expression_operator_magic },
    { &analysis::ScalerOverflow::staticMetaObject,          scaler_overflow_magic },

    { &analysis::IntervalCondition::staticMetaObject,       interval_condition_magic },
    { &analysis::RectangleCondition::staticMetaObject,      rectangle_condition_magic },
    { &analysis::PolygonCondition::staticMetaObject,        polygon_condition_magic },
    { &analysis::LutCondition::staticMetaObject,            lut_condition_magic },
    { &analysis::ExpressionCondition::staticMetaObject,     expression_condition_magic },

    { &analysis::Histo1DSink::staticMetaObject,             histo1d_sink_magic },
    { &analysis::Histo2DSink::staticMetaObject,             histo2d_sink_magic },
    { &analysis::RateMonitorSink::staticMetaObject,         rate_monitor_sink_magic },
    { &analysis::ExportSink::staticMetaObject,              export_sink_magic },
};

a2::Operator a2_adapter_magic(
    memory::Arena *arena,
    A2AdapterState *state,
    analysis::OperatorPtr op,
    const RunInfo &runInfo)
{
    a2::Operator result = {};
    result.type = a2::Invalid_OperatorType;

    assert(op->getNumberOfSlots() <= a2::Operator::MaxInputCount);
    assert(op->getNumberOfOutputs() <= a2::Operator::MaxOutputCount);

    InputSlots inputSlots(op->getNumberOfSlots());

    for (s32 slotIndex = 0; slotIndex < op->getNumberOfSlots(); slotIndex++)
    {
        inputSlots[slotIndex] = op->getSlot(slotIndex);

        if (inputSlots[slotIndex]->isOptional
                && !inputSlots[slotIndex]->isConnected())
            continue;

        // Check if the a2 pipe connected to the current inputSlot exists in
        // the A2AdapterState. If it doesn't it means that the DataSource or
        // Operator owning the pipe was not build for some reason (e.g.
        // DataSource was filtered out due to being unassigned).
        if (!find_output_pipe(state, inputSlots[slotIndex]).second)
            return result;
    }

    OutputPipes outputPipes(op->getNumberOfOutputs());

    for (s32 pipeIndex = 0; pipeIndex < op->getNumberOfOutputs(); pipeIndex++)
    {
        outputPipes[pipeIndex] = op->getOutput(pipeIndex);
    }

    auto operator_magic = OperatorMagicTable.value(op->metaObject(), nullptr);

    if (operator_magic)
    {
        LOG("found magic for %s (objectName=%s, id=%s)",
            op->metaObject()->className(),
            op->objectName().toLocal8Bit().constData(),
            op->getId().toString().toLocal8Bit().constData()
            );

        QLOG(op.get() << op->getId());

        result = operator_magic(arena, state, op, inputSlots, outputPipes, runInfo);
    }
    else
    {
        LOG("EE no magic for %s :(", op->metaObject()->className());
        InvalidCodePath;
    }

    return result;
}

} // end anon namespace

namespace analysis
{

a2::PipeVectors make_a2_pipe_from_a1_pipe(memory::Arena *arena, analysis::Pipe *a1_inPipe)
{
    a2::PipeVectors result = {};

    const s32 size = a1_inPipe->getSize();

    result.data        = a2::push_param_vector(arena, size, make_quiet_nan());
    result.lowerLimits = a2::push_param_vector(arena, size);
    result.upperLimits = a2::push_param_vector(arena, size);

    for (s32 i = 0; i < size; i++)
    {
        const auto &a1_param(a1_inPipe->getParameter(i));
        result.data[i]        = a1_param->value;
        result.lowerLimits[i] = a1_param->lowerLimit;
        result.upperLimits[i] = a1_param->upperLimit;
    }

    return result;
}

std::pair<a2::PipeVectors, bool>
find_output_pipe(const A2AdapterState *state, analysis::Pipe *pipe)
{
    assert(pipe);
    assert(pipe->source);

    return ::find_output_pipe(
        state,
        pipe->source,
        pipe->sourceOutputIndex);
}

void a2_adapter_build_datasources(
    memory::Arena *arena,
    A2AdapterState *state,
    const analysis::SourceVector &sources,
    const vme_analysis_common::VMEIdToIndex &vmeMap)
{
    struct SourceInfo
    {
        SourcePtr source;
        int moduleIndex;
    };

    std::array<QVector<SourceInfo>, a2::MaxVMEEvents> sourceInfos;

    for (auto source: sources)
    {
        assert(!source->getEventId().isNull());
        assert(!source->getModuleId().isNull());

        //qDebug() << __PRETTY_FUNCTION__ << source->getModuleId();

        auto index = vmeMap.value(source->getModuleId());

        if (index.eventIndex < 0 || index.moduleIndex < 0)
        {
            qDebug() << __PRETTY_FUNCTION__
                << "got an invalid index pair for data source" << source.get()
                << ", ei =" << index.eventIndex << ", mi =" << index.moduleIndex
                << ", source->eventId=" << source->getEventId()
                << ", source->moduleId=" << source->getModuleId()
                ;
            continue;
        }

        Q_ASSERT(0 <= index.eventIndex);
        Q_ASSERT(index.eventIndex < a2::MaxVMEEvents);

        Q_ASSERT(0 <= index.moduleIndex);
        Q_ASSERT(index.moduleIndex < a2::MaxVMEModules);

        SourceInfo sourceInfo = { source, index.moduleIndex };

        sourceInfos[index.eventIndex].push_back(sourceInfo);
    }

    // Sort the source vector by moduleIndex
    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        std::stable_sort(sourceInfos[ei].begin(), sourceInfos[ei].end(), [](auto a, auto b) {
            return a.moduleIndex < b.moduleIndex;
        });
    }

    // Adapt the Extractors
    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        for (auto src: sourceInfos[ei])
        {
            qDebug()
                << "SourceInfo: eventIndex =" << ei
                << ", moduleIndex =" << src.moduleIndex
                << ", source =" << src.source.get();
        }

        Q_ASSERT(sourceInfos[ei].size() <= std::numeric_limits<u8>::max());

        // space for the DataSource pointers
        state->a2->dataSources[ei] = arena->pushArray<a2::DataSource>(sourceInfos[ei].size());

        for (auto src: sourceInfos[ei])
        {
            a2::DataSource ds = {};

            // analysis::Extractor
            if (auto ex = qobject_cast<analysis::Extractor *>(src.source.get()))
            {
                a2::data_filter::MultiWordFilter filter = {};
                for (auto slowFilter: ex->getFilter().getSubFilters())
                {
                    a2::data_filter::add_subfilter(
                        &filter,
                        a2::data_filter::make_filter(
                            to_string(slowFilter),
                            slowFilter.matchWordIndex));
                }

                ds = a2::make_datasource_extractor(
                    arena,
                    filter,
                    ex->m_requiredCompletionCount,
                    ex->m_rngSeed,
                    src.moduleIndex,
                    ex->getOptions());
            }
            // analysis::ListFilterExtractor
            else if (auto ex = qobject_cast<analysis::ListFilterExtractor *>(src.source.get()))
            {
                ds = a2::make_datasource_listfilter_extractor(
                    arena,
                    ex->getExtractor().listFilter,
                    ex->getExtractor().repetitions,
                    ex->getRngSeed(),
                    src.moduleIndex,
                    ex->getOptions());
            }
            // analysis::MultiHitExtractor
            else if (auto ex = qobject_cast<analysis::MultiHitExtractor *>(src.source.get()))
            {
                ds = a2::make_datasource_multihit_extractor(
                    arena,
                    ex->getShape(),
                    ex->getFilter(),
                    ex->getMaxHits(),
                    ex->getRngSeed(),
                    src.moduleIndex,
                    ex->getOptions());
            }

            a2::A2::OperatorCountType &ds_cnt = state->a2->dataSourceCounts[ei];
            state->a2->dataSources[ei][ds_cnt] = ds;
            state->sourceMap.insert(src.source.get(), state->a2->dataSources[ei] + ds_cnt);
            ds_cnt++;
        }
    }
}

struct OperatorInfo
{
    analysis::OperatorPtr op;
    int rank;
    s32 a2OperatorType = a2::Invalid_OperatorType;
};

using OperatorsByEventIndex = std::array<QVector<OperatorInfo>, a2::MaxVMEEvents>;

OperatorsByEventIndex group_operators_by_event(
    const analysis::OperatorVector &operators,
    const vme_analysis_common::VMEIdToIndex &vmeMap)
{
    std::array<QVector<OperatorInfo>, a2::MaxVMEEvents> result;

    for (auto op: operators)
    {
        int eventIndex = vmeMap.value(op->getEventId()).eventIndex;

        assert(eventIndex < a2::MaxVMEEvents);

        if (eventIndex >= 0)
            result[eventIndex].push_back({ op, op->getRank(), -1 });
    }

    return result;
}

/* Returns true if any of the inputs of the given operator is contained in the
 * set of error info objects. */
bool has_input_from_error_set(const OperatorPtr &op,
                              const A2AdapterState::ErrorInfoVector &errorInfos)
{
    bool result = false;

    for (s32 si = 0; si < op->getNumberOfSlots(); si++)
    {
        auto slot = op->getSlot(si);

        if (!(slot && slot->isConnected() && slot->inputPipe->getSource()))
            continue;

        if (auto inputOp = qobject_cast<OperatorInterface *>(slot->inputPipe->getSource()))
        {
            auto it = std::find_if(errorInfos.begin(), errorInfos.end(),
                                   [&inputOp] (const A2AdapterState::ErrorInfo &errorInfo) {
                return errorInfo.op.get() == inputOp;
            });

            if (it != errorInfos.end())
            {
                result = true;
                break;
            }
        }
    }

    return result;
}

bool has_input_from_any_source(const OperatorPtr &op, const SourceVector &sources)
{
    for (s32 si=0; si<op->getNumberOfSlots(); si++)
    {
        auto slot = op->getSlot(si);

        if (!(slot && slot->isConnected() && slot->inputPipe->getSource()))
            continue;

        if (auto inputSource = qobject_cast<const SourceInterface *>(slot->inputPipe->getSource()))
        {
            auto it = std::find_if(
                std::begin(sources), std::end(sources),
                [&inputSource] (const auto &source) { return source.get() == inputSource; });

            if (it != std::end(sources))
                return true;
        }
    }

    return false;
};

void a2_adapter_build_single_operator(
    memory::Arena *arena,
    A2AdapterState *state,
    OperatorInfo &opInfo,
    s32 eventIndex,
    const RunInfo &runInfo)
{
    assert(state->a1);
    assert(state->a2);


    /* FIXME: for correctness a test if any of the input operators caused an
     * error has to be performed here:
     * For each input operator of the operator to be adapted:
     *   if inputOperator is in state->operatorErrors:
     *     do not build this operator
     *     instead also add it to the set of errors
     *     the reason should be set to "Input operator is in error state".
     *
     * For the ExpressionOperator this is not needed right now as the operator
     * tries an a2 build in ExpressionOperator::beginRun() and if that fails
     * keeps its outputs but resizes them to 0. Connected histo sinks will pick
     * this up and not error out.
     * Still, other dependent operators may not behave as well as the histosink
     * does and fail or assert in this case. Also no error information is
     * conveyed to the user.
     */
    if (has_input_from_error_set(opInfo.op, state->operatorErrors))
    {
        A2AdapterState::ErrorInfo error
        {
            opInfo.op,
            eventIndex,
            QSL("Input operator is in error/invalid state."),
            std::current_exception() // will result in a null exception_ptr
        };

        state->operatorErrors.push_back(error);

        return;
    }

    try
    {
        auto a2_op = a2_adapter_magic(arena, state, opInfo.op, runInfo);

        assert(a2_op.conditionBitIndexes.size == 0);

        if (a2::Invalid_OperatorType != a2_op.type && a2_op.type < a2::OperatorTypeCount)
        {
            /* If the operator is a condition set the index of the bit to write
             * when evaluating the condition. */
            if (auto a1_cond = qobject_cast<ConditionInterface *>(opInfo.op.get()))
            {
                assert(is_condition_operator(a2_op));

                auto d = reinterpret_cast<a2::ConditionBaseData *>(a2_op.d);

                /* It's not an error if the bit index is not setup yet as it is
                 * only known during the second pass build phase. */
                if (state->conditionBitIndexes.contains(a1_cond))
                {
                    d->bitIndex = state->conditionBitIndexes.value(a1_cond);
                }
            }

            /* Check for active conditions and set the corresponding bit
             * indexes on the operator. */
            auto activeConditions = state->a1->getActiveConditions(opInfo.op);
            std::vector<u16> conditionBitIndexes;

            for (const auto &cond: activeConditions)
            {
                // Check if the conditions bit index is known
                if (state->conditionBitIndexes.contains(cond.get()))
                {
                    conditionBitIndexes.push_back(state->conditionBitIndexes.value(cond.get()));
                }
            }

            if (!conditionBitIndexes.empty())
            {
                // Sort by increasing bit index and copy into the operator structure
                std::sort(std::begin(conditionBitIndexes), std::end(conditionBitIndexes));
                a2_op.conditionBitIndexes = push_copy_typed_block<u16>(arena, conditionBitIndexes);
            }

            opInfo.a2OperatorType = a2_op.type;
            a2::A2::OperatorCountType &opCount = state->a2->operatorCounts[eventIndex];

            // Copy the operator struct into the A2 instance
            state->a2->operators[eventIndex][opCount] = a2_op;
            state->a2->operatorRanks[eventIndex][opCount] = opInfo.rank;
            state->operatorMap.insert(opInfo.op.get(),
                                      state->a2->operators[eventIndex] + opCount);
            opCount++;

            for (unsigned bitIndex = 0; bitIndex < a2_op.conditionBitIndexes.size; ++bitIndex)
            {
                LOG("a2_op.type=%d, .conditionIndex=%d",
                    (s32)(a2_op.type), a2_op.conditionBitIndexes[bitIndex]);
            }
        }
        else
        {
            A2AdapterState::ErrorInfo error
            {
                opInfo.op,
                eventIndex,
                QSL("Adapter function returned invalid operator type."),
                std::current_exception() // will result in a null exception_ptr
            };

            state->operatorErrors.push_back(error);
        }
    }
    catch (const std::runtime_error &e)
    {
        qDebug() << __PRETTY_FUNCTION__ << e.what();
        A2AdapterState::ErrorInfo error
        {
            opInfo.op,
            eventIndex,
            QSL("Exception from operator adapter function."),
            std::current_exception()
        };

        state->operatorErrors.push_back(error);
    }
}

/* Fills in state and operators. */
void a2_adapter_build_operators(
    memory::Arena *arena,
    A2AdapterState *state,
    OperatorsByEventIndex &operators,
    const RunInfo &runInfo)
{
    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        if (operators[ei].size())
            qDebug() << "got" << operators[ei].size() << "operators for event" << ei;
        Q_ASSERT(operators[ei].size() <= std::numeric_limits<a2::A2::OperatorCountType>::max());

        state->a2->operators[ei] = arena->pushArray<a2::Operator>(operators[ei].size());
        state->a2->operatorRanks[ei] = arena->pushArray<a2::A2::OperatorCountType>(operators[ei].size());

        for (auto &opInfo: operators[ei])
        {
            a2_adapter_build_single_operator(arena, state, opInfo, ei, runInfo);
        }
    }
}

void set_null_if_input_is(analysis::OperatorVector &operators,
                          OperatorInterface *inputOp,
                          s32 startIndex)
{
    s32 operatorCount = operators.size();

    for (s32 i = startIndex;
         i < operatorCount;
         i++)
    {
        auto &op = operators[i];

        if (op)
        {
            for (s32 si = 0; si < op->getNumberOfSlots(); si++)
            {
                auto slot = op->getSlot(si);

                if (slot->inputPipe && slot->inputPipe->source == inputOp)
                {
                    set_null_if_input_is(operators, op.get(), i);
                    op.reset();
                    break;
                }
            }
        }
    }
}

analysis::OperatorVector a2_adapter_filter_operators(
    analysis::OperatorVector operators,
    const vme_analysis_common::VMEIdToIndex &vmeMap,
    const SourceVector &inactiveSources)
{
    OperatorVector result;

    const s32 operatorCount = operators.size();

    for (s32 opIndex = 0; opIndex < operatorCount; opIndex++)
    {
        auto &op = operators[opIndex];

        if (op)
        {
            bool filter_out = (!required_inputs_connected_and_valid(op.get())
                               && vmeMap.value(op->getEventId()).isValidEvent()
                               && !has_input_from_any_source(op, inactiveSources)
                               );

            if (auto sink = qobject_cast<SinkInterface *>(op.get()))
            {
                filter_out = filter_out || !sink->isEnabled();
            }

            if (filter_out)
            {
                QLOG("filtering out" << op.get() << "and children");
                set_null_if_input_is(operators, op.get(), opIndex + 1);
                op.reset();
            }
        }
    }

    for (s32 opIndex = 0; opIndex < operatorCount; opIndex++)
    {
        if (operators[opIndex])
        {
            result.push_back(operators[opIndex]);
        }
    }

    QLOG("filtered out" << operators.size() - result.size()
         << "of" << operators.size() << "operators");

    return result;
}

std::pair<analysis::SourceVector, analysis::SourceVector>
a2_adapter_filter_sources(
    const analysis::SourceVector &sources,
    const vme_analysis_common::VMEIdToIndex &vmeMap)
{
    SourceVector good;
    SourceVector bad;

    for (const auto &source: sources)
    {
        if (!source->getEventId().isNull()
            && !source->getModuleId().isNull()
            && vmeMap.value(source->getModuleId()).isValid())
            good.push_back(source);
        else
            bad.push_back(source);
    }

    return std::make_pair(good, bad);
}

A2AdapterState a2_adapter_build(
    memory::Arena *arena,
    memory::Arena *workArena,
    Analysis *analysis,
    const analysis::SourceVector &sources,
    const analysis::OperatorVector &operators,
    const vme_analysis_common::VMEIdToIndex &vmeMap,
    const RunInfo &runInfo)
{
    A2AdapterState result = {};

    result.a1 = analysis;
    result.a2 = arena->pushObject<a2::A2>(arena);

    for (u32 i = 0; i < result.a2->dataSourceCounts.size(); i++)
    {
        assert(result.a2->dataSourceCounts[i] == 0);
        assert(result.a2->operatorCounts[i] == 0);
    }

    // -------------------------------------------
    // Source -> Extractor
    // -------------------------------------------

    SourceVector activeSources, inactiveSources;

    std::tie(activeSources, inactiveSources) = a2_adapter_filter_sources(sources, vmeMap);

    a2_adapter_build_datasources(
        arena,
        &result,
        activeSources,
        vmeMap);

    LOG("data sources:");

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        if (!result.a2->dataSourceCounts[ei])
            continue;
        LOG("  ei=%d, #ds=%d", ei, (u32)result.a2->dataSourceCounts[ei]);
    }

    assert(result.sourceMap.size() == activeSources.size());

    // -------------------------------------------
    // a1 Operator -> a2 Operator
    // -------------------------------------------

    /* The problem: I want the operators for each event to be sorted by rank
     * _and_ by a2::OperatorType.
     *
     * The a2::OperatorType resulting from converting an analysis operator is
     * not known without actually doing the conversion.
     *
     * Step 1: Use the workArena to fill the A2 structure.
     * Step 2: Sort the operators by a2::OperatorType, preserving rank order.
     * Step 3: Clear the operator part of A2.
     * Step 4: Build again using the sorted operators information and the destination arena.
     */

    /* Filter out operators that are not fully connected. */
    auto activeOperators = a2_adapter_filter_operators(operators, vmeMap, inactiveSources);

    OperatorsByEventIndex operatorsByEventIndex = group_operators_by_event(
        activeOperators, vmeMap);

    /* Build in work arena. Fills out result and operators. */
    LOG("a2 adapter build first pass");
    a2_adapter_build_operators(
        workArena,
        &result,
        operatorsByEventIndex,
        runInfo);

    LOG("operators before type sort:");

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        if (!result.a2->operatorCounts[ei])
            continue;
        LOG("  ei=%d, #ops=%d", ei, (u32)result.a2->operatorCounts[ei]);
    }

    // FIXME: figure this out
    //assert(activeOperators.size() == result.operatorMap.size() + result.operatorErrors.size());

    /* Sort the operator arrays by rank and type */
    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        std::sort(
            operatorsByEventIndex[ei].begin(),
            operatorsByEventIndex[ei].end(),
            [] (const OperatorInfo &oi1, const OperatorInfo &oi2) {
                if (oi1.rank == oi2.rank)
                {
                    return oi1.a2OperatorType < oi2.a2OperatorType;
                }
                return oi1.rank < oi2.rank;
            });
    }

    /* Walk sorted operator arrays assigning increasing condition bit positions
     * for condition operators and filling the conditionBitIndexes bi-hash.
     * This information will be available for the second build pass below. */
    s16 nextConditionBitIndex = 0;

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        const auto &opInfos(operatorsByEventIndex[ei]);

        for (auto &opInfo: opInfos)
        {
            if (auto cond = qobject_cast<ConditionInterface *>(opInfo.op.get()))
            {
                assert(!result.conditionBitIndexes.contains(cond));
                assert(!result.conditionBitIndexes.contains(nextConditionBitIndex));

                result.conditionBitIndexes.insert(cond, nextConditionBitIndex);
                ++nextConditionBitIndex;
            }
        }
    }

    // nextConditionBitIndex now holds the total number of condition bits used

    result.a2->conditionBits.resize(nextConditionBitIndex);
    result.a2->conditionBits.reset(); // clear all bits

    /* Clear the operator part. */
    result.a2->operatorCounts.fill(0);
    result.a2->operators.fill(nullptr);
    result.a2->operatorRanks.fill(nullptr);
    result.operatorMap.clear();
    result.operatorErrors.clear();

    /* Second build using the destination arena. */
    LOG("a2 adapter build second pass");
    a2_adapter_build_operators(
        arena,
        &result,
        operatorsByEventIndex,
        runInfo
        );

    LOG("operators after type sort:");

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        if (!result.a2->operatorCounts[ei])
            continue;
        LOG("  ei=%d, #ops=%d", ei, (u32)result.a2->operatorCounts[ei]);
    }

    // FIXME: figure this out
    //assert(activeOperators.size() == result.operatorMap.size() + result.operatorErrors.size());

    LOG("mem=%lu", arena->used());

#define qcstr(str) ((str).toLocal8Bit().constData())

    LOG(">>>>>>>> result <<<<<<<<");

    LOG("data sources:");

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        auto srcCount = result.a2->dataSourceCounts[ei];

        if (srcCount)
        {
            LOG("  ei=%d", ei);

            auto dataSources = result.a2->dataSources[ei];

            for (a2::DataSource *ds = dataSources; ds < dataSources + srcCount; ds++)
            {
                analysis::SourceInterface *a1_src = result.sourceMap.value(ds, nullptr);
                (void) a1_src;

                LOG("    [%u] data source@%p, moduleIndex=%d, a1_type=%s, a1_name=%s",
                    (u32)(ds - dataSources),
                    ds,
                    (s32)ds->moduleIndex,
                    a1_src ? a1_src->metaObject()->className() : "nullptr",
                    a1_src ? qcstr(a1_src->objectName()) : "nullptr");
            }
        }
    }

    LOG("operators:");

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        auto opCount = result.a2->operatorCounts[ei];

        if (opCount)
        {
            LOG("  ei=%d", ei);

            auto operators = result.a2->operators[ei];
            auto ranks = result.a2->operatorRanks[ei];

            s32 opIndex = 0;
            for (auto op = operators; op < operators + opCount; op++, opIndex++)
            {
                s32 rank = ranks[opIndex];
                (void) rank;

                analysis::OperatorInterface *a1_op = result.operatorMap.value(op, nullptr);
                (void) a1_op;

                for (unsigned bitIndex = 0; bitIndex < op->conditionBitIndexes.size; ++bitIndex)
                {
                    LOG("    [%3d] operator@%p, rank=%2d, type=%2d, condIdx=%d a1_type=%s, a1_name=%s",
                        opIndex,
                        op,
                        rank,
                        (s32)op->type,
                        op->conditionBitIndexes[bitIndex],
                        a1_op ? a1_op->metaObject()->className() : "nullptr",
                        a1_op ? qcstr(a1_op->objectName()) : "nullptr"
                        );
                }
            }
        }
    }

    if (!result.operatorErrors.isEmpty())
    {
        LOG("%d operator adapter errors:", result.operatorErrors.size());

        for (const auto &errorInfo: result.operatorErrors)
        {
            auto &a1_op = errorInfo.op;
            (void) a1_op;

            LOG("  ei=%d, a1_type=%s, a1_name=%s, reason=%s",
                errorInfo.eventIndex,
                a1_op ? a1_op->metaObject()->className() : "nullptr",
                a1_op ? qcstr(a1_op->objectName()) : "nullptr",
                qcstr(errorInfo.reason)
                );
        }
    }

    LOG("conditions:");

    LOG("  bits=%lu", result.a2->conditionBits.size());

    for (const auto &cond: result.conditionBitIndexes.hash.keys())
    {
        [[maybe_unused]] s16 bitIndex = result.conditionBitIndexes.value(cond);

        assert(bitIndex < static_cast<s32>(result.a2->conditionBits.size()));

        if ([[maybe_unused]] auto a2_cond = result.operatorMap.value(cond))
        {
            LOG("    firstBit=%3d, a2_cond=%p, a2_type=%2d, a1_type=%s, a1_name=%s",
                bitIndex,
                a2_cond,
                a2_cond->type,
                cond->metaObject()->className(),
                qcstr(cond->objectName())
            );
        }
    }

#undef qcstr

    LOG("<<<<<<<< end result >>>>>>>>");

    return result;
}

} // namespace analysis
