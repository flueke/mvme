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

/* RateSampler
 * Setup, storage and sampling logic for rate monitoring.
 */
struct RateSampler
{
    RateSampler(const RateSampler &) = delete;
    void operator=(const RateSampler &) = delete;


    // setup
    double scale  = 1.0;
    double offset = 0.0;

    // state and data
    RateHistoryBufferPtr rateHistory;
    double lastValue = 0.0;


    void sample(double value)
    {
        double delta = calc_delta0(value, lastValue);

        if (rateHistory)
        {
            rateHistory->push_back(getRate(value));
        }

        lastValue = value;
    }

    double getRate(double value) const
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
    UnitScaling unitScaling; // for numeric formatting
    double scaleFactor;
    double scaleOffset;
    double samplingPeriod_s;
    bool available;

    // if non-null sampling is enabled for this counter
    std::shared_ptr<RateSampler> rateSampler;
};

#endif /* __RATE_MONITOR_BASE_H__ */
