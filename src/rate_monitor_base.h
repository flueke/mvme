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
#ifndef __RATE_MONITOR_BASE_H__
#define __RATE_MONITOR_BASE_H__

#include <array>
#include <boost/iterator/filter_iterator.hpp>
#include <QDebug>
#include <QRectF>
#include <QString>

#include "analysis/a2/rate_sampler.h"
#include "histo_util.h"
#include "typedefs.h"
#include "util/counters.h"
#include "util/cpp17_algo.h"
#include "util/qt_str.h"
#include "util/strings.h"
#include "util/tree.h"


using a2::RateHistoryBuffer;
using a2::RateSampler;
using a2::RateSamplerPtr;

enum class RateMonitorXScaleType
{
    Time,   // x axis scale shows time values (QwtDateScaleEngine)
    Samples // x axis scale shows sample numbers
};

inline QString to_string(RateMonitorXScaleType scaleType)
{
    switch (scaleType)
    {
        case RateMonitorXScaleType::Time:
            return QSL("Time");
        case RateMonitorXScaleType::Samples:
            return QSL("Samples");
    }
    return {};
}

inline RateMonitorXScaleType rate_monitor_xscale_type_from_string(const QString &str)
{
    RateMonitorXScaleType result = RateMonitorXScaleType::Time;

    if (str.compare(QSL("Time"), Qt::CaseInsensitive) == 0)
        result = RateMonitorXScaleType::Time;

    if (str.compare(QSL("Samples"), Qt::CaseInsensitive) == 0)
        result = RateMonitorXScaleType::Samples;

    return result;
}

struct not_nan_filter
{
    inline bool operator() (double d) { return !std::isnan(d); }
};

inline double get_max_value(const RateHistoryBuffer &rh, double defaultValue = 0.0)
{
    const auto f_begin = boost::make_filter_iterator<not_nan_filter, RateHistoryBuffer::const_iterator>(rh.begin());
    const auto f_end   = boost::make_filter_iterator<not_nan_filter, RateHistoryBuffer::const_iterator>(rh.end());

    auto max_it = std::max_element(f_begin, f_end);

    double max_value = (max_it == f_end) ? defaultValue : *max_it;
    return max_value;
}

inline std::pair<double, double> get_minmax_values(const RateHistoryBuffer &rh,
                                                   const std::pair<double, double> defaultValues = { 0.0, 0.0 })
{
    const auto f_begin = boost::make_filter_iterator<not_nan_filter, RateHistoryBuffer::const_iterator>(rh.begin());
    const auto f_end   = boost::make_filter_iterator<not_nan_filter, RateHistoryBuffer::const_iterator>(rh.end());

    auto minmax_iters = std::minmax_element(f_begin, f_end);

    auto result = std::make_pair(
        minmax_iters.first  == f_end ? defaultValues.first  : *minmax_iters.first,
        minmax_iters.second == f_end ? defaultValues.second : *minmax_iters.second);

    return result;
}

inline double get_max_value(const a2::RateSampler &sampler, double defaultValue = 0.0)
{
    a2::RateSampler::UniqueLock guard(sampler.mutex);
    return get_max_value(sampler.rateHistory, defaultValue);
}

inline std::pair<double, double> get_minmax_values(const a2::RateSampler &sampler,
                                                   AxisInterval timeInterval,
                                                   const std::pair<double, double> defaultValues = { 0.0, 0.0 })
{
    /* create iterator for timeInterval.minValue,
     * create iterator for timeInterval.maxValue,
     * find minmax elements in the iterator interval
     */

    ssize_t minIndex = sampler.getSampleIndex(timeInterval.minValue);
    ssize_t maxIndex = sampler.getSampleIndex(timeInterval.maxValue);
    const ssize_t size = sampler.historySize();

    minIndex = cpp17::std::clamp(minIndex, ssize_t(0), ssize_t(size-1));
    maxIndex = cpp17::std::clamp(maxIndex, ssize_t(0), ssize_t(size-1));

    a2::RateSampler::UniqueLock guard(sampler.mutex);

    if (0 <= minIndex && minIndex < size
        && 0 <= maxIndex && maxIndex <= size)
    {
        const auto &rh(sampler.rateHistory);

        const auto f_begin = boost::make_filter_iterator<not_nan_filter, RateHistoryBuffer::const_iterator>(rh.begin() + minIndex);
        const auto f_end   = boost::make_filter_iterator<not_nan_filter, RateHistoryBuffer::const_iterator>(rh.begin() + maxIndex);

        auto minmax_iters = std::minmax_element(f_begin, f_end);

        auto result = std::make_pair(
            minmax_iters.first  == f_end ? defaultValues.first  : *minmax_iters.first,
            minmax_iters.second == f_end ? defaultValues.second : *minmax_iters.second);

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
