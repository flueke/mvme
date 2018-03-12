#include "rate_monitor_base.h"
#include "analysis/a2/util/nan.h"

using a2::make_quiet_nan;

RateSamplerStatistics calc_rate_sampler_stats(const RateSampler &sampler, AxisInterval xInterval)
{
    auto minmax_values = get_minmax_values(sampler,
                                           xInterval,
                                           { make_quiet_nan(), make_quiet_nan() });

    RateSamplerStatistics result = {};
    result.intervals[Qt::XAxis] = xInterval;
    result.intervals[Qt::YAxis] = { minmax_values.first, minmax_values.second };

    return result;
}
