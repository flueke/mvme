#include "waveform_interpolation.h"

namespace mesytec::mvme::waveforms
{

// *************** SINC by r.schneider@mesytec.com ****************************
static double sinc(double phase) // one period runs 0...1, minima at +-0.5
{
#define LIMIT_PERIODES 3.0 // limit sinc function to +-2

    double sinc;

    if ((phase != 0) && (phase >= -LIMIT_PERIODES) && (phase <= LIMIT_PERIODES))
    {
        sinc = ((sin(phase * M_PI)) / (phase * M_PI)) * ((sin(phase * M_PI / LIMIT_PERIODES)) / (phase * M_PI / LIMIT_PERIODES));
    }
    else if (phase == 0)
    {
        sinc = 1;
    }
    else
    {
        sinc = 0;
    }

    return (sinc);
}

// phase= 0...1 (= a2...a3)
static double ipol(double a0, double a1, double a2, double a3, double a4, double a5, double phase) // one period runs 0...1, minima at +-0.5
{
#define LIN_INT 0 // 1 for linear interpolation

    double ipol;
    if (!LIN_INT)
    {
        // phase runs from 0 (position A1) to 1 (position A2)
        ipol = a0 * sinc(-2.0 - phase) + a1 * sinc(-1 - phase) + a2 * sinc(-phase) + a3 * sinc(1.0 - phase) + a4 * sinc(2.0 - phase) + a5 * sinc(3.0 - phase);
    }
    else
        ipol = a2 + (phase * (a3 - a2)); // only linear interpolation

    return (ipol);
}

static const u32 MinInterpolationSamples = 6;

void interpolate(const mvlc::util::span<const Sample> &samples, u32 factor, EmitterFun ef)
{
    if (factor <= 1 || samples.size() < MinInterpolationSamples)
    {
        std::for_each(std::begin(samples), std::end(samples), ef);
        return;
    }

    const size_t WindowMid = (MinInterpolationSamples - 1) / 2;
    const double factor_1 = 1.0 / factor;
    const double dtSample = samples[1].first - samples[0].first;
    const auto samplesEnd = std::end(samples);
    auto windowStart = samples.data();
    auto windowEnd = windowStart + MinInterpolationSamples;

    // Emit the first few input samples before interpolation starts.
    std::for_each(windowStart, windowStart+WindowMid, ef);

    while (windowEnd <= samplesEnd)
    {
        assert(std::distance(windowStart, windowEnd) == MinInterpolationSamples);
        mvlc::util::span<const Sample> window(windowStart, MinInterpolationSamples);

        ef(*windowStart); // Emit the original sample.

        auto sampleX = (windowStart + WindowMid)->first;

        for (size_t step=0; step<factor-1; ++step)
        {
            double phase = (step+1) * factor_1;
            double y = ipol(
                window[0].first, window[1].first, window[2].first,
                window[3].first, window[4].first, window[5].first,
                phase);

            auto x = sampleX + phase * dtSample;

            ef(std::make_pair(x, y));
        }

        // Done with this window, advance both start and end by one.
        ++windowStart;
        ++windowEnd;
    }

    // Emit the last few samples after interpolation ends.
    std::for_each(windowStart+WindowMid, samplesEnd, ef);
}

void interpolate(const mvlc::util::span<s16> &samples, double dtSample, u32 factor, EmitterFun ef)
{
    std::vector<Sample> buffer;
    buffer.reserve(samples.size());

    for (size_t xi=0; xi<samples.size(); ++xi)
    {
        double x = xi * dtSample;
        double y = samples[xi];
        buffer.push_back(std::make_pair(x, y));
    }

    interpolate(buffer, factor, ef);
}

}
