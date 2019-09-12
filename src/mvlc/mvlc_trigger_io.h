#ifndef __MVME_MVLC_TRIGGER_IO_H__
#define __MVME_MVLC_TRIGGER_IO_H__

#include <array>
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

    LUT_RAM ram;
    u8 strobeBits; // outputs to be strobed
    IO strobeGG;
};

struct StackStart
{
    bool activate;
    bool immediate;
    u8 stackIndex;
    bool storeStrobes;
};

struct MasterTrigger
{
    bool activate;
    bool immediate;
};

struct Counter
{
    bool activate;
};

struct Level0
{
    static const int OutputCount = 33;
    static const int NIM_IO_Offset = 16;

    std::array<Timer, TimerCount> timers;   // 0..3
                                            // 4, 5     are irq units
                                            // 6, 7     are software triggers
    std::array<IO, 4> slaveTriggers;        // 8..11
    std::array<StackBusy, 2> stackBusy;     // 12, 13
                                            // 14, 15 unused
    std::array<IO, NIM_IO_Count> ioNIM;     // 16..29
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
    // Per (lut, input) dynamic connection values. Only the first 3 inputs of
    // each level 2 LUT are dynamic the other 3 are static.
    std::array<std::array<unsigned, 3>, LUTCount> lutConnections;
    std::array<unsigned, LUTCount> strobeConnections;

    //std::array<unsigned, LUTCount> strobedOutput;
    //std::array<IO, LUTCount> strobeGGs; // TODO: maybe move this into the respective LUT
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

    std::array<unsigned, UnitCount> connections = {};
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
        throw std::out_of_range("LUT address > 2^6");

    if (value > 8)
        throw std::out_of_range("LUT value > 2^3");

    u8 cell = address >> 2;
    u8 nibble =  address & 0b11;
    u8 shift = nibble * 4;

    lut[cell] |= (value & 0xf) << shift;
}

} // end namespace trigger_io
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_H__ */
