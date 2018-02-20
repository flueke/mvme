#ifndef __RATE_MONITOR_BASE_H__
#define __RATE_MONITOR_BASE_H__

#include <boost/circular_buffer.hpp>
#include <memory>
#include <QRectF>
#include <QString>
#include "util/counters.h"
#include "util/strings.h"

/* RateHistory - circular buffer for rate values */
using RateHistoryBuffer = boost::circular_buffer<double>;
using RateHistoryBufferPtr = std::shared_ptr<RateHistoryBuffer>;

inline double get_max_value(const RateHistoryBuffer &rh, double defaultValue = 0.0)
{
    auto max_it = std::max_element(rh.begin(), rh.end());
    double max_value = (max_it == rh.end()) ? defaultValue : *max_it;
    return max_value;
}

inline QRectF get_qwt_bounding_rect(const RateHistoryBuffer &rh)
{

    double max_value = get_max_value(rh);
    auto result = QRectF(0.0, 0.0, rh.capacity(), max_value);
    return result;
}

/* RateSampler
 * Setup, storage and sampling logic for rate monitoring.
 */
struct RateSampler
{
#if 0
    RateSampler(const RateSampler &) = delete;
    RateSampler &operator=(const RateSampler &) = delete;

    RateSampler(RateSampler &&) = default;
    RateSampler &operator=(RateSampler &&) = default;

#endif

    // setup
    double scale  = 1.0;
    double offset = 0.0;

    // state and data
    RateHistoryBufferPtr rateHistory;
    double lastValue = 0.0;


    void sample(double value)
    {
        if (rateHistory)
        {
            rateHistory->push_back(calcRate(value));
        }

        lastValue = value;
    }

    double calcRate(double value) const
    {
        double delta = calc_delta0(value, lastValue);
        double rate  = (delta + offset) * scale;
        return rate;
    }
};

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

    // for numeric formatting
    UnitScaling unitScaling;

    // if non-null sampling is enabled for this counter
    std::shared_ptr<RateSampler> rateSampler;

    double scaleFactor;
    double scaleOffset;
    double samplingPeriod_s;
    bool available;
};

#endif /* __RATE_MONITOR_BASE_H__ */
