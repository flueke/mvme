#ifndef __MVME_MVLC_TRIGGER_IO_SIM_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_H__

#include <chrono>
#include <functional>
#include <iterator>
#include <unordered_map>
#include "mvlc/mvlc_trigger_io.h"
#include "mvlc/trigger_io_scope.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using namespace trigger_io_scope;
using namespace std::chrono_literals;

// Extend the timeline to toTime using the last input samples edge.
// Does nothing if input is empty or the last sample time in input is >= toTime.
inline void extend(Timeline &input, const SampleTime &toTime)
{
    if (!input.empty() && input.back().time < toTime)
        input.push_back({ toTime, input.back().edge });
}

void simulate(
    const IO &io,
    const Timeline &input,
    Timeline &output,
    const SampleTime &maxtime);

void simulate(
    const Timer &timer,
    Timeline &output,
    const SampleTime &maxtime);

static const auto SysClockPeriod = 62.5ns;
static const auto SysClockHalfPeriod = SysClockPeriod * 0.5;

void simulate_sysclock(
    Timeline &output,
    const SampleTime &maxtime);

// +1 for the strobe in and out traces. These entries may be nullptr if the
// strobe is not used. Otherwise both must be valid.
using LUT_Input_Timelines = std::array<const Timeline *, LUT::InputBits+1>;
using LUT_Output_Timelines = std::array<Timeline *, LUT::OutputBits+1>;

// Full LUT simulation with strobe input
void simulate(
    const LUT &lut,
    const LUT_Input_Timelines &inputs,
    LUT_Output_Timelines &outputs,
    const SampleTime &maxtime);

#if 0
// LUT simulation without passing the strobe input
inline void simulate(
    const LUT &lut,
    const LUT_Input_Timelines &inputs,
    LUT_Output_Timelines &outputs,
    const SampleTime &maxtime)
{
    Timeline strobeOutput;
    simulate(lut, inputs, {}, outputs, strobeOutput, maxtime);
}
#endif

// +1 for the strobe output trace
using LUTOutputTraces = std::array<Timeline, LUT::OutputBits+1>;

struct Sim
{
    // The trigger io setup
    TriggerIO trigIO;

    // 0-14 are the NIMs, additional things are going to be added
    Snapshot sampledTraces;

    // L0
    std::array<Timeline, Level0::OutputCount> l0_traces;

    // L1
    std::array<LUTOutputTraces, Level1::LUTCount> l1_luts;

    // L2
    std::array<LUTOutputTraces, Level2::LUTCount> l2_luts;

    // L3
    std::array<Timeline, Level3::UnitCount> l3_traces;
};

void simulate(Sim &sim, const SampleTime &maxtime);

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_H__ */
