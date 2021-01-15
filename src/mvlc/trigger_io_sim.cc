#include "mvlc/trigger_io_sim.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

void simulate(
    const IO &io,
    const Timeline &input,
    Timeline &output,
    const SampleTime &maxtime)
{
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

void simulate(
    const Timer &timer,
    Timeline &output,
    const SampleTime &maxtime)
{
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

void simulate_sysclock(
    Timeline &output,
    const SampleTime &maxtime)
{
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

// Full LUT simulation with strobe input
void simulate(
    const LUT &lut,
    const LUT_Input_Timelines &inputs,
    const Timeline &strobeInput,
    LUT_Output_Timelines &outputs,
    Timeline &strobeOutput, // for diagnostics only
    const SampleTime &maxtime)
{
    // First implementation:
    // Find earliest change in the input timelines -> t0
    // Determine the state of all inputs at time t0 to calculate the input combination
    // Use the input combination to lookup the output state. Do this for each of the 3 output bits.
    // Produce the output samples
    // Find the next change with t > t0
    // Repeat
    //
    // If an output is strobed: determine the state of the strobe input at the
    // time we're currently looking at. Generate an output change only if the strobe is high.
    //
    // End condition: either no more changes in the inputs or all of the outputs have a time >= maxtime.

    // Get the earliest sample time in the input timelines that is later than
    // the given t0.
    // Returns SampleTime::min() if no matching sample is found.
    auto find_next_sample_time = [] (SampleTime t0, const auto &timelineRefs)
    {
        auto result = SampleTime::min();

        for (const auto &timelineRef: timelineRefs)
        {
            const auto &timeline = timelineRef.get();

            auto it = std::find_if(
                std::begin(timeline), std::end(timeline),
                [t0] (const Sample &sample)
                {
                    return sample.time > t0;
                });

            if (it != std::end(timeline)
                && (result == SampleTime::min()
                    || it->time < result))
            {
                result = it->time;
            }
        }

        return result;
    };

    // TODO: split into edge_at(t, timeline) and/or sample_at(t, timeline)
    auto state_at = [] (SampleTime t, const auto &timelineRefs) -> s32
    {
        unsigned result = 0;
        unsigned shift = 0;

        for (const auto &timelineRef: timelineRefs)
        {
            const auto &timeline = timelineRef.get();

            auto it = std::find_if(
                std::rbegin(timeline), std::rend(timeline),
                [t] (const Sample &sample)
                {
                    return sample.time <= t;
                });

            if (it == std::rend(timeline))
                return -1;

            result |= (static_cast<unsigned>(it->edge) << shift++);
        }

        return static_cast<s32>(result);
    };

    if (lut.strobedOutputs.any())
        simulate(lut.strobeGG, strobeInput, strobeOutput, maxtime);

    for (auto &output: outputs)
        output.get().push_back({ 0ns, Edge::Falling });

    // Start at t0=0 to find the first actual change in the input timelines.
    SampleTime t0(0);

    while (true)
    {
        t0 = find_next_sample_time(t0, inputs);

        if (t0 == SampleTime::min())
            break;

        s32 inputCombination = state_at(t0, inputs);

        if (inputCombination < 0)
            break;

        s32 strobeState = 0;

        if (lut.strobedOutputs.any())
        {
            std::array<std::reference_wrapper<const Timeline>, 1> strobeWrap = { strobeOutput };
            strobeState = state_at(t0, strobeWrap);

            if (strobeState < 0)
                break;
        }

        for (size_t outIdx = 0; outIdx < outputs.size(); ++outIdx)
        {
            if (lut.strobedOutputs.test(outIdx) && !strobeState)
                continue;

            auto outEdge = (lut.lutContents[outIdx].test(inputCombination)
                            ? Edge::Rising : Edge::Falling);

            outputs[outIdx].get().push_back({ t0, outEdge });
        }
    }
}

void simulate(Sim &sim, const Snapshot &inputSnapshot, const SampleTime &maxtime);

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
