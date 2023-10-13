#ifndef __MVME_MVLC_TRIGGER_IO_DSO_H__
#define __MVME_MVLC_TRIGGER_IO_DSO_H__

#include <chrono>
#include <deque>
#include <vector>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "libmvme_export.h"
#include "mvlc/mvlc_trigger_io.h"
#include "mvlc/trigger_io_sim_pinaddress.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

// Support for the digital storage oscilloscope (DSO) built into the MVLC
// trigger_io module.
//
// The DSO is at level0, unit 48. The following needs to be done to read out DSO data:
// - set pre and post trigger times
// - set the NIM, IRQ and level0 util trigger masks
// - start block reads from 0xffff0000, store the data somewhere
//
// While the DSO is active no other communication may take place as that would
// mix DSO sample data with command responses. This is enforced in
// acquire_dso_sample() by locking the command mutex while the DSO is
// active.
//
// As the DSO may never receive a trigger the acquire_dso_sample() function may
// keep trying to read a valid buffer forever. By setting the 'cancel' flag the
// function can be made to return.
// Problem: When running a vme script in mvme the GUI is blocked by the script
// exec progress dialog so the DSO can't be stopped. Also the user will not
// know why everything is stalled.
// Solution: add a timeout parameter to acquire_dso_sample() and return after that time has elapsed.

enum class Edge
{
    Falling = 0,
    Rising = 1,
    Unknown = 2,
};

// Max measurement duration of the DSO in ns. Going over this value leads to
// overflows in the MVLC and in turn to garbage data.
static const unsigned DsoMaxMeasureTime = 65500;

struct LIBMVME_EXPORT DSOSetup
{
    u16 preTriggerTime = 0u;
    u16 postTriggerTime = 0u;
    std::bitset<NIM_IO_Count> nimTriggers;
    std::bitset<Level0::IRQ_Inputs_Count> irqTriggers;
    std::bitset<Level0::UtilityUnitCount> utilTriggers;
};

static const size_t CombinedTriggerCount = NIM_IO_Count + Level0::IRQ_Inputs_Count + Level0::UtilityUnitCount;

using CombinedTriggers = std::bitset<CombinedTriggerCount>;

// Returns a flat bitset containing the nim, irq and util trigger bits.
LIBMVME_EXPORT CombinedTriggers
get_combined_triggers(const DSOSetup &setup);

// Sets the trigger bits from the combinedTriggers set on the DSOSetup structure.
LIBMVME_EXPORT void
set_combined_triggers(DSOSetup &setup, const CombinedTriggers &combinedTriggers);

namespace data_format
{
    // Valid DSO data words have the following structure:
    // 00 | unused | addr | edge | time |
    //  2 |    7   |   6  |  1   |  16  |

    static const u32 MatchBitsMask = 0b00;
    static const u32 MatchBitsShift = 30;
    static const u32 AddressMask = 0b111111;
    static const u32 AddressShift = 17;
    static const u32 EdgeMask = 0b1;
    static const u32 EdgeShift = 16;
    static const u32 TimeMask = 0xffff;
    static const u32 TimeShift = 0;

    static const u32 Header = 0x40000000u;
    static const u32 EoE    = 0xC0000000u;
};

// Note: the MVLC transmits unsigned 16 bit time values. Floating point values
// are used for the simulation code. This could be changed to signed integers
// (signed to make time based calculations behave properly) if needed.
using SampleTime = std::chrono::duration<float, std::chrono::nanoseconds::period>;

struct LIBMVME_EXPORT Sample
{
    SampleTime time;
    Edge edge;
};

// Samples over time for one signal/pin.
using Trace = std::deque<Sample>;
// Collection of traces representing e.g. a snapshot acquired from the DSO.
using Snapshot = std::vector<Trace>;

// Starts, reads and stops the DSO. Puts the first valid sample into the dest
// buffer, other samples are discarded. Returns on fatal error, e.g. on MVLC
// disconnect or when a valid DSO buffer has been received.
// Set cancel=true to force the function to return std::errc::operation_canceled
// if no valid sample has been received yet.
LIBMVME_EXPORT std::error_code
acquire_dso_sample(
    mvlc::MVLC mvlc, DSOSetup setup,
    std::vector<u32> &dest,
    std::atomic<bool> &cancel);

