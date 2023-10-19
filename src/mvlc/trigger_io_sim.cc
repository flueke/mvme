#include "mvlc/trigger_io_sim.h"

#include <boost/range/adaptor/indexed.hpp>
#include <QDebug>
#include <QTime>
#include <type_traits>

#include "qt_util.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using boost::adaptors::indexed;


namespace
{
static const SampleTime GateGeneratorDelay = 8ns;
static const SampleTime LevelOutputDelay = 8ns;
static const SampleTime NimIoDelay = 28ns;
static const SampleTime EclOutDelay = 28ns;
static const SampleTime IRQInputDelay = 24ns;
static const SampleTime LutInternalDelay = 8ns;
static const SampleTime LutStrobeInputDelay = 8ns;
static const SampleTime LutPostStrobeOutputDelay = 8ns;

void apply_delay(Trace &trace, SampleTime delay)
{
    std::for_each(
        std::begin(trace), std::end(trace),
        [delay] (Sample &sample) {
            if (sample.time != 0ns)
                sample.time += delay;
        });
}


// Get the earliest sample time in the input trace that is later than
// the given t0.
// Returns SampleTime::min() if no matching sample is found.
SampleTime find_sample_time_after(SampleTime t0, const Trace *tracePtr)
{
    assert(tracePtr);

    const auto &trace = *tracePtr;

    auto it = std::find_if(
        std::begin(trace), std::end(trace),
        [t0] (const Sample &sample)
        {
            return sample.time > t0;
        });

    if (it != std::end(trace))
        return it->time;

    return SampleTime::min();
}

// Get the earliest sample time in the input traces that is later than
// the given t0.
// Returns SampleTime::min() if no matching sample is found.
// C must be a container containing raw pointers to Trace.
template<typename C, typename std::enable_if_t<!std::is_pointer<C>{}, bool> = true>
SampleTime find_sample_time_after(SampleTime t0, const C &traces)
{
    auto result = SampleTime::min();

    for (const auto &tracePtr: traces)
    {
        if (!tracePtr) continue;

        auto tTrace = find_sample_time_after(t0, tracePtr);

        if (result == SampleTime::min() ||
            (tTrace != SampleTime::min() && tTrace < result))
        {
            result = tTrace;
        }
    }

    return result;
}

// Reverse search in the trace for time <= t. Return the edge of that sample or
// Edge::Unknown if no matching sample is found.
Edge edge_at(SampleTime t, const Trace &trace)
{
    auto it = std::find_if(
        std::rbegin(trace), std::rend(trace),
        [t] (const Sample &sample)
        {
            return sample.time <= t;
        });

    return it == std::rend(trace) ? Edge::Unknown : it->edge;
}

inline Edge edge_at(SampleTime t, const Trace *trace)
{
    assert(trace);
    return edge_at(t, *trace);
}

u32 calculate_lut_input_combination(
    SampleTime t,
    const LutInputTraces &inputs,
    const std::bitset<LUT::InputBits> &usedInputs)
{
    u32 inputCombination = 0u;

    for (size_t inIdx = 0; inIdx < inputs.size(); ++inIdx)
    {
        if (usedInputs.test(inIdx))
        {
            auto edge = edge_at(t, inputs[inIdx]);

            if (edge == Edge::Unknown)
            {
                inputCombination = LUT::InvalidInputCombination;
                break; // no need to check the other input traces
            }

            inputCombination |= static_cast<unsigned>(edge) << inIdx;
        }
    }

    assert(inputCombination <= LUT::InputCombinations);

    return inputCombination;
};

}

