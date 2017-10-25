#ifndef __A2_ADAPTER_H__
#define __A2_ADAPTER_H__

#include "analysis.h"
#include "a2/a2.h"

namespace analysis
{

/*
 * operators must be sorted by rank and their beginRun() must have been called.
 *
 * vmeConfigUuIdToIndexes maps a QUuid from EventConfig/ModuleConfig to a
 * pair of (eventIndex, moduleIndex).
 * For EventConfigs only the eventIndex is set. For ModuleConfigs both indexes
 * are set.
 */
a2::A2 *a2_adapter_build(memory::Arena *arena,
                         const QVector<Analysis::SourceEntry> &sources,
                         const QVector<Analysis::OperatorEntry> &operators,
                         const QHash<QUuid, QPair<int, int>> &vmeConfigUuIdToIndexes);

} // namespace analysis

#endif /* __A2_ADAPTER_H__ */
