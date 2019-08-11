#ifndef __MVME_MVLC_TRIGGER_IO_H__
#define __MVME_MVLC_TRIGGER_IO_H__

#include <array>
#include <stdexcept>

#include "typedefs.h"

namespace mvlc
{
namespace trigger_io
{

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

// 6 input bits, 4 output bits, 3 of which are used
// => 2^6 * 4 bits = 64 * 4 bits = 256 bits needed
using LUT_RAM = std::array<u16, 16>;

struct LUT
{
    LUT_RAM ram;
    u8 strobed; // output to be strobed
    IO strobeGG;
};

struct StackStart
{
    bool activate;
    u8 stackIndex;
    bool storeStrobes;
};

struct MasterTrigger
{
    bool activate;
    bool immediate;
};

struct Level0
{
    std::array<Timer, 4> timers;            // 0..3
                                            // 4, 5     are irq units
                                            // 6, 7     are software triggers
    std::array<IO, 4> slaveTriggers;    // 8..11
    std::array<StackBusy, 2> stackBusy;     // 12, 13
                                            // 14, 15 unused
    std::array<IO, 14> outputsNIM;      // 16..29
    std::array<IO, 3> outputsECL;       // 30..32
};

struct Level1
{
    std::array<LUT, 5> luts;
};

struct Level2
{
    std::array<LUT, 2> luts;
};

struct Level3
{
    std::array<StackStart, 4> stackStart;
    std::array<MasterTrigger, 4> masterTrigger;
    std::array<IO, 14> outputsNIM;
    std::array<IO, 3> outputsECL;
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

#endif /* __MVME_MVLC_TRIGGER_IO_H__ */