void simulate_single_lut_output(
    const LUT::Bitmap &mapping,
    const LutInputTraces &inputs,
    Trace *outputTrace,
    const Trace *strobeTrace,      // set to nullptr if the output being simulated is not strobed
    const SampleTime &maxtime)
{
    assert(outputTrace);

    // Determine which inputs actually have an effect on the output.
    auto usedInputs = minimize(mapping);

    if (usedInputs.none()) // No input has an effect on the output => static output value.
    {
        Edge staticOutputValue = mapping.test(0) ? Edge::Rising : Edge::Falling;

        // Not strobed or static output value set to low => static output from 0 to max time.
        if (!strobeTrace || staticOutputValue == Edge::Falling)
        {
            outputTrace->push_back({0ns, staticOutputValue});
            outputTrace->push_back({maxtime, staticOutputValue});
        }
        // FIXME: does this really handle all cases? test strobe + Rising|Falling combinations.
        else // strobed and output set to 1 (Rising)
        {
            // In this case the output is equal to the simulated strobe trace
            // delayed by the internal LUT delay
            std::copy(std::begin(*strobeTrace), std::end(*strobeTrace),
                      std::back_inserter(*outputTrace));
            apply_delay(*outputTrace, LutInternalDelay);
        }
    }
    #if 0
    else if (!strobeTrace) // simple case first: no strobe => output immediately follows input
    {
        assert(usedInputs.any());
        SampleTime t0(0);
        u32 inputCombination = calculate_lut_input_combination(t0, inputs, usedInputs);
        auto outEdge = (mapping.test(inputCombination) ? Edge::Rising : Edge::Falling);
        outputTrace->push_back({ t0 , outEdge });

        while (true)
        {
            t0 = find_sample_time_after(t0, inputs);

            if (t0 == SampleTime::min())
                return;

            inputCombination = calculate_lut_input_combination(t0, inputs, usedInputs);
            outEdge = Edge::Unknown;

            if (inputCombination < LUT::InputCombinations)
                outEdge = (mapping.test(inputCombination) ? Edge::Rising : Edge::Falling);

            outputTrace->push_back({ t0 + LutInternalDelay, outEdge });
        }
    }
    #elif 1 // old code
    else // At least one input affects the output -> simulate.
    {
        assert(usedInputs.any());

        SampleTime t0(0);
        u32 inputCombination = calculate_lut_input_combination(t0, inputs, usedInputs);
        auto outEdge = Edge::Unknown;
        if (inputCombination < LUT::InputCombinations)
            outEdge = (mapping.test(inputCombination) ? Edge::Rising : Edge::Falling);
        outputTrace->push_back({ t0 , outEdge });

        while (true)
        {
            if (strobeTrace)
                t0 = find_sample_time_after(t0, strobeTrace);
            else
                t0 = find_sample_time_after(t0, inputs);

            if (t0 == SampleTime::min())
                return;

            inputCombination = calculate_lut_input_combination(t0, inputs, usedInputs);

            if (!strobeTrace || edge_at(t0 + LutInternalDelay, strobeTrace) == Edge::Rising)
            {
                outEdge = Edge::Unknown;

                if (inputCombination < LUT::InputCombinations)
                    outEdge = (mapping.test(inputCombination) ? Edge::Rising : Edge::Falling);

                if (outputTrace->back().edge != outEdge)
                    outputTrace->push_back({ t0 + LutInternalDelay, outEdge });

                if (strobeTrace)
                {
                    Sample s { t0 + LutInternalDelay + SampleTime(LUT::StrobeGGDefaultWidth), invert(outEdge) };
                    outputTrace->push_back(s);
                }
            }

        }
    }
    #endif
}


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
    }
    else
    {
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
                Sample outRising = { inSample.time + SampleTime(io.delay), Edge::Rising };
                Sample outFalling = { outRising.time + SampleTime(io.width), Edge::Falling };

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

    // Apply the internal GG delay to the output trace
    apply_delay(output, GateGeneratorDelay); // FIXME 230504: this should probably only happen in the io.width==0 case!
}

