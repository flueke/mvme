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
#ifndef __MVME_A2__IMPL_H__
#define __MVME_A2__IMPL_H__

#include "a2.h"
#include "memory.h"

namespace a2
{

inline void push_output_vectors(
    memory::Arena *arena,
    Operator *op,
    s32 outputIndex,
    s32 size,
    double lowerLimit = 0.0,
    double upperLimit = 0.0)
{
    assert(op->outputCount > outputIndex);

    op->outputs[outputIndex] = push_param_vector(arena, size, invalid_param());
    op->outputLowerLimits[outputIndex] = push_param_vector(arena, size, lowerLimit);
    op->outputUpperLimits[outputIndex] = push_param_vector(arena, size, upperLimit);
}

inline void push_output_vectors(
    memory::Arena *arena,
    DataSource *ds,
    s32 outputIndex,
    s32 size,
    double lowerLimit,
    double upperLimit)
{
    assert(ds->outputCount > outputIndex);

    ds->outputs[outputIndex] = push_param_vector(arena, size, invalid_param());
    ds->outputLowerLimits[outputIndex] = push_param_vector(arena, size, lowerLimit);
    ds->outputUpperLimits[outputIndex] = push_param_vector(arena, size, upperLimit);
    ds->hitCounts[outputIndex] = push_param_vector(arena, size, 0.0);
}

/* ===============================================
 * Operators
 * =============================================== */

enum OperatorType
{
    Invalid_OperatorType = 0,

    Operator_Calibration,
    Operator_Calibration_sse,
    Operator_Calibration_idx,
    Operator_KeepPrevious,
    Operator_KeepPrevious_idx,
    Operator_Difference,
    Operator_Difference_idx,
    Operator_ArrayMap,
    Operator_BinaryEquation,
    Operator_BinaryEquation_idx,

    Operator_H1DSink,
    Operator_H1DSink_idx,
    Operator_H2DSink,

    Operator_RateMonitor_PrecalculatedRate,
    Operator_RateMonitor_CounterDifference,
    Operator_RateMonitor_FlowRate,

    Operator_ExportSinkFull,
    Operator_ExportSinkSparse,
    Operator_ExportSinkCsv,

    Operator_RangeFilter,
    Operator_RangeFilter_idx,
    Operator_RectFilter,
    Operator_ConditionFilter,

    /* Aggregate Operations: produce one output value from an input array.
     * Can make use of thresholds to filter input values. */
    Operator_Aggregate_Sum,
    Operator_Aggregate_Multiplicity,
    Operator_Aggregate_Min,
    Operator_Aggregate_Max,
    Operator_Aggregate_Mean,
    Operator_Aggregate_Sigma,

    Operator_Aggregate_MinX,
    Operator_Aggregate_MaxX,
    Operator_Aggregate_MeanX,
    Operator_Aggregate_SigmaX,

    Operator_Expression,
    Operator_ScalerOverflow,
    Operator_ScalerOverflow_idx,

    Operator_IntervalCondition,
    Operator_RectangleCondition,
    Operator_PolygonCondition,
    Operator_LutCondition,

    OperatorTypeCount
};

void calibration_step(Operator *op, A2 *a2 = nullptr);
void calibration_sse_step(Operator *op, A2 *a2 = nullptr);
void calibration_step_idx(Operator *op, A2 *a2 = nullptr);
void keep_previous_step(Operator *op, A2 *a2 = nullptr);
void difference_step(Operator *op, A2 *a2 = nullptr);
void array_map_step(Operator *op, A2 *a2 = nullptr);
void binary_equation_step(Operator *op, A2 *a2 = nullptr);
void aggregate_sum_step(Operator *op, A2 *a2 = nullptr);
void aggregate_multiplicity_step(Operator *op, A2 *a2 = nullptr);
void aggregate_max_step(Operator *op, A2 *a2 = nullptr);

void h1d_sink_step(Operator *op, A2 *a2 = nullptr);
void h1d_sink_step_idx(Operator *op, A2 *a2 = nullptr);
void h2d_sink_step(Operator *op, A2 *a2 = nullptr);
void rate_monitor_step(Operator *op, A2 *a2 = nullptr);
void rate_monitor_sample_flow(Operator *op);

} // namespace a2

#endif /* __MVME_A2__IMPL_H__ */
