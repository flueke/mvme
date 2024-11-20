/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
        s32 eventIndex = -1;
        SourcePtr src;
        s32 moduleIndex = -1;
        QString reason;
        std::exception_ptr ep;

        ErrorInfo(const OperatorPtr &op, s32 eventIndex, const QString &reason, const std::exception_ptr &ep):
            op(op),
            eventIndex(eventIndex),
            reason(reason),
            ep(ep)
        {
        }

        ErrorInfo(const SourcePtr &src, s32 moduleInex, const QString &reason, const std::exception_ptr &ep):
            src(src),
            moduleIndex(moduleInex),
            reason(reason),
            ep(ep)
        {
        }
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

std::pair<a2::PipeVectors, bool>
    find_output_pipe(const A2AdapterState *state, analysis::Pipe *pipe);

template<typename T, typename SizeType>
QVector<T> to_qvector(TypedBlock<T, SizeType> block)
{
    QVector<T> result;

    result.reserve(block.size);

    std::copy(block.begin(), block.end(), std::back_inserter(result));

    return result;
}

a2::PipeVectors make_a2_pipe_from_a1_pipe(memory::Arena *arena, analysis::Pipe *a1_inPipe);

template<typename RuntimeData, typename CfgOperator>
RuntimeData *get_runtime_operator_data(const A2AdapterState &state, const CfgOperator *cfg_op)
{
    // The const cast is needed for the bihash lookup to work. Probably need
    // special handling for pointers in the BiHash.
    if (auto a2_op = state.operatorMap.value(const_cast<CfgOperator *>(cfg_op), nullptr))
        return reinterpret_cast<RuntimeData *>(a2_op->d);
    return nullptr;
}

inline a2::H1DSinkData *get_runtime_h1dsink_data(const A2AdapterState &state, const Histo1DSink *cfg_sink)
{
    return get_runtime_operator_data<a2::H1DSinkData>(state, cfg_sink);
}

inline a2::H2DSinkData *get_runtime_h2dsink_data(const A2AdapterState &state, const Histo2DSink *cfg_sink)
{
    return get_runtime_operator_data<a2::H2DSinkData>(state, cfg_sink);
}

} // namespace analysis

#endif /* __A2_ADAPTER_H__ */
