#ifndef __MVME_MVLC_TRIGGER_IO_H__
#define __MVME_MVLC_TRIGGER_IO_H__

#include <array>
#include <bitset>
#include <stdexcept>
#include <vector>

#include <QString>
#include <QStringList>

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
    u16 stackIndex;
};

using Names = std::vector<QString>;

using UnitPinNames = std::vector<Names>;
using LevelUnitPinNames = std::vector<UnitPinNames>;

using LevelUnitNames = std::vector<Names>;

using PinConnectionChoices = std::vector<unsigned>;
using UnitInputChoices = std::vector<PinConnectionChoices>;
using LevelUnitInputChoices = std::vector<UnitInputChoices>;
using UnitConnectionSelection = std::vector<unsigned>;
using LevelUnitConnectionSelections = std::vector<UnitConnectionSelection>;

struct LUT
{
    static const u16 InputBits = 6;
    static const u16 OutputBits = 3;
    static const u16 InputCombinations = 1u << InputBits;
    static const u16 StrobeGGDefaultWidth = 8;
    // This 'fake' value is used to refer to the LUTs strobe gate generator, e.g
    // UnitAddress {2, 0, StrobeGGInput } for L2.LUT0.StrobeGG
    static const u16 StrobeGGInput = InputBits;

    // Holds the output state for all 64 input combinations. One of the logic LUTs
    // is made up for three of these mappings, one for each output bit.
    using Bitmap = std::bitset<64>;
    using Contents = std::array<Bitmap, trigger_io::LUT::OutputBits>;


    LUT();
    LUT(const LUT &) = default;
    LUT &operator=(const LUT &) = default;

    Contents lutContents;

    // Bit mask determining which outputs are to be strobed.
    std::bitset<OutputBits> strobedOutputs;

    // Strobe gate generator settings. Each Level2 LUT has one of these.
    IO strobeGG = { .delay = 0, .width = StrobeGGDefaultWidth };

    std::array<QString, OutputBits> defaultOutputNames;
    std::array<QString, OutputBits> outputNames;
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
    // Empty right now. Maybe forever :)
};

struct IRQ_Unit
{
    // zero-based IRQ index (0 == IRQ1, 6 == IRQ7)
    u8 irqIndex;
};

#if 0
struct UnitAddress: public std::array<int, 3>
{
    static const UnitAddress Invalid;
    static const UnitAddress Dynamic;

    UnitAddress(int level = -1, int unit = -1, int pin = -1)
        : std::array<int, 3>{{level, unit, pin}}
    {
    }

    int level() const { return (*this)[0]; }
    int unit() const { return (*this)[1]; }
    int pin() const { return (*this)[2]; }

    bool isLevelAddress() const { return level() >= 0 && unit() < 0 };
    bool isUnitAddress() const { return !isLevelAddress() && pin() < 0 };
    bool isPinAddress() const { return !isLevelAddress && !isUnitAddress(); }
};
#else
// Addressing: level, unit [, subunit]
// subunit used to address LUT outputs in levels 1 and 2
using UnitAddress = std::array<unsigned, 3>;
#endif
using UnitAddressVector = std::vector<UnitAddress>;

struct UnitConnection
{
    static UnitConnection makeDynamic(bool available = true)
    {
        UnitConnection res{0, 0, 0};
        res.isDynamic = true;
        res.isAvailable = available;
        return res;
    }

#if 0
    UnitConnection(int level, int unit, int subunit = 0)
        : address({level, unit, subunit})
    {}
#else
    UnitConnection(unsigned level, unsigned unit, unsigned subunit = 0)
        : address({level, unit, subunit})
    {}
#endif

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

struct Level0
{
    static const int OutputCount = 30;
    static const int NIM_IO_Offset = 16;

    static const int IRQ_UnitCount = 2;
    static const int IRQ_UnitOffset = 4;

    static const int SoftTriggerCount = 2;
    static const int SoftTriggerOffset = 6;

    static const int SlaveTriggerCount = 4;
    static const int SlaveTriggerOffset = 8;

    static const int StackBusyCount = 2;
    static const int StackBusyOffset = 12;

    static const size_t UtilityUnitCount = 14;

    static const std::array<QString, OutputCount> DefaultUnitNames;