// Fill a snapshot from a DSO buffer obtained via acquire_dso_sample().
LIBMVME_EXPORT Snapshot
fill_snapshot_from_dso_buffer(const std::vector<u32> &buffer);

// Removes overflow markers from the front of each of the given traces. Returns
// an evil vector<bool> where indexes of overflowed traces are set to true.
LIBMVME_EXPORT std::vector<bool>
    remove_trace_overflow_markers(Snapshot &sampledTraces);

inline void pre_extend_trace(Trace &trace)
{
    if (!trace.empty())
    {
        if (auto firstRealSampleTime = trace.front().time;
            firstRealSampleTime != SampleTime(0.0))
        {
            // Note: order is backwards here as we push_front()!
            trace.push_front({firstRealSampleTime, Edge::Unknown});
            trace.push_front({SampleTime(0.0), Edge::Unknown});
        }
    }
}

// Edge::Unknown is prepended to the trace up to the first sample time if
// traceOverflowed is true.
template<typename TraceContainer, typename OverflowsContainer>
void pre_extend_traces(TraceContainer &traces, const OverflowsContainer &overflows)
{
    auto ti = std::begin(traces);
    auto oi = std::begin(overflows);

    for (; ti!=std::end(traces) && oi!=std::end(overflows); ++ti, ++oi)
        if (*oi)
            pre_extend_trace(*ti);
}

template<typename TraceContainer>
void pre_extend_traces(TraceContainer &traces)
{
    for (auto ti = std::begin(traces); ti!=std::end(traces); ++ti)
        pre_extend_trace(*ti);
}

// If the trace is non-empty extend it to the given time using the last known
// edge value in the trace.
inline void post_extend_trace_to(Trace &trace, const SampleTime &extendTo)
{
    if (!trace.empty() && trace.back().time < extendTo)
        trace.push_back({ extendTo, trace.back().edge });
}

template<typename TraceContainer>
void post_extend_traces_to(TraceContainer &traceContainer, const SampleTime &extendTo)
{
    for (auto &trace: traceContainer)
        post_extend_trace_to(trace, extendTo);
}

LIBMVME_EXPORT void
jitter_correct_dso_snapshot(
    Snapshot &snapshot,
    const DSOSetup &dsoSetup);

//s32 calculate_jitter_value(const Snapshot &snapshot, const DSOSetup &dsoSetup);
std::pair<unsigned, bool> calculate_jitter_value(const Snapshot &snapshot, const DSOSetup &dsoSetup);

inline Edge invert(const Edge &e)
{
    if (e == Edge::Falling)
        return Edge::Rising;

    if (e == Edge::Rising)
        return Edge::Falling;

    return e;
}

inline const char *to_string(const Edge &e)
{
    switch (e)
    {
        case Edge::Falling: return "falling";
        case Edge::Rising: return "rising";
        case Edge::Unknown: return "unknown";
    }

    return {};
};

template<typename Out>
Out & print(Out &out, const Trace &trace)
{
    for (const auto &sample: trace)
    {
        out << "(" << sample.time.count()
            << ", " << static_cast<unsigned>(sample.edge)
            << "), ";
    }

    return out;
}

struct DSOBufferEntry
{
    u8 address;
    Edge edge;
    u16 time;
};

inline DSOBufferEntry extract_dso_entry(u32 dataWord)
{
    DSOBufferEntry result;
    result.address = (dataWord >> data_format::AddressShift) & data_format::AddressMask;
    result.edge = Edge((dataWord >> data_format::EdgeShift) & data_format::EdgeMask);
    result.time = (dataWord >> data_format::TimeShift) & data_format::TimeMask;
    return result;
}

Edge edge_at(const Trace &trace, const SampleTime &t);

inline bool has_overflow_marker(const Trace &trace)
{
    if (!trace.empty())
        return trace.front().time == SampleTime(1);
    return false;
}

static const int DSOExpectedSampledTraces = CombinedTriggerCount;


// The list of pin addresses in DSO trace index order.
// The Snapshot created by fill_snapshot_from_dso_buffer() contains traces in
// this order. Also the trigger bits in get_combined_triggers() are sorted this
// way.
const std::vector<PinAddress> &trace_index_to_pin_list();

int get_trace_index(const PinAddress &pa);

LIBMVME_EXPORT QString
get_trigger_default_name(unsigned combinedTriggerIndex);


} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_H__ */
