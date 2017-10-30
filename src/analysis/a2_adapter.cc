#include "a2_adapter.h"
#include "a2/a2_impl.h"
#include "analysis.h"
#include <cstdio>
#include <QMetaObject>
#include <QMetaClassInfo>

#ifndef NDEBUG
#define LOG(fmt, ...)\
do\
{\
    fprintf(stderr, "a2_adapter::%s() " fmt, __FUNCTION__, ##__VA_ARGS__);\
    fprintf(stderr, "\n");\
} while (0)
#else
#define LOG(...)
#endif

#ifndef NDEBUG
inline QDebug a2_adapter_qlog(const char *func)
{
    return (qDebug().nospace() << "a2_adapter::" << func << "()").space();
}

#define QLOG(x) a2_adapter_qlog(__FUNCTION__) << x
#else
#define QLOG(x)
#endif

namespace
{
using a2::ParamVec;
//using a2::PipeVectors;
using analysis::A2AdapterState;

using InputSlots = QVector<analysis::Slot *>;
using OutputPipes = QVector<analysis::Pipe *>;

#define DEF_OP_MAGIC(name) a2::Operator name(\
    memory::Arena *arena,\
    A2AdapterState &adapterState,\
    analysis::OperatorPtr op,\
    InputSlots inputSlots,\
    OutputPipes outputPipes)

typedef DEF_OP_MAGIC(OperatorMagic);

inline a2::PipeVectors find_output_pipe(const A2AdapterState &state, const analysis::PipeSourcePtr &pipeSource, u8 outputIndex)
{
    //QLOG("pipeSource to find:" << pipeSource.get() << ", outputIndex=" << (u32)outputIndex);

    a2::PipeVectors result = {};

    if (a2::Extractor *ex_a2 = state.sourceMappings.value(pipeSource, nullptr))
    {
        assert(outputIndex == 0);
        result = ex_a2->output;
    }
    else if (a2::Operator *op_a2 = state.operatorMappings.value(pipeSource, nullptr))
    {
        assert(outputIndex < op_a2->outputCount);
        result.data = op_a2->outputs[outputIndex];
        result.lowerLimits = op_a2->outputLowerLimits[outputIndex];
        result.upperLimits = op_a2->outputUpperLimits[outputIndex];
    }
    else
    {
        QLOG(pipeSource.get() << pipeSource->getId());
        assert(!"no source mapping");
    }

    return result;
}

inline a2::PipeVectors find_output_pipe(const A2AdapterState &state, analysis::Slot *slot)
{
    // This should be the path to get the correct 
    return find_output_pipe(
        state,
        slot->inputPipe->source->getSharedPointer(),
        slot->inputPipe->sourceOutputIndex);
}

#define assert_slot(slot)\
do\
{\
    assert(slot->inputPipe);\
    assert(slot->inputPipe->source);\
    assert(slot->inputPipe->source->getSharedPointer());\
} while (0)

DEF_OP_MAGIC(calibration_magic)
{
    LOG("");
    assert(inputSlots.size() == 1);
    assert_slot(inputSlots[0]);

    auto calib = qobject_cast<analysis::CalibrationMinMax *>(op.get());

    assert(calib);

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]);

    auto calibs = calib->getCalibrations();

    QVector<double> calibMinimums(calibs.size());
    QVector<double> calibMaximums(calibs.size());

    for (s32 i = 0; i < calibs.size(); i++)
    {
        calibMinimums[i] = calibs[i].unitMin;
        calibMaximums[i] = calibs[i].unitMax;
    }

    a2::Operator result = a2::make_calibration(
        arena,
        a2_input,
        { calibMinimums.data(), calibMinimums.size() },
        { calibMaximums.data(), calibMaximums.size() });

    return result;
};

