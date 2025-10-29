/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __MVME_MVLC_TRIGGER_IO_H__
#define __MVME_MVLC_TRIGGER_IO_H__

#include <array>
#include <bitset>
#include <stdexcept>
#include <vector>

#include <QString>
#include <QStringList>

#include <mesytec-mvlc/mvlc_constants.h>
#include "typedefs.h"
#include "libmvme_export.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

static const size_t TimerCount = 4;
static const size_t NIM_IO_Count = 14;
static const size_t ECL_OUT_Count = 3;

// The minimum value for gate generator signal widths is 8ns.
// If the width is set to 0 the gate generator is disabled and thus delay and
// holdoff do not apply.
static const size_t GateGeneratorMinWidth = 8;

struct LIBMVME_EXPORT Timer
{
    static const u16 MinPeriod = 8;
    static const u16 MaxPeriod = 0xffffu;

    enum class Range: u16
    {
        ns,
        us,
        ms,
        s
    };

    Range range;
    u16 delay_ns;
    u16 period;
    bool softActivate;
};

struct LIBMVME_EXPORT IO
{
    enum class Direction { in, out };
    u16 delay;
    u16 width;
    u16 holdoff;
    bool invert;
    Direction direction;
    bool activate;
};

struct LIBMVME_EXPORT StackBusy
{
    u16 stackIndex;
};

using Names = std::vector<QString>;

struct LIBMVME_EXPORT LUT
{
    static const u16 InputBits = 6;
    static const u16 OutputBits = 3;
    static const u16 InputCombinations = 1u << InputBits;
    // Used to communicate that at least one of the LUT inputs used to form the
    // input combination has an Unknown edge value. In this case the LUT output
    // should also be set to Unknown.
    static const u16 InvalidInputCombination = InputCombinations;
    static const u16 StrobeGGDefaultWidth = 8;

    // This value is used to refer to the LUTs strobe gate generator, e.g
    // UnitAddress {2, 0, StrobeGGInput } for L2.LUT0.StrobeGG
    static const u16 StrobeGGInput = InputBits;
    // Same as above but for the strobe output which is only used in the simulation
    // code.
    static const u16 StrobeGGOutput = OutputBits;

    // Holds the output state for all 64 input combinations. One of the logic LUTs
    // is made up for three of these mappings, one for each output bit.
    using Bitmap = std::bitset<InputCombinations>;
    using Contents = std::array<Bitmap, trigger_io::LUT::OutputBits>;


    LUT();
    LUT(const LUT &) = default;
    LUT &operator=(const LUT &) = default;

    Contents lutContents;

    // Bit mask determining which outputs are to be strobed.
    std::bitset<OutputBits> strobedOutputs;

    // Strobe gate generator settings. Each Level2 LUT has one of these.
    // The strobe GGs width is fixed, delay and holdoff are variable.
    IO strobeGG = { .delay = 0, .width = StrobeGGDefaultWidth, .holdoff = 0,
        .invert = false, .direction = IO::Direction::in, .activate = false };

    std::array<QString, OutputBits> defaultOutputNames;
    std::array<QString, OutputBits> outputNames;
};

// Minimize the given 6 -> 1 bit boolean function. Return a bitset with the
// input bits affecting the output set, the other bits cleared.
LIBMVME_EXPORT std::bitset<LUT::InputBits> minimize(const LUT::Bitmap &mapping);

// Minimize each of the 3 6->1 bit functions the LUT implements. Return a
// bitset with the bits affecting at least one of the outputs set, the other
// bits cleared.
LIBMVME_EXPORT std::bitset<LUT::InputBits> minimize(const LUT &lut);

struct LIBMVME_EXPORT StackStart
{
    bool activate;
    u8 stackIndex;
    u16 delay_ns;
    //bool storeStrobes; // not implemented / not used
    // Note: the immediate flag is used to directly exec the stack which can be
    // useful for testing. The value is not currently stored in here.
    //bool immediate;
};

struct LIBMVME_EXPORT MasterTrigger
{
    bool activate;
    // Note: the immediate flag is used to directly exec the stack which can be
    // useful for testing. The value is not currently stored in here.
    //bool immediate;
};

struct LIBMVME_EXPORT Counter
{
    static const u16 InputCount = 2u; // counter and latch inputs
    // If clearOnLatch is set the latch signal will also reset the counter to 0
    // (Frequency Counter Mode).
    bool clearOnLatch;
    bool softActivate;
};

struct LIBMVME_EXPORT IRQ_Util
{
    // zero-based IRQ index (0 == IRQ1, 6 == IRQ7)
    u8 irqIndex;
};

