#ifndef __RATE_MONITOR_BASE_H__
#define __RATE_MONITOR_BASE_H__

#include <QDebug>
#include <QRectF>
#include <QString>
#include "analysis/a2/rate_sampler.h"
#include "histo_util.h"
#include "typedefs.h"
#include "util/counters.h"
#include "util/strings.h"
#include "util/tree.h"

using a2::RateHistoryBuffer;
using a2::RateSampler;
using a2::RateSamplerPtr;

inline double get_max_value(const RateHistoryBuffer &rh, double defaultValue = 0.0)
{
    auto max_it = std::max_element(rh.begin(), rh.end());
    double max_value = (max_it == rh.end()) ? defaultValue : *max_it;
    return max_value;
}

inline std::pair<double, double> get_minmax_values(const RateHistoryBuffer &rh,
                                                   const std::pair<double, double> defaultValues = { 0.0, 0.0 })
{
    auto minmax_iters = std::minmax_element(rh.begin(), rh.end());

    auto result = std::make_pair(
        minmax_iters.first  == rh.end() ? defaultValues.first  : *minmax_iters.first,
        minmax_iters.second == rh.end() ? defaultValues.second : *minmax_iters.second);

    return result;
}

inline std::pair<double, double> get_minmax_values(const a2::RateSampler &sampler,
                                                   AxisInterval timeInterval,
                                                   const std::pair<double, double> defaultValues = { 0.0, 0.0 })
{
    /* find iterator for timeInterval.minValue,
     * find iterator for timeInterval.maxValue,
     * find minmax elements in the iterator interval
     */

    ssize_t minIndex = sampler.getSampleIndex(timeInterval.minValue);
    ssize_t maxIndex = sampler.getSampleIndex(timeInterval.maxValue);
    const ssize_t size = sampler.rateHistory.size();

    qDebug() << __PRETTY_FUNCTION__ << "minValue =" << timeInterval.minValue << " -> index =" << minIndex;
    qDebug() << __PRETTY_FUNCTION__ << "maxValue =" << timeInterval.maxValue << " -> index =" << maxIndex;

    if (0 <= minIndex && minIndex < size
        && 0 <= maxIndex && maxIndex <= size)
    {
        const auto &rh(sampler.rateHistory);

        auto minmax_iters = std::minmax_element(rh.begin() + minIndex,
                                                rh.begin() + maxIndex);

        auto result = std::make_pair(
            minmax_iters.first  == rh.end() ? defaultValues.first  : *minmax_iters.first,
            minmax_iters.second == rh.end() ? defaultValues.second : *minmax_iters.second);

        return result;
    }

    return defaultValues;
}

struct RateSamplerStatistics
{
    using Intervals = std::array<AxisInterval, 2>;
    Intervals intervals;
};

// xInterval is given in terms of samples
RateSamplerStatistics calc_rate_sampler_stats(const RateSampler &sampler, AxisInterval xInterval);

struct RateMonitorEntry
{
    enum class Type
    {
        Group,
        SystemRate,
    };

    using Flag = u8;

    Type type = Type::Group;
    QString description;
    QString unitLabel;

    // for number formatting of rate values
    UnitScaling unitScaling = UnitScaling::Decimal;

    RateSamplerPtr sampler;

    Flag flags = 0u;

    bool operator==(const RateMonitorEntry &other) const
    {
        if (this == &other)
            return true;

        return description == other.description
            && unitLabel == other.unitLabel
            && unitScaling == other.unitScaling
            && sampler == other.sampler
            && flags == other.flags;
    }
};

using RateMonitorNode = util::tree::Node<RateMonitorEntry>;

#endif /* __RATE_MONITOR_BASE_H__ */
