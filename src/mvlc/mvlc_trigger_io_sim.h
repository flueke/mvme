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
inline void extend(Timeline &input, const std::chrono::nanoseconds &toTime)
{
    if (!input.empty() && input.back().time < toTime)
        input.push_back({ toTime, input.back().edge });
}

inline void simulate(const struct IO &io, const Timeline &input, Timeline &output, const std::chrono::nanoseconds &maxtime)
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

    auto holdoffUntil = std::chrono::nanoseconds::min();

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

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_H__ */
