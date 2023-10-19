#ifndef __MVME_MVLC_TRIGGER_IO_SIM_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_H__

#include <chrono>
#include <functional>
#include <iterator>
#include <unordered_map>
#include "libmvme_export.h"
#include "mvlc/mvlc_trigger_io.h"
#include "mvlc/trigger_io_dso.h"
#include "mvlc/trigger_io_sim_pinaddress.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using namespace std::chrono_literals;

// Extend the timeline to toTime using the last input samples edge.
// Does nothing if input is empty or the last sample time in input is >= toTime.
#if 0
inline void extend(Trace &input, const SampleTime &toTime)
{
    if (!input.empty() && input.back().time < toTime)
        input.push_back({ toTime, input.back().edge });
}
#endif

// Simulate a gate generator.
LIBMVME_EXPORT void simulate_gg(
    const IO &io,
    const Trace &input,
    Trace &output,
    const SampleTime &maxtime);

LIBMVME_EXPORT void simulate(
    const Timer &timer,
    Trace &output,
    const SampleTime &maxtime);

using LutInputTraces = std::array<const Trace *, LUT::InputBits>;
using LutOutputTraces = std::array<Trace *, LUT::OutputBits>;

// Full LUT simulation with strobe trace
LIBMVME_EXPORT void simulate_lut(
    const LUT &lut,
    const LutInputTraces &inputs,
    LutOutputTraces outputs,
    const Trace *strobeTrace,
    const SampleTime &maxtime);

// LUT simulation without the strobe
inline void simulate_lut(
    const LUT &lut,
    const LutInputTraces &inputs,
    LutOutputTraces outputs,
    const SampleTime &maxtime)
{
    simulate_lut(lut, inputs, outputs, nullptr, maxtime);
}

void simulate_single_lut_output(
    const LUT::Bitmap &mapping,
    const LutInputTraces &inputs,
    Trace *outputTrace,
    const Trace *strobeTrace,      // set to nullptr if the output being simulated is not strobed
    const SampleTime &maxtime);

struct LIBMVME_EXPORT Sim
{
    // +1 for the strobe output trace
    using LutOutputAndStrobeTraces = std::array<Trace, LUT::OutputBits+1>;
    static const size_t StrobeGGOutputTraceIndex = LUT::OutputBits;

    Sim()
    {
        sampledTraces.resize(DSOExpectedSampledTraces);
    }

    Sim(const Sim &) = default;
    Sim &operator=(const Sim &) = default;

    Sim( Sim &&) = default;
    Sim &operator=(Sim &&) = default;

    // The trigger io setup
    TriggerIO trigIO;

    // 0-13 are the NIMs, 14-19 the IRQ inputs.
    Snapshot sampledTraces;

    // True if the trace stored in sampledTraces had the overflow marker set.
    // Must be the same size as the snapshot.
    std::vector<bool> traceOverflows;

    // L0 - simulated 'output' traces of the NIMs, IRQs and simulated utility
    // traces (timers, sysclock)
    std::array<Trace, Level0::OutputCount> l0_traces;

    // L1 - LUT outputs
    std::array<LutOutputAndStrobeTraces, Level1::LUTCount> l1_luts;

    // L2 - LUT outputs
    std::array<LutOutputAndStrobeTraces, Level2::LUTCount> l2_luts;

    // L3 - simulated 'output' traces
    std::array<Trace, Level3::UnitCount> l3_traces;
};

LIBMVME_EXPORT void simulate(Sim &sim, const SampleTime &maxtime);

// Returns the address of an output trace of the system.
LIBMVME_EXPORT Trace *lookup_output_trace(Sim &sim, const UnitAddress &addr);

LIBMVME_EXPORT Trace *lookup_trace(Sim &sim, const PinAddress &pa);

inline const Trace *lookup_input_trace(Sim &sim, const UnitAddress &addr)
{
    return lookup_trace(sim, PinAddress(addr, PinPosition::Input));
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_H__ */