// SlaveTrigger consisting of a gate generator and the slave trigger index to
// output.
struct LIBMVME_EXPORT SlaveTrigger
{
    IO gateGenerator;
    u8 triggerIndex; // slave trigger index to output (0..3)
};

// Generic trigger resource unit. Replaces individual IRQ, SoftTrigger, and
// SlaveTrigger units since FW0016_55.
struct LIBMVME_EXPORT TriggerResource
{
    // Note: 'type' is stored in connection register offset 0,
    // slaveTrigger.triggerIndex is stored in connection register offset 2.

    enum class Type: u8
    {
        IRQ,
        SoftTrigger,
        SlaveTrigger
    };

    Type type;
    IRQ_Util irqUtil;
    SlaveTrigger slaveTrigger;
};

// Addressing: level, unit [, output]
// output used to address LUT output pins in levels 1 and 2
// This is not perfect as not every pin in the system can be addressed. Right
// now the output pins are addressed except for L3 where the current code is
// dealing with the input pins.
using UnitAddress = std::array<unsigned, 3>;
using UnitAddressVector = std::vector<UnitAddress>;

inline unsigned level(const UnitAddress &a) { return a[0]; }
inline unsigned unit(const UnitAddress &a) { return a[1]; }
inline unsigned subunit(const UnitAddress &a) { return a[2]; }

struct LIBMVME_EXPORT UnitConnection
{
    static UnitConnection makeDynamic()
    {
        UnitConnection res{0, 0, 0};
        res.isDynamic = true;
        return res;
    }

    UnitConnection(unsigned level, unsigned unit, unsigned subunit = 0)
        : address({level, unit, subunit})
    {}

    unsigned level() const { return address[0]; }
    unsigned unit() const { return address[1]; }
    unsigned subunit() const { return address[2]; }

    unsigned operator[](size_t index) const { return address[index]; }
    unsigned &operator[](size_t index) { return address[index]; }

    UnitConnection(const UnitConnection &other) = default;
    UnitConnection &operator=(const UnitConnection &other) = default;

    bool isDynamic = false;
    bool isAvailable = true;
    UnitAddress address = {};
};

using LUT_Connections = std::array<UnitConnection, trigger_io::LUT::InputBits>;

static const QString UnitNotAvailable = "N/A";

struct LIBMVME_EXPORT Level0
{
    static const int TriggerResourceCount = 8;
    static const int TriggerResourceOffset = 4;

    static const int StackBusyCount = 2;
    static const int StackBusyOffset = 12;

    static const int SysClockOffset = 14;
    static const int DAQStartOffset = 15;

    static const size_t UtilityUnitCount = 16;

    static const int NIM_IO_Offset = 16;

    // Address gap between the NIM and IRQ units. These addresses are taken by
    // the LVDS outputs on Level3.
    static const int NIM_to_IRQ_Gap = ECL_OUT_Count;

    // Describes the IRQ Inputs on L0 (since FW0016)
    static const int IRQ_Inputs_Count = 6;
    static const int IRQ_Inputs_Offset = 33;

    // Total number of output pins on L0 including the unused gap pins.
    static const int OutputCount = UtilityUnitCount + NIM_IO_Count
        + NIM_to_IRQ_Gap + IRQ_Inputs_Count;

    static const std::array<QString, OutputCount> DefaultUnitNames;

    std::array<Timer, TimerCount> timers;                               // 0..3
    std::array<TriggerResource, TriggerResourceCount> triggerResources; // 4..11

    std::array<StackBusy, StackBusyCount> stackBusy;                    // 12, 13
                                                                        // 14 sysclock
                                                                        // 15 daq_start
    std::array<IO, NIM_IO_Count> ioNIM;                                 // 16..29
    // 30..32 is shared with the L3 ECL Outputs
    std::array<IO, IRQ_Inputs_Count> ioIRQ;                             // 33..38 IRQ inputs
                                                                        // 48 Digital Oscilloscope

    QStringList unitNames;

    Level0();
};

static const size_t LUT_DynamicInputCount = 3;
using LUT_DynamicConnections = std::array<unsigned, LUT_DynamicInputCount>;

struct LIBMVME_EXPORT Level1
{
    static const size_t LUTCount = 7;
    static const std::array<LUT_Connections, LUTCount> StaticConnections;

    std::array<LUT, LUTCount> luts;

    static const std::vector<UnitAddressVector> LUT2DynamicInputChoices;

    // The first 3 inputs of L1.LUT2 have dynamic connections. The selected
    // value is stored here.
    LUT_DynamicConnections lut2Connections;

