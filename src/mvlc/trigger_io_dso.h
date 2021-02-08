#ifndef __MVME_MVLC_TRIGGER_IO_DSO_H__
#define __MVME_MVLC_TRIGGER_IO_DSO_H__

#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "libmvme_export.h"
#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_dso
{

using namespace trigger_io;

// Support for the digital storage oscilloscope (DSO) built into the MVLC
// trigger_io module.
//
// The DSO is at level0, unit 48. The following needs to be done to read out DSO data:
// - set pre and post trigger times
// - set the NIM and IRQ trigger masks
// - start block reads from 0xffff0000, store the data somewhere
// - write readout_reset (0x6034) after each block read
//
// While the DSO is active no other communication may take place as that would
// mix DSO sample data with command responses. This is enforced in
// acquire_dso_sample() by locking the command mutex while the DSO is
// activated. Also the stack error poller built into the MVLC object is
// suspended during the time the DSO is active.
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
    Rising = 1
};

struct LIBMVME_EXPORT DSOSetup
{
    u16 preTriggerTime = 0u;
    u16 postTriggerTime = 0u;
    std::bitset<NIM_IO_Count> nimTriggers;
    std::bitset<Level0::IRQ_Inputs_Count> irqTriggers;
};

static const size_t CombinedTriggerCount = NIM_IO_Count + Level0::IRQ_Inputs_Count;

LIBMVME_EXPORT std::bitset<CombinedTriggerCount>
combined_triggers(const DSOSetup &setup);

namespace data_format
{
    static const u32 MatchBitsMask = 0b00;
    static const u32 MatchBitsShift = 30;
    static const u32 AddressMask = 0b11111;
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
using Trace = std::vector<Sample>;
// Collection of traces representing e.g. a snapshot acquired from the DSO.
using Snapshot = std::vector<Trace>;

// Starts, reads and stops the DSO. Puts the first valid sample into the dest
// buffer, other samples are discarded.
// This function internally suspends the MVLCs stack error poller and locks the
// command pipe. This way no other communication can take place while the DSO
// is active.
// If no valid sample is received within the timeout std::errc::timed_out is
// returned. The timeout is needed as the DSO may never receive a trigger.
LIBMVME_EXPORT std::error_code
acquire_dso_sample(
    mvlc::MVLC mvlc, DSOSetup setup,
    std::vector<u32> &dest,
    std::atomic<bool> &cancel,
    const std::chrono::milliseconds &timeout = std::chrono::milliseconds(3000));

// Fill a snapshot from a DSO buffer obtained via acquire_dso_sample().
LIBMVME_EXPORT Snapshot
fill_snapshot_from_dso_buffer(const std::vector<u32> &buffer);

LIBMVME_EXPORT void
pre_process_dso_snapshot(
    Snapshot &snapshot,
    const DSOSetup &dsoSetup,
    SampleTime extendToTime = SampleTime::min());

s32 calculate_jitter_value(const Snapshot &snapshot, const DSOSetup &dsoSetup);

inline Edge invert(const Edge &e)
{
    return (e == Edge::Falling ? Edge::Rising : Edge::Falling);
}

inline const char *to_string(const Edge &e)
{
    switch (e)
    {
        case Edge::Falling: return "falling";
        case Edge::Rising: return "rising";
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

} // end namespace trigger_io_dso
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_H__ */
