#include "a2.h"
#include "analysis.h"
#include <cstdio>

using namespace a2;

#define LOG(fmt, ...)\
do\
{\
    fprintf(stderr, "%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);\
    fprintf(stderr, "\n");\
} while (0)

namespace
{

a2::Operator a2_adapter_magic(analysis::OperatorPtr op, a2::A2 *analysis)
{
    a2::Operator result = {};

    /* switch on op type. could maybe use some QObject meta information to
     * create a map of function pointers.
     *
     * always get inputs of op
     * 
     * need to map input arrays somehow
     *
     */


    return result;
}

}

namespace analysis
{

a2::A2 *a2_adapter_build(memory::Arena *arena,
                         const QVector<Analysis::SourceEntry> &sourceEntries,
                         const QVector<Analysis::OperatorEntry> &operatorEntries,
                         const QHash<QUuid, QPair<int, int>> &vmeConfigUuIdToIndexes)
{
    qDebug() << vmeConfigUuIdToIndexes;

    auto result = arena->push<a2::A2>({});

    for (u32 i = 0; i < result->extractorCounts.size(); i++)
    {
        assert(result->extractorCounts[i] == 0);
        assert(result->operatorCounts[i] == 0);
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
        result->extractors[ei] = arena->pushArray<a2::Extractor>(sources[ei].size());

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

            u8 &ex_cnt = result->extractorCounts[ei];
            result->extractors[ei][ex_cnt++] = ex_a2;
        }
    }

    LOG("extractors:");

    for (s32 ei = 0; ei < a2::MaxVMEEvents; ei++)
    {
        if (!result->extractorCounts[ei])
            break;
        LOG("  ei=%d, #ex=%d", ei, (u32)result->extractorCounts[ei]);
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

        result->operators[ei] = arena->pushArray<a2::Operator>(operators[ei].size());
        result->operatorRanks[ei] = arena->pushArray<u8>(operators[ei].size());

        for (auto opInfo: operators[ei])
        {
            auto ex_operator = a2_adapter_magic(opInfo.op, result);

            u8 &op_cnt = result->operatorCounts[ei];
            result->operators[ei][op_cnt] = ex_operator;
            result->operatorRanks[ei][op_cnt] = opInfo.rank;
            op_cnt++;
        }
    }

    return result;
}

} // namespace analysis