    Level1();
};

struct LIBMVME_EXPORT Level2
{
    struct LUTDynamicInputChoices
    {
        std::vector<UnitAddressVector> lutChoices;
        UnitAddressVector strobeChoices;
    };

    static const size_t LUTCount = 3;
    static const std::array<LUT_Connections, LUTCount> StaticConnections;

    // List of possible input connection choices per LUT (including the LUTs
    // strobe GG input).
    static const std::array<LUTDynamicInputChoices, LUTCount> DynamicInputChoices;

    std::array<LUT, LUTCount> luts;

    // The first 3 inputs of each LUT have dynamic connections. The selected
    // value is stored here.
    std::array<LUT_DynamicConnections, LUTCount> lutConnections;

    // The strobe GG is also dynamically connected.
    std::array<unsigned, trigger_io::Level2::LUTCount> strobeConnections;

    Level2();
};

struct LIBMVME_EXPORT Level3
{
    static const size_t StackStartCount = 4;

    static const size_t MasterTriggersCount = 4;
    static const size_t MasterTriggersOffset = 4;

    static const size_t CountersCount = 8;
    static const size_t CountersOffset = 8;

    static const size_t UtilityUnitCount =
        StackStartCount + MasterTriggersCount + CountersCount;

    static const size_t UnitCount =
        UtilityUnitCount + NIM_IO_Count + ECL_OUT_Count;

    static const int NIM_IO_Unit_Offset = 16;
    static const int ECL_Unit_Offset = 30;

    static const unsigned CounterInputNotConnected = 21;
    static const unsigned ExcludedSysclock = 20;

    static const std::array<QString, trigger_io::Level3::UnitCount+1> DefaultUnitNames;
    static const std::vector<std::vector<UnitAddressVector>> DynamicInputChoiceLists;

    std::array<StackStart, StackStartCount> stackStart = {};
    std::array<MasterTrigger, MasterTriggersCount> masterTriggers = {};
    std::array<Counter, CountersCount> counters = {};
    // Same as in level0. The logic keeps it in sync.
    std::array<IO, NIM_IO_Count> ioNIM = {};
    std::array<IO, ECL_OUT_Count> ioECL = {};

    QStringList unitNames;

    // Per {unit, unit-input} connection values.
    std::array<std::vector<unsigned>, trigger_io::Level3::UnitCount> connections = {};

    Level3();
    Level3(const Level3 &) = default;
    Level3 &operator=(const Level3 &) = default;
};

struct LIBMVME_EXPORT TriggerIO
{
    Level0 l0;
    Level1 l1;
    Level2 l2;
    Level3 l3;
};

// Returns the user supplied name for the given address stored in the
// TriggerIO.
LIBMVME_EXPORT QString lookup_name(const TriggerIO &ioCfg, const UnitAddress &addr);

// Returns the default name/unit name for the given address.
LIBMVME_EXPORT QString lookup_default_name(const TriggerIO &ioCfg, const UnitAddress &addr);

// Reset all pin names to their default value
LIBMVME_EXPORT void reset_names(TriggerIO &ioCfg);

// Given a unit address looks up the 'connect' value for that unit. These
// values have different meaning depending on the unit being checked (e.g. L3
// NIM outputs can only connect to the L2 LUTs).
LIBMVME_EXPORT unsigned get_connection_value(const TriggerIO &ioCfg, const UnitAddress &addr);

// Given a unit address this function looks up the 'connect' value stored in
// the TriggerIO setup (get_connection_value()) then resolves this value to the
// UnitAddress of the source unit of the connection.
LIBMVME_EXPORT UnitAddress get_connection_unit_address(const TriggerIO &ioCfg, const UnitAddress &addr);

// This is how the mappings of a single LUT are stored in the MVLC memory.
// 6 input bits, 4 output bits, 3 of which are used.
// => 2^6 * 4 bits = 64 * 4 bits = 256 bits needed.
// 4  4-bit nibbles are stored in a single 16 bit word in the RAM.
// => 16 rows of memory needed to store all 64 nibbles.
using LUT_RAM = std::array<u16, 16>;

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

    if (value > 7)
        throw std::out_of_range("LUT value > 2^3  - 1");

    u8 cell = address >> 2;
    u8 nibble =  address & 0b11;
    u8 shift = nibble * 4;
    u16 clearNibbleMask = ~(0xfu << shift);

    lut[cell] &= clearNibbleMask;
    lut[cell] |= (value & 0xf) << shift;
}

Timer::Range LIBMVME_EXPORT timer_range_from_string(const std::string &str);

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_H__ */