    std::array<Timer, TimerCount> timers;               // 0..3
    std::array<IRQ_Unit, IRQ_UnitCount> irqUnits;       // 4, 5 are irq units
                                                        // 6, 7 are software triggers
    std::array<IO, SlaveTriggerCount> slaveTriggers;    // 8..11
    std::array<StackBusy, StackBusyCount> stackBusy;    // 12, 13
                                                        // 14, 15 unused
    std::array<IO, NIM_IO_Count> ioNIM;                 // 16..29

    QStringList unitNames;

    Level0();
};

struct Level1
{
    static const size_t LUTCount = 5;
    static const std::array<LUT_Connections, LUTCount> StaticConnections;

    std::array<LUT, LUTCount> luts;

    Level1();
};

struct Level2
{
    static const size_t LUTCount = 2;
    static const std::array<LUT_Connections, LUTCount> StaticConnections;
    static const size_t LUT_DynamicInputCount = 3;

    using DynamicConnections = std::array<unsigned, LUT_DynamicInputCount>;

    struct LUTDynamicInputChoices
    {
        std::vector<UnitAddressVector> lutChoices;
        UnitAddressVector strobeChoices;
    };

    // List of possible input connection choices per LUT (including the LUTs
    // strobe GG input).
    static const std::array<LUTDynamicInputChoices, LUTCount> DynamicInputChoices;

    std::array<LUT, LUTCount> luts;

    // The first 3 inputs of each LUT have dynamic connections. The selected
    // value is stored here.
    std::array<DynamicConnections, LUTCount> lutConnections;

    // The strobe GG is also dynamically connected.
    std::array<unsigned, trigger_io::Level2::LUTCount> strobeConnections;

    Level2();
};

struct Level3
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

    static const size_t NIM_IO_Unit_Offset = 16;
    static const size_t ECL_Unit_Offset = 30;

    static const std::array<QString, trigger_io::Level3::UnitCount> DefaultUnitNames;
    static const std::vector<UnitAddressVector> DynamicInputChoiceLists;

    std::array<StackStart, StackStartCount> stackStart = {};
    std::array<MasterTrigger, MasterTriggersCount> masterTriggers = {};
    std::array<Counter, CountersCount> counters = {};
    // FIXME: maybe get rid of this. it's the same as in level0. The logic
    // keeps it in sync.
    std::array<IO, NIM_IO_Count> ioNIM = {};
    std::array<IO, ECL_OUT_Count> ioECL = {};

    QStringList unitNames;
    std::array<unsigned, trigger_io::Level3::UnitCount> connections = {};

    Level3();
    Level3(const Level3 &) = default;
    Level3 &operator=(const Level3 &) = default;
};

struct TriggerIO
{
    Level0 l0;
    Level1 l1;
    Level2 l2;
    Level3 l3;

    // TODO: more generic structure for names and connection choices and
    // connection selections
    static const LevelUnitNames DefaultUnitNames;
    static const LevelUnitPinNames DefaultPinNames;
    static const LevelUnitInputChoices UnitInputChoices;

    LevelUnitNames unitNames;
    LevelUnitPinNames pinNames;
    LevelUnitConnectionSelections connections;
};

QString lookup_name(const TriggerIO &ioCfg, const UnitAddress &addr);
QString lookup_default_name(const TriggerIO &ioCfg, const UnitAddress &addr);

QString lookup_name_2(const TriggerIO &ioCfg, const UnitAddress &addr);
QString lookup_default_name_2(const TriggerIO &ioCfg, const UnitAddress &addr);

// Given a unit address looks up the 'connect' value for that unit. These
// values have different meaning depending on the unit being checked (e.g. L3
// NIM outputs can only connect to the L2 LUTs).
unsigned get_connection_value(const TriggerIO &ioCfg, const UnitAddress &addr);

// Given a unit address this function looks up the 'connect' value stored in
// the TriggerIO setup (get_connection_value()) then resolves this value to the
// UnitAddress of the source unit of the connection.
UnitAddress get_connection_unit_address(const TriggerIO &ioCfg, const UnitAddress &addr);

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

    if (value > 8)
        throw std::out_of_range("LUT value > 2^3  - 1");

    u8 cell = address >> 2;
    u8 nibble =  address & 0b11;
    u8 shift = nibble * 4;
    u16 clearNibbleMask = ~(0xfu << shift);

    lut[cell] &= clearNibbleMask;
    lut[cell] |= (value & 0xf) << shift;
}

} // end namespace trigger_io
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_H__ */
