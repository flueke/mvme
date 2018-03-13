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
    op->outputs[outputIndex] = push_param_vector(arena, size, invalid_param());
    op->outputLowerLimits[outputIndex] = push_param_vector(arena, size, lowerLimit);
    op->outputUpperLimits[outputIndex] = push_param_vector(arena, size, upperLimit);
}


/* ===============================================
 * Operators
 * =============================================== */

enum OperatorType
{
    Operator_Calibration,
    Operator_Calibration_sse,
    Operator_Calibration_idx,
    Operator_KeepPrevious,
    Operator_KeepPrevious_idx,
    Operator_Difference,
    Operator_Difference_idx,
    Operator_ArrayMap,
    Operator_BinaryEquation,

    Operator_H1DSink,
    Operator_H1DSink_idx,
    Operator_H2DSink,

    Operator_RateMonitor_PrecalculatedRate,
    Operator_RateMonitor_CounterDifference,
    Operator_RateMonitor_FlowRate,

    Operator_ExportSinkPlain,
    Operator_ExportSinkIndexed,

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

    OperatorTypeCount
};

void calibration_step(Operator *op);
void calibration_sse_step(Operator *op);
void calibration_step_idx(Operator *op);
void keep_previous_step(Operator *op);
void difference_step(Operator *op);
void array_map_step(Operator *op);
void binary_equation_step(Operator *op);
void aggregate_sum_step(Operator *op);
void aggregate_multiplicity_step(Operator *op);
void aggregate_max_step(Operator *op);
void expression_operator_step(Operator *op);

void h1d_sink_step(Operator *op);
void h1d_sink_step_idx(Operator *op);
void h2d_sink_step(Operator *op);
void rate_monitor_step(Operator *op);
void rate_monitor_sample_flow(Operator *op);

} // namespace a2

#endif /* __MVME_A2__IMPL_H__ */
