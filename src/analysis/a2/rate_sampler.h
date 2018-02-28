#ifndef __A2_RATE_SAMPLER_H__
#define __A2_RATE_SAMPLER_H__

#include <boost/circular_buffer.hpp>
#include <memory>
#include <cmath>
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
    //
    // setup
    //

    /* Scale factor to multiply recorded samples/rates by. */
    double scale  = 1.0;

    /* Offset for recorded samples/rates. */
    double offset = 0.0;

    /* Sampling interval in seconds. Not used for calculations but for x-axis scaling. */
    double interval = 1.0;

    //
    // state and data
    //

    /* Pointer to sample storage. */
    RateHistoryBuffer rateHistory;

    /* The last value that was sampled if sample() is used. */
    double lastValue    = 0.0;

    /* The last rate that was calculated/sampled. */
    double lastRate     = 0.0;

    /* The last delta value that was calculated if sample() is used. */
    double lastDelta    = 0.0;

    /* The total number of samples added to the rateHistory so far. Used for
     * x-axis scaling once the circular history buffer is full. */
    double totalSamples = 0.0;

    void sample(double value)
    {
        std::tie(lastRate, lastDelta) = calcRateAndDelta(value);
        lastRate = std::isnan(lastRate) ? 0.0 : lastRate;

        if (rateHistory.capacity())
        {
            rateHistory.push_back(lastRate);
            totalSamples++;
        }

        lastValue = value;
    }

    void record_rate(double rate)
    {
        lastRate = rate * scale + offset;
        lastRate = std::isnan(lastRate) ? 0.0 : lastRate;

        if (rateHistory.capacity())
        {
            rateHistory.push_back(lastRate);
            totalSamples++;
        }
    }

    std::pair<double, double> calcRateAndDelta(double value) const
    {
        double delta = calc_delta0(value, lastValue);
        double rate  = delta * scale + offset;
        return std::make_pair(rate, delta);
    }

    double calcRate(double value) const
    {
        return calcRateAndDelta(value).first;
    }

    void clearHistory()
    {
        rateHistory.clear();
        totalSamples = 0.0;
    }
};

using RateSamplerPtr = std::shared_ptr<RateSampler>;

} // namespace a2

#endif /* __A2_RATE_SAMPLER_H__ */
