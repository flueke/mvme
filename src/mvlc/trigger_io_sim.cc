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

void simulate_gg(
    const IO &io,
    const Trace &input,
    Trace &output,
    const SampleTime &maxtime)
{
    // Zero width means the IO outputs levels instead of pulses.
    // Also the delay setting does not have an effect.
    if (io.width == 0)
    {
        // Copy all input samples to the output. The invert flag does not
        // effect the output level!
        std::transform(
            std::begin(input), std::end(input), std::back_inserter(output),
            [] (const Sample &sample) -> Sample
            {
                return { sample.time, sample.edge };
            });

        // Extend to maxtime using the last samples edge.
        //extend(output, maxtime);
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
                && it != std::begin(input))) // ignore the first sample representing the initial assumed low state
        {
            Sample outRising = { inSample.time + std::chrono::nanoseconds(io.delay), Edge::Rising };
            Sample outFalling = { outRising.time + std::chrono::nanoseconds(io.width), Edge::Falling };

            output.emplace_back(outRising);
            output.emplace_back(outFalling);

            holdoffUntil = std::max(outFalling.time, inSample.time + std::chrono::nanoseconds(io.holdoff));

            if (outFalling.time >= maxtime)
                break;
        }

        if (inSample.edge == Edge::Unknown)
            break;

    }
}

// Full LUT simulation with strobe input
#if 1
void simulate_lut(
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

        simulate_gg(lut.strobeGG,
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

    //for (auto &output: actualOutputs)
    //    output->push_back({ 0ns, Edge::Falling });

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
#endif

void simulate_lut2(
    const LUT &lut,
    const std::array<const Trace *, LUT::InputBits> &inputs,
    std::array<Trace *, LUT::OutputBits> &outputs,
    const SampleTime &maxtime)
{
    auto find_earliest_sample_time = [] (const auto &traces)
    {
        auto result = SampleTime::max();

        for (const auto &trace: traces)
        {
            if (!trace->empty())
                if (trace->front().time < result)
                    result = trace->front().time;
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

    auto t0 = find_earliest_sample_time(inputs);
    auto s0 = state_at(t0, inputs);
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

static const SampleTime GateGeneratorDelay = 8ns;
static const SampleTime LevelOutputDelay = 8ns;
static const SampleTime IRQInputDelay = 24ns;
static const SampleTime NIMInputDelay = 28ns;

void apply_delay(Trace &trace, SampleTime delay)
{
    std::for_each(
        std::begin(trace), std::end(trace),
        [delay] (Sample &sample) {
            sample.time += delay;
        });
}

void simulate(Sim &sim, const SampleTime &maxtime)
{
#if 1
    auto tStart = QTime::currentTime();

    clear_simulated_traces(sim);

    // Create missing traces to avoid having to check repeatedly in the rest of
    // the sim code.
    if (sim.sampledTraces.size() < DSOExpectedSampledTraces)
        sim.sampledTraces.resize(DSOExpectedSampledTraces);

    // L0 NIMs
    for (const auto &kv: sim.trigIO.l0.ioNIM | indexed(0))
    {
        const SampleTime Delay = NIMInputDelay + GateGeneratorDelay + LevelOutputDelay;

        UnitAddress ua{0, static_cast<unsigned>(Level0::NIM_IO_Offset + kv.index())};

        auto inputTrace = lookup_input_trace(sim, ua);
        auto outputTrace = lookup_output_trace(sim, ua);
        assert(inputTrace);
        assert(outputTrace);

        auto &io = kv.value();

        // The NIM is configured as an output which means the GG has already
        // been applied to the sampled trace. Copy and apply the delay.
        if (io.direction == IO::Direction::out)
        {
            std::copy(std::begin(*inputTrace), std::end(*inputTrace),
                      std::back_inserter(*outputTrace));
        }
        else
        {
            simulate_gg(kv.value(), *inputTrace, *outputTrace, maxtime);
        }

        apply_delay(*outputTrace, Delay);
    }

    // IRQ Inputs
    for (const auto &kv: sim.trigIO.l0.ioIRQ | indexed(0))
    {
        const SampleTime Delay = IRQInputDelay + GateGeneratorDelay + LevelOutputDelay;

        UnitAddress ua{0, static_cast<unsigned>(Level0::IRQ_Inputs_Offset + kv.index())};

        auto inputTrace = lookup_input_trace(sim, ua);
        auto outputTrace = lookup_output_trace(sim, ua);
        assert(inputTrace);
        assert(outputTrace);

        simulate_gg(kv.value(), *inputTrace, *outputTrace, maxtime);

        apply_delay(*outputTrace, Delay);
    }

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

        simulate_lut(lut, inputs, outputs, maxtime);
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
            outputs[i] = lookup_output_trace(sim, { 2, lutIndex, i });
            assert(outputs[i]);
        }

        simulate_lut(lut, inputs, outputs, maxtime);
    }

#if 0
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
        simulate_gg(io, *inputTrace,
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
        simulate_gg(io, *inputTrace,
                 sim.l3_traces[kv.index() + Level3::ECL_Unit_Offset],
                 maxtime);
    }
#endif

    auto tEnd = QTime::currentTime();
    auto dt = tStart.msecsTo(tEnd);
    qDebug() << "simulated up to" << maxtime.count() << "ns";
    qDebug() << "simulate() took" << dt << "ms";

#else
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
        // irq input traces directly follow the NIM traces
        const auto &trace = sim.sampledTraces[kv.index() + NIM_IO_Count];
        simulate(io, trace, sim.l0_traces[kv.index() + Level0::IRQ_Inputs_Offset], maxtime);
    }

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
#endif
}

Trace *lookup_trace(Sim &sim, const PinAddress &pa)
{
    if (pa.pos == PinPosition::Output)
        return lookup_output_trace(sim, pa.unit);

    // Trace for an input pin is wanted.
    assert(pa.pos == PinPosition::Input);

    // Sampled traces (level0 inputs)
    if (pa.unit[0] == 0)
    {
        int idx = get_trace_index(pa);

        if (0 <= idx && static_cast<unsigned>(idx) < sim.sampledTraces.size())
            return &sim.sampledTraces[idx];
    }
    else if (pa.unit[0] == 1)
    {
        auto con =  Level1::StaticConnections[pa.unit[1]][pa.unit[2]];
        return lookup_output_trace(sim, con.address);
    }
    else if (pa.unit[0] == 2)
    {
        auto con = Level2::StaticConnections[pa.unit[1]][pa.unit[2]];

        if (!con.isDynamic)
            return lookup_output_trace(sim, con.address);

        auto srcAddr = get_connection_unit_address(sim.trigIO, pa.unit);
        return lookup_output_trace(sim, srcAddr);
    }
    else if (pa.unit[0] == 3)
    {
        auto srcAddr = get_connection_unit_address(sim.trigIO, pa.unit);
        return lookup_output_trace(sim, srcAddr);
    }

    return nullptr;
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
