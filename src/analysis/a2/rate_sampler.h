#ifndef __A2_RATE_SAMPLER_H__
#define __A2_RATE_SAMPLER_H__

/* Disable circular buffer debugging support. I'm trying to see if concurrent
 * read access from the GUI thread is ok or if the buffer has to be guarded by
 * an RW lock. If buffer debugging is enabled assertions will fire under high
 * load as the analysis thread is pushing values onto the buffer while the GUI
 * plots the buffer contents.
 *
 * Results are good so far: if buffer debugging is disabled concurrent reads
 * cause no issues. To implement a "clear history" operation I added a
 * read/writer lock to the RateSampler struct. The lock is used to guard write
 * operations from concurrent accesses, reads are not guarded right now. */
#define BOOST_CB_DISABLE_DEBUG
#include <boost/circular_buffer.hpp>

#include <cpp11-on-multicore/common/rwlock.h>
#include <cmath>
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
    //
    // setup
    //

    /* Scale factor to multiply recorded samples/rates by. */
    double scale  = 1.0;

    /* Offset for recorded samples/rates. */
    double offset = 0.0;

    /* Sampling interval in seconds. Not used for sampling but for x-axis scaling. */
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

    /* Lock and guard to deal with concurrent write accesses. Reads are left
     * unguarded for performance reasons. */
    NonRecursiveRWLock rwLock;

    using WriteGuard = WriteLockGuard<NonRecursiveRWLock>;

    void sample(double value)
    {
        WriteGuard guard(rwLock);

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
        WriteGuard guard(rwLock);

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

    size_t historySize() const { return rateHistory.size(); }
    size_t historyCapacity() const { return rateHistory.capacity(); }

    void clearHistory(bool keepSampleCount = false)
    {
        WriteGuard guard(rwLock);

        rateHistory.clear();
        assert(rateHistory.empty());

        if (!keepSampleCount)
            totalSamples = 0.0;
    }

    double getSample(size_t sampleIndex) const
    {
        assert(sampleIndex < rateHistory.size());
        return rateHistory.at(sampleIndex);
    }

    double getSampleTime(size_t sampleIndex) const
    {
        assert(sampleIndex < rateHistory.size());

        double result = (totalSamples - rateHistory.size() + sampleIndex) * interval;
        return result;
    }

    double getFirstSampleTime() const { return getSampleTime(0); }
    double getLastSampleTime() const { return getSampleTime(rateHistory.size() - 1); }

    ssize_t getSampleIndex(double sampleTime) const
    {
        ssize_t result = std::floor(sampleTime / interval - totalSamples + rateHistory.size());
        return result;
    }
};

using RateSamplerPtr = std::shared_ptr<RateSampler>;

} // namespace a2

#endif /* __A2_RATE_SAMPLER_H__ */
