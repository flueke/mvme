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
#ifndef __A2_RATE_SAMPLER_H__
#define __A2_RATE_SAMPLER_H__

/* Disables circular buffer debugging support. Debugging asserts if internal
 * invariants are violated. */
//#define BOOST_CB_DISABLE_DEBUG
#include <boost/circular_buffer.hpp>

//#include <cpp11-on-multicore/common/rwlock.h>
#include <cmath>
#include <memory>
#include <iostream>

#include "util/counters.h"
#include "util/nan.h"
#include "util/util_threading.h"

namespace a2
{

/* RateHistory - circular buffer for rate values */
using RateHistoryBuffer = boost::circular_buffer<double>;

/* RateSampler
 * Setup, storage and sampling logic for rate monitoring.
 */
struct RateSampler
{
    //
    // setup
    //

    /* Scale factor to multiply recorded samples/rates by. This scales the rate
     * values, not the time axis. */
    double scale  = 1.0;

    /* Offset for recorded samples/rates. Scales the rate values, not the time
     * axis. */
    double offset = 0.0;

    /* Sampling interval in seconds.
     * Used to normalize rates to seconds and for x-axis scaling. */
    double interval = 1.0;

    //
    // state and data
    //

    /* Sample storage. */
    RateHistoryBuffer rateHistory;

    /* The last value that was sampled if sample() is used. */
    double lastValue    = make_quiet_nan();

    /* The last rate that was calculated/sampled. */
    double lastRate     = 0.0;

    /* The last delta value that was calculated if sample() is used. */
    double lastDelta    = 0.0;

    /* The total number of samples added to the rateHistory so far. Used for
     * x-axis scaling once the circular history buffer is full. */
    double totalSamples = 0.0;

    /* Lock and guard to deal with concurrent accesses. */
    using Mutex = TicketMutex;
    using UniqueLock = std::unique_lock<Mutex>;
    mutable TicketMutex mutex;

    void sample(double value)
    {
        std::tie(lastRate, lastDelta) = calcRateAndDelta(value);

        UniqueLock guard(mutex);

        if (rateHistory.capacity())
        {
            rateHistory.push_back(lastRate);
            totalSamples++;
        }

        lastValue = value;
    }

    void recordRate(double rate)
    {
        UniqueLock guard(mutex);

        lastRate = rate * scale + offset;

        if (rateHistory.capacity())
        {
            rateHistory.push_back(lastRate);
            totalSamples++;
        }
    }

    std::pair<double, double> calcRateAndDelta(double value) const
    {
        UniqueLock guard(mutex);

        if (std::isnan(lastValue))
            return std::make_pair(0.0, 0.0);

        double delta = calc_delta0(value, lastValue);
        double rate  = (delta * scale + offset) / interval;
        return std::make_pair(rate, delta);
    }

    double calcRate(double value) const
    {
        return calcRateAndDelta(value).first;
    }

    size_t historySize() const
    {
        UniqueLock guard(mutex);
        return rateHistory.size();
    }

    size_t historyCapacity() const
    {
        UniqueLock guard(mutex);
        return rateHistory.capacity();
    }

    // Clears history contents but keeps its capacity
    void clearHistory(bool keepSampleCount = false)
    {
        UniqueLock guard(mutex);

        rateHistory.clear();
        assert(rateHistory.empty());

        lastValue = make_quiet_nan();
        lastRate = 0.0;
        lastDelta = 0.0;

        if (!keepSampleCount)
            totalSamples = 0.0;
    }

    double getSample(size_t sampleIndex) const
    {
        UniqueLock guard(mutex);
        assert(sampleIndex < rateHistory.size());
        return rateHistory.at(sampleIndex);
    }

    double getSampleTime(size_t sampleIndex) const
    {
        UniqueLock guard(mutex);
        assert(sampleIndex < rateHistory.size());
        double result = (totalSamples - rateHistory.size() + sampleIndex) * interval;
        return result;
    }

    double getFirstSampleTime() const { return getSampleTime(0); }
    double getLastSampleTime() const { return getSampleTime(historySize() - 1); }

    ssize_t getSampleIndex(double sampleTime) const
    {
        UniqueLock guard(mutex);
        ssize_t result = std::floor(sampleTime / interval - totalSamples + rateHistory.size());
        return result;
    }
};

using RateSamplerPtr = std::shared_ptr<RateSampler>;

} // namespace a2

#endif /* __A2_RATE_SAMPLER_H__ */
