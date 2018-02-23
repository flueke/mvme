#ifndef __RATE_MONITOR_BASE_H__
#define __RATE_MONITOR_BASE_H__

#include <boost/circular_buffer.hpp>
#include <memory>
#include <QDebug>
#include <QRectF>
#include <QString>
#include "typedefs.h"
#include "util/counters.h"
#include "util/strings.h"
#include "util/tree.h"

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
    // setup
    double scale  = 1.0;
    double offset = 0.0;

    // state and data
    RateHistoryBufferPtr rateHistory;
    double lastValue = 0.0;
    double lastRate  = 0.0;


    void sample(double value)
    {
        lastRate = calcRate(value);

        if (rateHistory)
        {
            rateHistory->push_back(lastRate);
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
    };

    using Flag = u8;

    Type type = Type::Group;
    QString description;
    QString unitLabel;

    // for number formatting of rate values
    UnitScaling unitScaling = UnitScaling::Decimal;

    /* A pointer to the rate sampler for this entry. The RateSampler is owned
     * by the SamplerCollection that created it in
     * SamplerCollection::createTree(). */
    RateSampler *sampler = nullptr;

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
