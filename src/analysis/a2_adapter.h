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
    using SourceHash   = BiHash<SourceInterface *, a2::DataSource *>;
    using OperatorHash = BiHash<OperatorInterface *, a2::Operator *>;
    using ConditionBitIndexes = BiHash<ConditionInterface *, s16>;

    struct ErrorInfo
    {
        OperatorPtr op;
        s32 eventIndex;
        QString reason;
        std::exception_ptr ep;
    };

    using ErrorInfoVector = QVector<ErrorInfo>;

    Analysis *a1;
    a2::A2 *a2;

    SourceHash   sourceMap;
    OperatorHash operatorMap;
    ErrorInfoVector operatorErrors;
    ConditionBitIndexes conditionBitIndexes;
};

/*
 * operators must be sorted by rank and their beginRun() must have been called.
 *
 * The vmeMap maps a QUuid from EventConfig/ModuleConfig to a
 * pair of (eventIndex, moduleIndex).
 * For EventConfigs only the eventIndex is set. For ModuleConfigs both indexes
 * are set.
 */
A2AdapterState a2_adapter_build(
    memory::Arena *arena,
    memory::Arena *workArena,
    Analysis *analysis,
    const analysis::SourceVector &sources,
    const analysis::OperatorVector &operators,
    const vme_analysis_common::VMEIdToIndex &vmeMap,
    const RunInfo &runInfo);

a2::PipeVectors find_output_pipe(const A2AdapterState *state, analysis::Pipe *pipe);

template<typename T, typename SizeType>
QVector<T> to_qvector(TypedBlock<T, SizeType> block)
{
    QVector<T> result;

    result.reserve(block.size);

    std::copy(block.begin(), block.end(), std::back_inserter(result));

    return result;
}

a2::PipeVectors make_a2_pipe_from_a1_pipe(memory::Arena *arena, analysis::Pipe *a1_inPipe);

} // namespace analysis

#endif /* __A2_ADAPTER_H__ */