void simulate_lut(
    const LUT &lut,
    const LutInputTraces &inputs,
    LutOutputTraces outputs,
    const Trace *strobeTrace,       // set to nullptr if the LUT doesn't use a strobe
    const SampleTime &maxtime)
{
    assert((lut.strobedOutputs.any() && strobeTrace)
           || lut.strobedOutputs.none());

    for (auto outIdx=0; outIdx<LUT::OutputBits; ++outIdx)
    {
        bool isStrobed = lut.strobedOutputs.test(outIdx);

        simulate_single_lut_output(
            lut.lutContents[outIdx],
            inputs,
            outputs[outIdx],
            isStrobed ? strobeTrace : nullptr,
            maxtime);
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
    //auto tStart = QTime::currentTime();

    clear_simulated_traces(sim);

    // Create missing traces to avoid having to check repeatedly in the rest of
    // the sim code.
    if (sim.sampledTraces.size() < DSOExpectedSampledTraces)
        sim.sampledTraces.resize(DSOExpectedSampledTraces);

    // L0 utilities: No simulation for now, just copy from the sampled traces
    // to the L0 utility output traces
    for (unsigned utilIndex=0; utilIndex<Level0::UtilityUnitCount; ++utilIndex)
    {
        UnitAddress ua{0, utilIndex};

        auto inputTrace = lookup_input_trace(sim, ua);
        auto outputTrace = lookup_output_trace(sim, ua);
        assert(inputTrace && outputTrace && (inputTrace != outputTrace));

        std::copy(std::begin(*inputTrace), std::end(*inputTrace),
                  std::back_inserter(*outputTrace));

        assert(inputTrace && outputTrace && (inputTrace != outputTrace));
    }

    // L0 NIMs
    for (const auto &kv: sim.trigIO.l0.ioNIM | indexed(0))
    {
        UnitAddress ua{0, static_cast<unsigned>(Level0::NIM_IO_Offset + kv.index())};

        auto inputTrace = lookup_input_trace(sim, ua);
        auto outputTrace = lookup_output_trace(sim, ua);
        assert(inputTrace && outputTrace && (inputTrace != outputTrace));

        auto &io = kv.value();

        if (io.direction == IO::Direction::in)
        {
            // For input NIMs simulate the gate generator
            simulate_gg(kv.value(), *inputTrace, *outputTrace, maxtime);
        }
        else
        {
            // For NIMs configured as outputs the GG has already been applied
            // to the sampled values so the trace from the DSO can just be
            // copied.
            std::copy(std::begin(*inputTrace), std::end(*inputTrace),
                      std::back_inserter(*outputTrace));
        }

        const SampleTime Delay = NimIoDelay + LevelOutputDelay;
        apply_delay(*outputTrace, Delay);
    }

    // L0 IRQ Inputs
    for (const auto &kv: sim.trigIO.l0.ioIRQ | indexed(0))
    {
        UnitAddress ua{0, static_cast<unsigned>(Level0::IRQ_Inputs_Offset + kv.index())};

        auto inputTrace = lookup_input_trace(sim, ua);
        auto outputTrace = lookup_output_trace(sim, ua);
        assert(inputTrace && outputTrace && (inputTrace != outputTrace));

        simulate_gg(kv.value(), *inputTrace, *outputTrace, maxtime);

        const SampleTime Delay = IRQInputDelay + LevelOutputDelay;
        apply_delay(*outputTrace, Delay);
    }

    // L1 LUTs

    auto simulate_l1_lut = [&] (unsigned lutIndex, SampleTime additionalDelay = {})
    {
        LutInputTraces inputs;

        for (unsigned i=0; i<inputs.size(); i++)
            inputs[i] = lookup_input_trace(sim, { 1, lutIndex, i });

        LutOutputTraces outputs;

        for (unsigned i=0; i<outputs.size(); i++)
            outputs[i] = lookup_output_trace(sim, { 1, lutIndex, i });

        const auto &lut = sim.trigIO.l1.luts[lutIndex];
        simulate_lut(lut, inputs, outputs, maxtime);

        if (additionalDelay != SampleTime::zero())
        {
            for (auto trace: outputs)
                apply_delay(*trace, additionalDelay);
        }
    };

    // L1 hierarchy: first LUTs 0, 1, 2, 5 then 3, 4, 6
    for (unsigned lutIndex: { 0, 1, 2, 5 })
        simulate_l1_lut(lutIndex);

    for (unsigned lutIndex: { 3, 4, 6 })
        simulate_l1_lut(lutIndex, LevelOutputDelay);


    // L2 LUTs with strobes and dynamic connections
    for (const auto &kvLUT: sim.trigIO.l2.luts | indexed(0))
    {
        const unsigned lutIndex = kvLUT.index();
        const auto &lut = kvLUT.value();

        LutInputTraces inputs;

        for (unsigned i=0; i<inputs.size(); i++)
        {
            inputs[i] = lookup_input_trace(sim, { 2, lutIndex, i });
            assert(inputs[i]);
        }

        auto strobeInput = lookup_input_trace(sim, { 2, lutIndex, LUT::StrobeGGInput });
        assert(strobeInput);

        LutOutputTraces outputs;

        for (unsigned i=0; i<outputs.size(); i++)
        {
            outputs[i] = lookup_output_trace(sim, { 2, lutIndex, i });
            assert(outputs[i]);
        }

        auto *strobeOutput = lookup_output_trace(sim, { 2, lutIndex, Sim::StrobeGGOutputTraceIndex });
        assert(strobeOutput);

        // Simulate the strobe
        Trace strobeInputDelayed = *strobeInput;
        apply_delay(strobeInputDelayed, LutStrobeInputDelay);
        simulate_gg(lut.strobeGG, strobeInputDelayed, *strobeOutput, maxtime);

        // Simulate the LUT passing in the simulated strobe trace
        simulate_lut(lut, inputs, outputs, strobeOutput, maxtime);

        for (auto trace: outputs)
            apply_delay(*trace, LutPostStrobeOutputDelay + LevelOutputDelay);
    }

    // L3 NIMs
    for (const auto &kv: sim.trigIO.l3.ioNIM | indexed(0))
    {
        UnitAddress ua{3, static_cast<unsigned>(Level3::NIM_IO_Unit_Offset + kv.index())};

        auto inputTrace = lookup_input_trace(sim, ua);
        auto outputTrace = lookup_output_trace(sim, ua);
        assert(inputTrace && outputTrace && (inputTrace != outputTrace));

        const auto &io = kv.value();

        if (io.direction == IO::Direction::out)
        {
            simulate_gg(kv.value(), *inputTrace, *outputTrace, maxtime);
            apply_delay(*outputTrace, NimIoDelay);
        }
    }

    // L3 ECLs
    for (const auto &kv: sim.trigIO.l3.ioECL | indexed(0))
    {
        UnitAddress ua{3, static_cast<unsigned>(Level3::ECL_Unit_Offset + kv.index())};

        auto inputTrace = lookup_input_trace(sim, ua);
        auto outputTrace = lookup_output_trace(sim, ua);
        assert(inputTrace && outputTrace && (inputTrace != outputTrace));

        simulate_gg(kv.value(), *inputTrace, *outputTrace, maxtime);
        apply_delay(*outputTrace, EclOutDelay);
    }

    //auto tEnd = QTime::currentTime();
    //auto dt = tStart.msecsTo(tEnd);
    //qDebug() << "simulated up to" << maxtime.count() << "ns";
    //qDebug() << "simulate() took" << dt << "ms";
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
        if (!con.isDynamic)
            return lookup_output_trace(sim, con.address);
        // Dynamic input (L1.LUT2)
        auto srcAddr = get_connection_unit_address(sim.trigIO, pa.unit);
        return lookup_output_trace(sim, srcAddr);
    }
    else if (pa.unit[0] == 2)
    {
        assert(pa.unit[1] < Level2::StaticConnections.size());
        assert(pa.unit[2] <= LUT::StrobeGGInput);

        if (pa.unit[2] != LUT::StrobeGGInput)
        {
            auto con = Level2::StaticConnections[pa.unit[1]][pa.unit[2]];

            if (!con.isDynamic)
                return lookup_output_trace(sim, con.address);
        }

        // Dynamic input (including StrobeGGInput)
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
