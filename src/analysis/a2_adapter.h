#ifndef __A2_ADAPTER_H__
#define __A2_ADAPTER_H__

#include "analysis.h"
#include "a2/a2.h"
#include "../util/bihash.h"

#include <iterator>

namespace analysis
{

struct A2AdapterState
{
    a2::A2 *a2;

    using SourceHash   = BiHash<SourceInterface *, a2::DataSource *>;
    using OperatorHash = BiHash<OperatorInterface *, a2::Operator *>;

    SourceHash   sourceMap;
    OperatorHash operatorMap;
};


/*
 * operators must be sorted by rank and their beginRun() must have been called.
 *
 * vmeConfigUuIdToIndexes maps a QUuid from EventConfig/ModuleConfig to a
 * pair of (eventIndex, moduleIndex).
 * For EventConfigs only the eventIndex is set. For ModuleConfigs both indexes
 * are set.
 */
A2AdapterState a2_adapter_build(
    memory::Arena *arena,
    memory::Arena *workArena,
    const QVector<Analysis::SourceEntry> &sources,
    const QVector<Analysis::OperatorEntry> &operators,
    const vme_analysis_common::VMEIdToIndex &vmeMap);

a2::PipeVectors find_output_pipe(const A2AdapterState *state, analysis::Pipe *pipe);

template<typename T, typename SizeType>
QVector<T> to_qvector(TypedBlock<T, SizeType> block)
{
    QVector<T> result;

    result.reserve(block.size);

    std::copy(block.begin(), block.end(), std::back_inserter(result));

    return result;
}

} // namespace analysis

#endif /* __A2_ADAPTER_H__ */
