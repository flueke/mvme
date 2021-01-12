#ifndef __MVME_MVLC_TRIGGER_IO_SIM_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_H__

#include <chrono>
#include <iterator>
#include "mvlc/mvlc_trigger_io.h"
#include "mvlc/trigger_io_scope.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using namespace trigger_io_scope;

// Extend the timeline to toTime using the last input samples edge.
// Does nothing if input is empty or the last sample time in input is >= toTime.
inline void extend(Timeline &input, const SampleTime &toTime)
{
    if (!input.empty() && input.back().time < toTime)
        input.push_back({ toTime, input.back().edge });
}

inline void simulate(
    const IO &io,
    const Timeline &input,
    Timeline &output,
    const SampleTime &maxtime)
{
    using namespace std::chrono_literals;

    // Zero width means the IO outputs levels instead of pulses.
    if (io.width == 0)
    {
        // Copy all input samples to the output, inverting if required.
        std::transform(
            std::begin(input), std::end(input), std::back_inserter(output),
            [&io] (const Sample &sample) -> Sample
            {
                return { sample.time, io.invert ? invert(sample.edge) : sample.edge };
            });

        // Extend to maxtime using the last samples edge.
        extend(output, maxtime);

        return;
    }

    // The non-zero width case.
    // If the invert flag is set the IO reacts to Falling instead of Rising
    // edges. The generated output pulse is not inverted!
    // Implicit holdoff is always active at least for the duration of the output pulse.
    // Explicit holdoff is counted from the time of the triggering input edge.

    const auto inEnd = std::end(input);

    auto holdoffUntil = SampleTime::min();

    output.push_back({ 0ns, Edge::Falling }); // Start off with an initial 0 'state'.

    for (auto it = std::begin(input); it != inEnd; ++it)
    {
        const auto inSample = *it;

        if (inSample.time <= holdoffUntil)
            continue;

        if ((inSample.edge == Edge::Rising && !io.invert)
            || (inSample.edge == Edge::Falling
                && io.invert
                && inSample.time != 0ns)) // ignore the initial Falling state starting off each timeline
        {
            Sample outRising = { inSample.time + std::chrono::nanoseconds(io.delay), Edge::Rising };
            Sample outFalling = { outRising.time + std::chrono::nanoseconds(io.width), Edge::Falling };

            output.emplace_back(outRising);
            output.emplace_back(outFalling);

            holdoffUntil = std::max(outFalling.time, inSample.time + std::chrono::nanoseconds(io.holdoff));

            if (outFalling.time >= maxtime)
                break;
        }
    }
}

inline void simulate(
    const Timer &timer,
    Timeline &output,
    const SampleTime &maxtime)
{
    using namespace std::chrono_literals;

    auto range_to_ns_factor = [] (const Timer::Range &timerRange)
    {
        switch (timerRange)
        {
            case Timer::Range::ns:
                return 1u;
            case Timer::Range::us:
                return 1000u;
            case Timer::Range::ms:
                return 1000 * 1000u;
            case Timer::Range::s:
                return 1000 * 1000 * 1000u;
        }

        return 0u;
    };

    const auto timerHalfPeriod = SampleTime(
        timer.period * range_to_ns_factor(timer.range) * 0.5);

    // Initial output level
    output.push_back({ 0ns, Edge::Falling });

    // First delayed pulse
    output.push_back({ SampleTime(timer.delay_ns), Edge::Rising });
    output.push_back({ SampleTime(timer.delay_ns + timerHalfPeriod.count()), Edge::Falling });

    // Generate the rest of the pulses up to maxtime based of the time of the
    // last falling edge.
    // Note: might be better to remember the number of pulses generated and use
    // multiplication to calculate the next times. This way errors won't accumulate.
    while (output.back().time < maxtime)
    {
        Sample outRising = { output.back().time + timerHalfPeriod, Edge::Rising };
        Sample outFalling = { outRising.time + timerHalfPeriod, Edge::Falling };

        output.emplace_back(outRising);
        output.emplace_back(outFalling);
    }
}

inline void simulate_sysclock(
    Timeline &output,
    const SampleTime &maxtime)
{
    using namespace std::chrono_literals;
    static const auto SysClockHalfPeriod = 62.5ns * 0.5;

    output.push_back({ 0ns, Edge::Falling });

    while (output.back().time < maxtime)
    {
        Sample outRising = { output.back().time + SysClockHalfPeriod, Edge::Rising };
        Sample outFalling = { outRising.time + SysClockHalfPeriod, Edge::Falling };

        output.emplace_back(outRising);
        output.emplace_back(outFalling);
    }
}

using LUT_Input_Timelines = std::array<std::reference_wrapper<const Timeline>, LUT::InputBits>;
using LUT_Output_Timelines = std::array<std::reference_wrapper<Timeline>, LUT::OutputBits>;

// Full LUT simulation with strobe input
inline void simulate(
    const LUT &lut,
    const LUT_Input_Timelines &inputs,
    const Timeline &strobeInput,
    LUT_Output_Timelines &outputs,
    Timeline &strobeOutput, // for diagnostics only
    const std::chrono::nanoseconds &maxtime)
{
    // Ignore the strobe for now. Looking up the state of the strobe will be
    // the same as needs to be done for the inputs.

    // xxx: leftoff here

}

// LUT simulation without the strobe input
inline void simulate(
    const LUT &lut,
    const LUT_Input_Timelines &inputs,
    LUT_Output_Timelines &outputs,
    const std::chrono::nanoseconds &maxtime)
{
    Timeline strobeOut;
    simulate(lut, inputs, {}, outputs, strobeOut, maxtime);
}


} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_H__ */
