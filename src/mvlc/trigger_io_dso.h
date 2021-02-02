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
// activated.

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
LIBMVME_EXPORT std::error_code
acquire_dso_sample(
    mvlc::MVLC mvlc, DSOSetup setup,
    std::vector<u32> &dest, std::atomic<bool> &cancel);

// Fill a snapshot from a DSO buffer obtained via acquire_dso_sample().
LIBMVME_EXPORT Snapshot
fill_snapshot_from_mvlc_buffer(const std::vector<u32> &buffer);

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

} // end namespace trigger_io_dso
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_H__ */
