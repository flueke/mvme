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
namespace analysis
{

/*
        struct OperatorEntry
        {
            QUuid eventId;
            OperatorPtr op;
            OperatorInterface *opRaw;

            // A user defined level used for UI display structuring.
            s32 userLevel;
        };
*/

a2::A2 *a2_adapter_build(memory::Arena *arena,
                         const QVector<Analysis::SourceEntry> &sourceEntries,
                         const QVector<Analysis::OperatorEntry> &operatorEntries,
                         const QHash<QUuid, QPair<int, int>> &vmeConfigUuIdToIndexes)
{
    qDebug() << vmeConfigUuIdToIndexes;
    auto result = arena->push<A2>({});
    result->extractorCounts.fill(0);// FIXME: do I need those?
    result->operatorCounts.fill(0);

    struct SourceWithModuleIndex
    {
        SourcePtr source;
        int moduleIndex;
    };

    std::array<QVector<SourceWithModuleIndex>, a2::MaxVMEEvents> sources;

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

            auto ex_a2 = make_extractor(
                arena,
                filter,
                ex->m_requiredCompletionCount,
                ex->m_rngSeed);

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






    //std::array<QVector<OperatorPtr>, a2::MaxVMEEvents> operatorsByEvent;


    return result;
}

} // namespace analysis
