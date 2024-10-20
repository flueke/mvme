#include "waveform_interpolation.h"
#include <mesytec-mvlc/util/algo.h>

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

inline void emit(const span<const double> &xs, const span<const double> &ys, EmitterFun emitter)
{
    assert(xs.size() == ys.size());

    return mvlc::util::for_each(std::begin(xs), std::end(xs), std::begin(ys), emitter);
}

inline void emit(const double *xsBegin, const double *xsEnd, const double *ysBegin, EmitterFun emitter)
{
    return mvlc::util::for_each(xsBegin, xsEnd, ysBegin, emitter);
}

static const u32 MinInterpolationSamples = 6;

void interpolate(const span<const double> &xs, const span<const double> &ys, u32 factor, EmitterFun emitter)
{
    using mvlc::util::span;

    assert(xs.size() == ys.size());
    if (xs.size() != ys.size())
        return;

    if (factor <= 1 || xs.size() < MinInterpolationSamples)
    {
        emit(xs.begin(), xs.end(), ys.begin(), emitter);
        return;
    }

    const size_t WindowMid = (MinInterpolationSamples - 1) / 2;
    const double factor_1 = 1.0 / factor;
    const double dtSample = xs[1] - xs[0];
    const auto samplesEnd = std::end(xs);
    auto windowStart = std::begin(xs);
    auto windowEnd = windowStart + MinInterpolationSamples;

    // Emit the first few input samples before interpolation starts.
    emit(windowStart, windowStart+WindowMid, std::begin(ys), emitter);

    while (windowEnd <= samplesEnd)
    {
        assert(std::distance(windowStart, windowEnd) == MinInterpolationSamples);

        const auto windowOffset = std::distance(std::begin(xs), windowStart);
        span<const double> wxs(windowStart, MinInterpolationSamples);
        span<const double> wys(std::begin(ys) + windowOffset, MinInterpolationSamples);

        emitter(wxs[WindowMid], wys[WindowMid]); // Emit the original sample.

        const auto sampleX = wxs[WindowMid];

        for (size_t step=0; step<factor-1; ++step)
        {
            double phase = (step+1) * factor_1;
            double x = sampleX + phase * dtSample;
            double y = ipol(wys[0], wys[1], wys[2], wys[3], wys[4], wys[5], phase);
            emitter(x, y);
        }

        // Done with this window, advance both start and end by one.
        ++windowStart;
        ++windowEnd;
    }

    // Emit the last few samples after interpolation ends.
    const auto windowOffset = std::distance(std::begin(xs), windowStart);
    emit(windowStart+WindowMid, samplesEnd, std::begin(ys) + windowOffset + WindowMid, emitter);
}

void interpolate(const mvlc::util::span<const s16> &samples, double dtSample, u32 factor, EmitterFun emitter)
{
    std::vector<double> xs(samples.size());
    std::vector<double> ys(samples.size());

    auto fill_x = [idx=0u, dtSample] () mutable { return idx++ * dtSample; };
    auto fill_y = [](s16 y) { return static_cast<double>(y); };

    std::generate(std::begin(xs), std::end(xs), fill_x);
    std::transform(std::begin(samples), std::end(samples), std::begin(ys), fill_y);

    interpolate(xs, ys, factor, emitter);
}

}
