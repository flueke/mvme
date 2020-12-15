#ifndef __MVME_MVLC_TRIGGER_IO_SCOPE_H__
#define __MVME_MVLC_TRIGGER_IO_SCOPE_H__

#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mesytec-mvlc/util/threadsafequeue.h"
#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_scope
{

using namespace trigger_io;

// Support for the digital oscilloscope built into the MVLC trigger_io module.
//
// The osci is at level0, unit 48. The following needs to be done to read out osci data:
// - set pre and post trigger times
// - the the trigger channel mask to specify which channels do trigger
// - start block reads from 0xffff0000, store the data somewhere

static const unsigned UnitNumber = 48;

enum class Edge
{
    Falling = 0,
    Rising = 1
};

struct OsciSetup
{
    u16 preTriggerTime = 0u;
    u16 postTriggerTime = 0u;
    std::bitset<NIM_IO_Count> triggerChannels;
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

void reader(
    mvlc::MVLC mvlc,
    const OsciSetup &setup,
    mvlc::ThreadSafeQueue<std::vector<u32>> &buffers,
    std::atomic<bool> &quit,
    std::promise<std::error_code> ec_promise);

inline const char *to_string(const Edge &e)
{
    switch (e)
    {
        case Edge::Falling: return "falling";
        case Edge::Rising: return "rising";
    }

    return {};
};

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SCOPE_H__ */
