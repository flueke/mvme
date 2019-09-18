#ifndef __MVME_MVLC_TRIGGER_IO_H__
#define __MVME_MVLC_TRIGGER_IO_H__

#include <array>
#include <bitset>
#include <stdexcept>
#include <vector>

#include "typedefs.h"

namespace mesytec
{
namespace mvlc
{
namespace trigger_io
{

static const size_t TimerCount = 4;
static const size_t NIM_IO_Count = 14;
static const size_t ECL_OUT_Count = 3;

struct Timer
{
    enum class Range { ns, us, ms, s };
    Range range;
    u16 delay_ns;
    u16 period;
};

struct IO
{
    enum class Direction { in, out };
    u16 delay;
    u16 width;
    u16 holdoff;
    bool invert;
    Direction direction;
    bool activate;
};

struct StackBusy
{
    u8 stackIndex;
};

using LUT_OutputMap = std::bitset<64>;

// 6 input bits, 4 output bits, 3 of which are used.
// => 2^6 * 4 bits = 64 * 4 bits = 256 bits needed.
// 4  4-bit nibbles are stored in a single 16 bit word in the RAM.
// => 16 rows of memory needed to store all 64 nibbles.
using LUT_RAM = std::array<u16, 16>;

struct LUT
{
    static const int InputBits = 6;
    static const int OutputBits = 3;
    static const size_t InputCombinations = 1u << InputBits;

    // TODO: remove ram from here. Build conversion functions from outputMappings to ram and vice versa
    LUT_RAM ram;

    std::array<LUT_OutputMap, OutputBits> outputMappings;

    // Bit mask determining which outputs are to be strobed.
    std::bitset<OutputBits> strobedOutputs;

    // Strobe gate generator settings
    IO strobeGG;
};

struct StackStart
{
    bool activate;
    u8 stackIndex;
    //bool storeStrobes; // not implemented / not used
    // Note: the immediate flag is used to directly exec the stack which can be
    // useful for testing. The value is not currently stored in here.
    //bool immediate;
};

struct MasterTrigger
{
    bool activate;
    // Note: the immediate flag is used to directly exec the stack which can be
    // useful for testing. The value is not currently stored in here.
    //bool immediate;
};

struct Counter
{
    bool activate;
};

struct IRQ_Unit
{
    // zero-based IRQ index (0 == IRQ1, 6 == IRQ7)
    u8 irqIndex;
};

struct Level0
{
    static const int OutputCount = 33;
    static const int NIM_IO_Offset = 16;
    static const size_t ECL_Unit_Offset = 30;

    static const int IRQ_UnitCount = 2;
    static const int IRQ_UnitOffset = 4;

    static const int SoftTriggerCount = 2;
    static const int SoftTriggerOffset = 6;

    static const int SlaveTriggerCount = 4;
    static const int SlaveTriggerOffset = 8;

    static const int StackBusyCount = 2;
    static const int StackBusyOffset = 12;

    static const size_t UtilityUnitCount = 14;

    std::array<Timer, TimerCount> timers;   // 0..3
    std::array<IRQ_Unit, IRQ_UnitCount> irqUnits; // 4, 5     are irq units
                                            // 6, 7     are software triggers
    std::array<IO, SlaveTriggerCount> slaveTriggers;        // 8..11
    std::array<StackBusy, StackBusyCount> stackBusy;     // 12, 13
                                            // 14, 15 unused
    std::array<IO, NIM_IO_Count> ioNIM;     // 16..29
    // FIXME: not really needed here. l3 only
    //std::array<IO, ECL_OUT_Count> ioECL;    // 30..32
};

struct Level1
{
    static const size_t LUTCount = 5;
    std::array<LUT, LUTCount> luts;
};

struct Level2
{
    static const size_t LUTCount = 2;
    std::array<LUT, LUTCount> luts;
};

struct Level3
{
    static const size_t StackStartCount = 4;
    static const size_t MasterTriggerCount = 4;
    static const size_t CountersCount = 4;
    static const size_t UtilityUnitCount = StackStartCount + MasterTriggerCount + CountersCount;
    static const size_t UnitCount = 33;
    static const size_t NIM_IO_Unit_Offset = 16;
    static const size_t ECL_Unit_Offset = 30;

    std::array<StackStart, StackStartCount> stackStart = {};
    std::array<MasterTrigger, MasterTriggerCount> masterTrigger = {};
    std::array<Counter, CountersCount> counters = {};
    std::array<IO, NIM_IO_Count> ioNIM = {};
    std::array<IO, ECL_OUT_Count> ioECL = {};
};

struct TriggerIO
{
    Level0 l0;
    Level1 l1;
    Level2 l2;
    Level3 l3;
};

// 6 bit address -> 4 output bits, 3 of which are used
inline u8 lookup(const LUT_RAM &lut, u8 address)
{
    if (address > 63)
        throw std::out_of_range("LUT address > 2^6");

    u8 cell = address >> 2;
    u8 nibble =  address & 0b11;
    u8 shift = nibble * 4;

    return (lut[cell] >> shift) & 0xf;
}

// 6 bit address <- 4 output bits, 3 of which are used
inline void set(LUT_RAM &lut, u8 address, u8 value)
{
    if (address > 63)
        throw std::out_of_range("LUT address > 2^6 - 1");

    if (value > 8)
        throw std::out_of_range("LUT value > 2^3  - 1");

    u8 cell = address >> 2;
    u8 nibble =  address & 0b11;
    u8 shift = nibble * 4;

    lut[cell] |= (value & 0xf) << shift;
}

} // end namespace trigger_io
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_H__ */
