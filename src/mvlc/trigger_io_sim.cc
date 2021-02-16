#include "mvlc/trigger_io_sim.h"

#include <boost/range/adaptor/indexed.hpp>
#include <QDebug>

#include "qt_util.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using boost::adaptors::indexed;

void simulate(
    const IO &io,
    const Trace &input,
    Trace &output,
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
    Trace &output,
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

//- FIXME: Timer sim: 8ns high, 8ns low, 8ns pause (lowest valid period is 24ns)
//  also wirklich 8ns oben, 16ns unten als max frequenz
//  hide timer delay column in GUI

    auto timerPeriod = timer.period * range_to_ns_factor(timer.range);

    if (timerPeriod < Timer::MinPeriod)
        timerPeriod = Timer::MinPeriod;

    const auto timerHalfPeriod = SampleTime(timerPeriod * 0.5);

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
    Trace &output,
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
    LUT_Output_Timelines &outputs,
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
    auto find_next_sample_time = [] (SampleTime t0, const auto &traces)
    {
        auto result = SampleTime::min();

        for (const auto &tracePtr: traces)
        {
            if (!tracePtr) continue;

            const auto &timeline = *tracePtr;

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
    auto state_at = [] (SampleTime t, const auto &traces) -> s32
    {
        unsigned result = 0;
        unsigned shift = 0;

        for (const auto &tracePtr: traces)
        {
            if (!tracePtr) continue;

            const auto &timeline = *tracePtr;

            auto it = std::find_if(
                std::rbegin(timeline), std::rend(timeline),
                [t] (const Sample &sample)
                {
                    return sample.time <= t;
                });

            if (it == std::rend(timeline))
                return -1;

            if (it->edge == Edge::Unknown)
                return -2;

            result |= (static_cast<unsigned>(it->edge) << shift++);
        }

        return static_cast<s32>(result);
    };

    if (lut.strobedOutputs.any())
    {
        assert(inputs[LUT::InputBits]);
        assert(outputs[LUT::OutputBits]);

        simulate(lut.strobeGG,
                 *inputs[LUT::InputBits],
                 *outputs[LUT::OutputBits],
                 maxtime);
    }

    // Get rid of the last input and output which refers to the strobes.
    std::array<const Trace *, LUT::InputBits> actualInputs;
    for (size_t i=0; i<actualInputs.size(); i++)
        actualInputs[i] = inputs[i];

    std::array<Trace *, LUT::OutputBits> actualOutputs;
    for (size_t i=0; i<actualOutputs.size(); i++)
        actualOutputs[i] = outputs[i];

    for (auto &output: actualOutputs)
        output->push_back({ 0ns, Edge::Falling });

    // Start at t0=0 to find the first actual change in the input timelines.
    SampleTime t0(0);

    while (true)
    {
        t0 = find_next_sample_time(t0, actualInputs);

        if (t0 == SampleTime::min())
            break;

        s32 inputCombination = state_at(t0, actualInputs);

        if (inputCombination < 0)
            break;

        assert(inputCombination < LUT::InputCombinations);

        s32 strobeState = 0;

        if (lut.strobedOutputs.any())
        {
            // Check if the strobe is high at t0
            // TODO: split up state_at() and get rid of the wrap hack
            std::array<const Trace *, 1> strobeWrap = { outputs[LUT::OutputBits] };
            strobeState = state_at(t0, strobeWrap);

            if (strobeState < 0)
                break;
        }

        for (size_t outIdx = 0; outIdx < actualOutputs.size(); ++outIdx)
        {
            if (lut.strobedOutputs.test(outIdx) && !strobeState)
                continue;

            auto outEdge = (lut.lutContents[outIdx].test(inputCombination)
                            ? Edge::Rising : Edge::Falling);

            actualOutputs[outIdx]->push_back({ t0, outEdge });
        }
    }
}

namespace
{
void clear_simulated_traces(Sim &sim)
{
    for (auto &trace: sim.l0_traces)
        trace.clear();

    for (auto &lutTraces: sim.l1_luts)
        for (auto &trace: lutTraces)
            trace.clear();

    for (auto &lutTraces: sim.l2_luts)
        for (auto &trace: lutTraces)
            trace.clear();

    for (auto &trace: sim.l3_traces)
        trace.clear();
}

template<typename V, typename B1, typename B2>
bool in_range(const V &value, const B1 &lo, const B2 &hi)
{
    return lo <= value && value < hi;
}

} // end anon namespace

Trace *lookup_output_trace(Sim &sim, const UnitAddress &addr)
{
    switch (addr[0])
    {
        case 0:
            if (addr[1] < sim.l0_traces.size())
                return &sim.l0_traces[addr[1]];
            break;

        case 1:
            if (addr[1] < sim.l1_luts.size())
            {
                auto &lutTraces = sim.l1_luts[addr[1]];

                if (addr[2] < lutTraces.size())
                    return &lutTraces[addr[2]];
            }
            break;

        case 2:
            if (addr[1] < sim.l2_luts.size())
            {
                auto &lutTraces = sim.l2_luts[addr[1]];
                if (addr[2] < lutTraces.size())
                    return &lutTraces[addr[2]];
            }
            break;

        case 3:
            if (addr[1] < sim.l3_traces.size())
                return &sim.l3_traces[addr[1]];
            break;
    }

    return nullptr;
}

void simulate(Sim &sim, const SampleTime &maxtime)
{
    clear_simulated_traces(sim);

    if (sim.sampledTraces.size() < ExpectedSampledTraces)
    {
        qDebug() << "error: expected more sampled traces";
        return;
    }

    // L0 NIM input gate generators with sampled input
    for (const auto &kv: sim.trigIO.l0.ioNIM | indexed(0))
    {
        const auto &io = kv.value();
        const auto &trace = sim.sampledTraces[kv.index()];
        simulate(io, trace, sim.l0_traces[kv.index() + Level0::NIM_IO_Offset], maxtime);
    }

    // L0 IRQ gate generators with sampled input
    for (const auto &kv: sim.trigIO.l0.ioIRQ | indexed(0))
    {
        const auto &io = kv.value();
        const auto &trace = sim.sampledTraces[kv.index() + NIM_IO_Count]; // irq input traces directly follow the NIM traces
        simulate(io, trace, sim.l0_traces[kv.index() + Level0::IRQ_Inputs_Offset], maxtime);
    }

    // L0 timers but only if softActivate is enabled.
    for (const auto &kv: sim.trigIO.l0.timers | indexed(0))
    {
        if (kv.value().softActivate)
            simulate(kv.value(), sim.l0_traces[kv.index()], maxtime);
    }

    //simulate_sysclock(sim.l0_traces[Level0::SysClockOffset], maxtime);

    // L1 LUT hierarchy. 0-2 first, then 3 & 4
    for (const auto &kvLUT: sim.trigIO.l1.luts | indexed(0u))
    {
        const auto &lut = kvLUT.value();
        const unsigned lutIndex = kvLUT.index();
        const auto &connections = Level1::StaticConnections[kvLUT.index()];

        LUT_Input_Timelines inputs;

        for (unsigned i=0; i<inputs.size(); i++)
            inputs[i] = lookup_output_trace(sim, connections[i].address);

        LUT_Output_Timelines outputs;

        for (unsigned i=0; i<outputs.size(); i++)
            outputs[i] = lookup_output_trace(sim, { 1, lutIndex, i });

        simulate(lut, inputs, outputs, maxtime);
    }

    // L2 LUTs with strobes and dynamic connections
    for (const auto &kvLUT: sim.trigIO.l2.luts | indexed(0))
    {
        const auto &lut = kvLUT.value();
        const unsigned lutIndex = kvLUT.index();

        LUT_Input_Timelines inputs;

        for (unsigned i=0; i<inputs.size(); i++)
        {
            UnitAddress dstAddr{ 2, lutIndex, i };
            UnitAddress srcAddr = get_connection_unit_address(
                sim.trigIO, dstAddr);
            inputs[i] = lookup_output_trace(sim, srcAddr);
            assert(inputs[i]);
        }

        LUT_Output_Timelines outputs;

        for (unsigned i=0; i<outputs.size(); i++)
        {
            outputs[i] = lookup_output_trace(sim, { 1, lutIndex, i });
            assert(outputs[i]);
        }

        simulate(lut, inputs, outputs, maxtime);
    }

    // L3 NIMs
    for (const auto &kv: sim.trigIO.l3.ioNIM | indexed(0))
    {
        const auto &io = kv.value();
        const unsigned ioIndex = kv.index() + Level3::NIM_IO_Unit_Offset;
        UnitAddress dstAddr{ 3, ioIndex, 0 };
        UnitAddress srcAddr = get_connection_unit_address(
            sim.trigIO, dstAddr);
        auto inputTrace = lookup_output_trace(sim, srcAddr);
        assert(inputTrace);
        simulate(io, *inputTrace,
                 sim.l3_traces[kv.index() + Level3::NIM_IO_Unit_Offset],
                 maxtime);
    }

    // L3 ECLs
    for (const auto &kv: sim.trigIO.l3.ioECL | indexed(0))
    {
        const auto &io = kv.value();
        const unsigned ioIndex = kv.index() + Level3::ECL_Unit_Offset;
        UnitAddress dstAddr{ 3, ioIndex, 0 };
        UnitAddress srcAddr = get_connection_unit_address(
            sim.trigIO, dstAddr);
        auto inputTrace = lookup_output_trace(sim, srcAddr);
        assert(inputTrace);
        simulate(io, *inputTrace,
                 sim.l3_traces[kv.index() + Level3::ECL_Unit_Offset],
                 maxtime);
    }
}

QStringList pin_path_list(const TriggerIO &trigIO, const PinAddress &pa)
{
    if (pa.unit[0] == 0)
    {
        if (pa.pos == PinPosition::Input)
            return { "sampled", lookup_default_name(trigIO, pa.unit) };
        else
            return { "L0", lookup_default_name(trigIO, pa.unit) };
    }

    if (pa.unit[0] == 1 || pa.unit[0] == 2)
    {
        QStringList result = {
            QSL("L%1").arg(pa.unit[0]),
            QSL("LUT%1").arg(pa.unit[1])
        };

        if (pa.pos == PinPosition::Input)
        {
            if (pa.unit[2] < LUT::InputBits)
                result << QSL("in%1").arg(pa.unit[2]);
            else
                result << QSL("strobeIn");
        }
        else
        {
            if (pa.unit[2] < LUT::OutputBits)
                result << QSL("out%1").arg(pa.unit[2]);
            else
                result << QSL("strobeOut");
        }

        return result;
    }

    if (pa.unit[0] == 3)
    {
        if (pa.pos == PinPosition::Input)
            return { "L3in", lookup_default_name(trigIO, pa.unit) };
        else
            return { "L3out", lookup_default_name(trigIO, pa.unit) };
    }

    return {};
}

QString pin_path(const TriggerIO &trigIO, const PinAddress &pa)
{
    return pin_path_list(trigIO, pa).join('.');
}

QString pin_name(const TriggerIO &trigIO, const PinAddress &pa)
{
    auto parts = pin_path_list(trigIO, pa);
    if (!parts.isEmpty())
        return parts.back();
    return "<pinName>";
}

QString pin_user_name(const TriggerIO &trigIO, const PinAddress &pa)
{
    if (pa.unit[0] == 0)
        return lookup_name(trigIO, pa.unit);

    if (pa.unit[0] == 1)
    {
        if (pa.pos == PinPosition::Output)
            return lookup_name(trigIO, pa.unit);
        auto con =  Level1::StaticConnections[pa.unit[1]][pa.unit[2]];
        return lookup_name(trigIO, con.address);
    }

    if (pa.unit[0] == 2)
    {
        if (pa.pos == PinPosition::Output)
        {
            if (pa.unit[2] < LUT::OutputBits)
                return lookup_name(trigIO, pa.unit);
            return {}; // strobeOut
        }

        auto con = Level2::StaticConnections[pa.unit[1]][pa.unit[2]];

        if (!con.isDynamic)
            return lookup_name(trigIO, con.address);

        auto srcAddr = get_connection_unit_address(trigIO, pa.unit);
        return lookup_name(trigIO, srcAddr);
    }

    if (pa.unit[0] == 3)
    {
        if (pa.pos == PinPosition::Output)
            return lookup_name(trigIO, pa.unit);
        auto srcAddr = get_connection_unit_address(trigIO, pa.unit);
        return lookup_name(trigIO, srcAddr);
    }

    return "<pinUserName>";
}

Trace *lookup_trace(Sim &sim, const PinAddress &pa)
{
    if (pa.pos == PinPosition::Output)
        return lookup_output_trace(sim, pa.unit);

    // Trace for an input pin is wanted.
    assert(pa.pos == PinPosition::Input);

    if (pa.unit[0] == 0)
    {
        unsigned pin = pa.unit[1];

        if (Level0::NIM_IO_Offset <= pin
            && pin < Level0::NIM_IO_Offset + NIM_IO_Count)
        {
            unsigned idx = pin - Level0::NIM_IO_Offset;

            assert(idx < sim.sampledTraces.size());

            if (idx < sim.sampledTraces.size())
                return &sim.sampledTraces[idx];
        }

        if (Level0::IRQ_Inputs_Offset <= pin
            && pin < Level0::IRQ_Inputs_Offset + Level0::IRQ_Inputs_Count)
        {
            unsigned idx = pin - Level0::IRQ_Inputs_Offset + NIM_IO_Count;;

            assert(idx < sim.sampledTraces.size());

            if (idx < sim.sampledTraces.size())
                return &sim.sampledTraces[idx];
        }
    }

    if (pa.unit[0] == 1)
    {
        auto con =  Level1::StaticConnections[pa.unit[1]][pa.unit[2]];
        return lookup_output_trace(sim, con.address);
    }

    if (pa.unit[0] == 2)
    {
        auto con = Level2::StaticConnections[pa.unit[1]][pa.unit[2]];

        if (!con.isDynamic)
            return lookup_output_trace(sim, con.address);

        auto srcAddr = get_connection_unit_address(sim.trigIO, pa.unit);
        return lookup_output_trace(sim, srcAddr);
    }

    if (pa.unit[0] == 3)
    {
        auto srcAddr = get_connection_unit_address(sim.trigIO, pa.unit);
        return lookup_output_trace(sim, srcAddr);
    }

    return nullptr;
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
