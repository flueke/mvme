#ifndef __RATE_MONITOR_BASE_H__
#define __RATE_MONITOR_BASE_H__

#include <boost/circular_buffer.hpp>
#include <memory>
#include <QRectF>
#include <QString>
#include "util/strings.h"

using RateHistoryBuffer = boost::circular_buffer<double>;
using RateHistoryBufferPtr = std::shared_ptr<RateHistoryBuffer>;

inline double get_max_value(const RateHistoryBuffer &rh)
{
    auto max_it = std::max_element(rh.begin(), rh.end());
    double max_value = (max_it == rh.end()) ? 0.0 : *max_it;
    return max_value;
}

inline QRectF get_qwt_bounding_rect(const RateHistoryBuffer &rh)
{

    double max_value = get_max_value(rh);
    auto result = QRectF(0.0, 0.0, rh.capacity(), max_value);
    return result;
}

struct RateMonitorEntry
{
    enum class Type
    {
        Group,
        SystemRate,
        // TODO: add AnalysisRate
    };

    Type type;
    QString description;
    QString unitLabel;
    UnitScaling unitScaling;
    double calibrationFactor;
    double calibrationOffset;
    double samplingPeriod_ms;

    // if non-null sampling is enabled for this counter
    std::shared_ptr<RateSampler> rateSampler;
};

#endif /* __RATE_MONITOR_BASE_H__ */