DEF_OP_MAGIC(difference_magic)
{
    LOG("");
    assert(inputSlots.size() == 2);
    assert_slot(inputSlots[0]);
    assert_slot(inputSlots[1]);
    //assert(inputSlots[0]->acceptedInputTypes == analysis::InputType::Array);

    auto diff = qobject_cast<analysis::Difference *>(op.get());

    assert(diff);

    auto a2_inputA = find_output_pipe(adapterState, inputSlots[0]);
    auto a2_inputB = find_output_pipe(adapterState, inputSlots[1]);

    a2::Operator result = {};

    if (inputSlots[0]->acceptedInputTypes == analysis::InputType::Array)
    {
        assert(inputSlots[0]->paramIndex == analysis::Slot::NoParamIndex);
        assert(inputSlots[1]->paramIndex == analysis::Slot::NoParamIndex);

        result = make_difference(arena, a2_inputA, a2_inputB);
    }
    else
    {
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
    a2::TypedBlock<T, SizeType> block;
    QVector<T> store;
};

DEF_OP_MAGIC(array_map_magic)
{
    LOG("");

    auto arrayMap = qobject_cast<analysis::ArrayMap *>(op.get());
    assert(arrayMap);

    QVector<a2::PipeVectors> a2_inputs(inputSlots.size());

    for (s32 si = 0; si < inputSlots.size(); si++)
    {
        a2_inputs[si] = find_output_pipe(adapterState, inputSlots[si]);
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
    LOG("");

    using analysis::AggregateOps;
    auto agOps = qobject_cast<AggregateOps *>(op.get());
    assert(agOps);

    a2::Thresholds thresholds =
    {
        agOps->getMinThreshold(),
        agOps->getMaxThreshold()
    };

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]);

    a2::Operator result = {};

    switch (agOps->getOperation())
    {
        case AggregateOps::Op_Sum:
            result = make_aggregate_sum(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_Max:
            result = make_aggregate_max(arena, a2_input, thresholds);
            break;

        case AggregateOps::Op_Multiplicity:
            result = make_aggregate_multiplicity(arena, a2_input, thresholds);
            break;

        default:
            assert(!"unsupported AggregateOps::Operation");
    }

    return result;
}

DEF_OP_MAGIC(binary_equation_magic)
{
    LOG("");

    auto binSumDiff = qobject_cast<analysis::BinarySumDiff *>(op.get());
    assert(binSumDiff);

    auto a2_inputA = find_output_pipe(adapterState, inputSlots[0]);
    auto a2_inputB = find_output_pipe(adapterState, inputSlots[1]);

    /* Copy user set output limits from the analysis::BinarySumDiff output. */
    double outputLowerLimit = outputPipes[0]->parameters[0].lowerLimit;
    double outputUpperLimit = outputPipes[0]->parameters[0].upperLimit;

    a2::Operator result = make_binary_equation(
        arena,
        a2_inputA,
        a2_inputB,
        binSumDiff->getEquation(),
        outputLowerLimit,
        outputUpperLimit);

    return result;
}

DEF_OP_MAGIC(histo1d_sink_magic)
{
    LOG("");
    assert(inputSlots.size() == 1);
    assert_slot(inputSlots[0]);

    auto histoSink = qobject_cast<analysis::Histo1DSink *>(op.get());

    assert(histoSink);

    auto a2_input = find_output_pipe(adapterState, inputSlots[0]);

    QVector<a2::H1D> histos(histoSink->m_histos.size());

    for (s32 i = 0; i < histos.size(); i++)
    {
        auto histo = histoSink->m_histos[i];

        assert(histo->getNumberOfBins() < a2::H1D::size_max);

        a2::H1D a2_histo = {};
        a2_histo.data = histo->data();
        a2_histo.size = histo->getNumberOfBins();
        a2_histo.binning.min = histo->getXMin();
        a2_histo.binning.range = histo->getXMax() - histo->getXMin();
        // binningFactor = binCount / binning.range
        a2_histo.binningFactor = a2_histo.size / a2_histo.binning.range;

        histos[i] = a2_histo;
    }

    a2::Operator result = a2::make_h1d_sink(
        arena,
        a2_input,
        { histos.data(), histos.size()});

    return result;
};

DEF_OP_MAGIC(histo2d_sink_magic)
{
    LOG("");
    assert(inputSlots.size() == 2);
    assert_slot(inputSlots[0]);
    assert_slot(inputSlots[1]);

    auto histoSink = qobject_cast<analysis::Histo2DSink *>(op.get());

    assert(histoSink);

    auto a2_xInput = find_output_pipe(adapterState, inputSlots[0]);
    auto a2_yInput = find_output_pipe(adapterState, inputSlots[1]);

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
        a2_histo.binCounts[axis] = binnings[axis].getBins();
        a2_histo.binnings[axis].min = binnings[axis].getMin();
        a2_histo.binnings[axis].range = binnings[axis].getMax() - binnings[axis].getMin();
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

//using OperatorAdapterTable = QHash<const QMetaObject *, OperatorMagic *>;

static const QHash<const QMetaObject *, OperatorMagic *> OperatorMagicTable =
{
    { &analysis::CalibrationMinMax::staticMetaObject, calibration_magic },
    { &analysis::Difference::staticMetaObject, difference_magic },
    { &analysis::ArrayMap::staticMetaObject, array_map_magic },
    { &analysis::AggregateOps::staticMetaObject, aggregate_ops_magic },
    { &analysis::BinarySumDiff::staticMetaObject, binary_equation_magic },

    { &analysis::Histo1DSink::staticMetaObject, histo1d_sink_magic },
    { &analysis::Histo2DSink::staticMetaObject, histo2d_sink_magic },
};

a2::Operator a2_adapter_magic(memory::Arena *arena, A2AdapterState &state, analysis::OperatorPtr op)
{
    a2::Operator result = {};
    result.type = a2::OperatorTypeCount;

    assert(op->getNumberOfSlots() <= a2::Operator::MaxInputCount);
    assert(op->getNumberOfOutputs() <= a2::Operator::MaxOutputCount);

    InputSlots inputSlots(op->getNumberOfSlots());

    for (s32 slotIndex = 0; slotIndex < op->getNumberOfSlots(); slotIndex++)
    {
        inputSlots[slotIndex] = op->getSlot(slotIndex);
    }

    OutputPipes outputPipes(op->getNumberOfOutputs());

    for (s32 pipeIndex = 0; pipeIndex < op->getNumberOfOutputs(); pipeIndex++)
    {
        outputPipes[pipeIndex] = op->getOutput(pipeIndex);
    }

    auto operator_magic = OperatorMagicTable.value(op->metaObject(), nullptr);

    if (operator_magic)
    {
        LOG("found magic for %s", op->metaObject()->className());
        QLOG(op.get() << op->getId());
        result = operator_magic(arena, state, op, inputSlots, outputPipes);
    }
    else
    {
        LOG("EE no magic for %s :(", op->metaObject()->className());
    }

    return result;
}

}

namespace analysis
{

a2::PipeVectors find_output_pipe(const A2AdapterState *state, analysis::Pipe *pipe)
{
    assert(pipe);
    assert(pipe->source);

    return ::find_output_pipe(
        *state,
        pipe->source->getSharedPointer(),
        pipe->sourceOutputIndex);
}

A2AdapterState a2_adapter_build(
    memory::Arena *arena,
    const QVector<Analysis::SourceEntry> &sourceEntries,
    const QVector<Analysis::OperatorEntry> &operatorEntries,
    const QHash<QUuid, QPair<int, int>> &vmeConfigUuIdToIndexes)
{
    qDebug() << vmeConfigUuIdToIndexes;

    A2AdapterState result = {};
    result.a2 = arena->push<a2::A2>({});

    for (u32 i = 0; i < result.a2->extractorCounts.size(); i++)
    {
        assert(result.a2->extractorCounts[i] == 0);
        assert(result.a2->operatorCounts[i] == 0);
    }

    // -------------------------------------------
    // Source -> Extractor
    // -------------------------------------------

    struct SourceInfo
    {
        SourcePtr source;
        int moduleIndex;
    };

    std::array<QVector<SourceInfo>, a2::MaxVMEEvents> sources;

    for (auto se: sourceEntries)
    {
        auto p = vmeConfigUuIdToIndexes[se.moduleId];
        int eventIndex = p.first;
        int moduleIndex = p.second;

        Q_ASSERT(eventIndex < a2::MaxVMEEvents);
        Q_ASSERT(moduleIndex < a2::MaxVMEModules);

        sources[eventIndex].push_back({ se.source, moduleIndex });
        qSort(sources[eventIndex].begin(), sources[eventIndex].end(), [](auto a, auto b) {
            return a.moduleIndex < b.moduleIndex;
        });
    }

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        for (auto src: sources[ei])
        {
            qDebug() 
                << "eventIndex =" << ei
                << ", moduleIndex =" << src.moduleIndex
                << ", source =" << src.source.get();
        }

        Q_ASSERT(sources[ei].size() <= std::numeric_limits<u8>::max());

        // space for the extractor pointers
        result.a2->extractors[ei] = arena->pushArray<a2::Extractor>(sources[ei].size());

        for (auto src: sources[ei])
        {
            auto ex = qobject_cast<analysis::Extractor *>(src.source.get());
            Q_ASSERT(ex);

            a2::data_filter::MultiWordFilter filter = {};
            for (auto slowFilter: ex->getFilter().getSubFilters())
            {
                a2::data_filter::add_subfilter(
                    &filter,
                    a2::data_filter::make_filter(
                        slowFilter.getFilter().toStdString(),
                        slowFilter.getWordIndex()));
            }

            auto ex_a2 = a2::make_extractor(
                arena,
                filter,
                ex->m_requiredCompletionCount,
                ex->m_rngSeed,
                src.moduleIndex);

            u8 &ex_cnt = result.a2->extractorCounts[ei];
            result.a2->extractors[ei][ex_cnt] = ex_a2;
            result.sourceMappings[src.source] = result.a2->extractors[ei] + ex_cnt;
            ex_cnt++;
        }
    }

    // -------------------------------------------
    // Operator -> Operator
    // -------------------------------------------

    struct OperatorInfo
    {
        OperatorPtr op;
        int eventIndex;
        int rank;
    };

    std::array<QVector<OperatorInfo>, a2::MaxVMEEvents> operators;

    for (auto oe: operatorEntries)
    {
        auto p = vmeConfigUuIdToIndexes[oe.eventId];
        int eventIndex = p.first;

        Q_ASSERT(eventIndex < a2::MaxVMEEvents);

        operators[eventIndex].push_back({ oe.op, eventIndex, oe.op->getMaximumOutputRank() });
    }

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        Q_ASSERT(operators[ei].size() <= std::numeric_limits<u8>::max());

        result.a2->operators[ei] = arena->pushArray<a2::Operator>(operators[ei].size());
        result.a2->operatorRanks[ei] = arena->pushArray<u8>(operators[ei].size());

        for (auto opInfo: operators[ei])
        {
            auto a2_op = a2_adapter_magic(arena, result, opInfo.op);

            if (a2_op.type < a2::OperatorTypeCount)
            {
                u8 &op_cnt = result.a2->operatorCounts[ei];
                result.a2->operators[ei][op_cnt] = a2_op;
                result.a2->operatorRanks[ei][op_cnt] = opInfo.rank;
                result.operatorMappings[opInfo.op] = result.a2->operators[ei] + op_cnt;
                op_cnt++;
                LOG("a2_op.type=%d", (s32)(a2_op.type));
            }
        }
    }

    LOG("extractors:");

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        if (!result.a2->extractorCounts[ei])
            break;
        LOG("  ei=%d, #ex=%d", ei, (u32)result.a2->extractorCounts[ei]);
    }

    LOG("operators:");

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        if (!result.a2->operatorCounts[ei])
            break;
        LOG("  ei=%d, #op=%d", ei, (u32)result.a2->operatorCounts[ei]);
    }

    LOG("mem=%lu, start@%p", arena->used(), arena->mem);

    return result;
}

} // namespace analysis
