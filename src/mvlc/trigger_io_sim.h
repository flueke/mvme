#ifndef __MVME_MVLC_TRIGGER_IO_SIM_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_H__

#include <chrono>
#include <functional>
#include <iterator>
#include <unordered_map>
#include "libmvme_export.h"
#include "mvlc/mvlc_trigger_io.h"
#include "mvlc/trigger_io_dso.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using namespace trigger_io_dso;
using namespace std::chrono_literals;

// Extend the timeline to toTime using the last input samples edge.
// Does nothing if input is empty or the last sample time in input is >= toTime.
inline void extend(Trace &input, const SampleTime &toTime)
{
    if (!input.empty() && input.back().time < toTime)
        input.push_back({ toTime, input.back().edge });
}

LIBMVME_EXPORT void simulate(
    const IO &io,
    const Trace &input,
    Trace &output,
    const SampleTime &maxtime);

LIBMVME_EXPORT void simulate(
    const Timer &timer,
    Trace &output,
    const SampleTime &maxtime);

static const auto SysClockPeriod = 62.5ns;
static const auto SysClockHalfPeriod = SysClockPeriod * 0.5;

LIBMVME_EXPORT void simulate_sysclock(
    Trace &output,
    const SampleTime &maxtime);

// +1 for the strobe in and out traces. These entries may be nullptr if the
// strobe is not used. Otherwise both must be valid.
using LUT_Input_Timelines = std::array<const Trace *, LUT::InputBits+1>;
using LUT_Output_Timelines = std::array<Trace *, LUT::OutputBits+1>;

// Full LUT simulation with strobe input
LIBMVME_EXPORT void simulate(
    const LUT &lut,
    const LUT_Input_Timelines &inputs,
    LUT_Output_Timelines &outputs,
    const SampleTime &maxtime);

// +1 for the strobe output trace
using LUTOutputTraces = std::array<Trace, LUT::OutputBits+1>;

static const int ExpectedSampledTraces = NIM_IO_Count + Level0::IRQ_Inputs_Count;

struct LIBMVME_EXPORT Sim
{
    Sim()
    {
        sampledTraces.resize(ExpectedSampledTraces);
    }

    Sim(const Sim &) = default;
    Sim &operator=(const Sim &) = default;

    Sim( Sim &&) = default;
    Sim &operator=(Sim &&) = default;

    // The trigger io setup
    TriggerIO trigIO;

    // 0-13 are the NIMs, 14-19 the IRQ inputs.
    Snapshot sampledTraces;

    // L0 - simulated 'output' traces of the NIMs, IRQs and simulated utility
    // traces (timers, sysclock)
    std::array<Trace, Level0::OutputCount> l0_traces;

    // L1 - LUT outputs
    std::array<LUTOutputTraces, Level1::LUTCount> l1_luts;

    // L2 - LUT outputs
    std::array<LUTOutputTraces, Level2::LUTCount> l2_luts;

    // L3 - simulated 'output' traces
    std::array<Trace, Level3::UnitCount> l3_traces;
};

LIBMVME_EXPORT void simulate(Sim &sim, const SampleTime &maxtime);

// Returns the address of an output trace of the system.
LIBMVME_EXPORT Trace *lookup_output_trace(Sim &sim, const UnitAddress &addr);

enum class PinPosition
{
    Input,
    Output
};

struct LIBMVME_EXPORT PinAddress
{
    PinAddress() {}

    PinAddress(const UnitAddress &unit_, const PinPosition &pos_)
        : unit(unit_)
        , pos(pos_)
    {}

    PinAddress(const PinAddress &) = default;
    PinAddress &operator=(const PinAddress &) = default;

    inline bool operator==(const PinAddress &o) const
    {
        return unit == o.unit && pos == o.pos;
    }

    inline bool operator!=(const PinAddress &o) const
    {
        return !(*this == o);
    }

    UnitAddress unit = { 0, 0, 0 };
    PinPosition pos = PinPosition::Input;
};

LIBMVME_EXPORT QStringList pin_path_list(const TriggerIO &trigIO, const PinAddress &pa);
LIBMVME_EXPORT QString pin_path(const TriggerIO &trigIO, const PinAddress &pa);
LIBMVME_EXPORT QString pin_name(const TriggerIO &trigIO, const PinAddress &pa);
LIBMVME_EXPORT QString pin_user_name(const TriggerIO &trigIO, const PinAddress &pa);

LIBMVME_EXPORT Trace *lookup_trace(Sim &sim, const PinAddress &pa);

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_H__ */
