#ifndef __A2_RATE_SAMPLER_H__
#define __A2_RATE_SAMPLER_H__

#include <boost/circular_buffer.hpp>
#include <memory>
#include "util/counters.h"

namespace a2
{

/* RateHistory - circular buffer for rate values */
using RateHistoryBuffer = boost::circular_buffer<double>;
using RateHistoryBufferPtr = std::shared_ptr<RateHistoryBuffer>;

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
    double lastDelta = 0.0;

    void sample(double value)
    {
        std::tie(lastRate, lastDelta) = calcRateAndDelta(value);

        if (rateHistory)
        {
            rateHistory->push_back(lastRate);
        }

        lastValue = value;
    }

    void record_rate(double rate)
    {
        lastRate = (rate + offset) * scale;

        if (rateHistory)
        {
            rateHistory->push_back(lastRate);
        }
    }

    std::pair<double, double> calcRateAndDelta(double value) const
    {
        double delta = calc_delta0(value, lastValue);
        double rate  = (delta + offset) * scale;
        return std::make_pair(rate, delta);
    }

    double calcRate(double value) const
    {
        return calcRateAndDelta(value).first;
    }
};

using RateSamplerPtr = std::shared_ptr<RateSampler>;

} // namespace a2

#endif /* __A2_RATE_SAMPLER_H__ */
