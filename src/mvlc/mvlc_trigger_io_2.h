#ifndef __MVME_MVLC_TRIGGER_IO_2_H__
#define __MVME_MVLC_TRIGGER_IO_2_H__

#include <QString>
#include <QStringList>

#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvlc
{
namespace trigger_io_config
{

// Addressing: level, unit [, subunit]
// subunit used to address LUT outputs in levels 1 and 2
using UnitAddress = std::array<unsigned, 3>;

struct UnitConnection
{
    static UnitConnection makeDynamic(bool available = true)
    {
        UnitConnection res{0, 0, 0};
        res.isDynamic = true;
        res.isAvailable = available;
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

using OutputMapping = std::bitset<trigger_io::LUT::InputCombinations>;

using LUT_Connections = std::array<UnitConnection, trigger_io::LUT::InputBits>;

static const size_t Level2LUT_VariableInputCount = 3;
using LUT_DynConValues = std::array<unsigned, Level2LUT_VariableInputCount>;

struct LUT
{
    // one bitset for each output
    using Contents = std::array<OutputMapping, trigger_io::LUT::OutputBits>;
    Contents lutContents;
    std::array<QString, trigger_io::LUT::OutputBits> outputNames;

    // Strobe gate generator settings
    trigger_io::IO strobeGG =
    {
        .delay = 0,
        .width = trigger_io::LUT::StrobeGGDefaultWidth,
    };

    std::bitset<trigger_io::LUT::OutputBits> strobedOutputs;

    LUT();
    LUT(const LUT &) = default;
    LUT &operator=(const LUT &) = default;
};

struct Level0: public trigger_io::Level0
{
    static const std::array<QString, trigger_io::Level0::OutputCount> DefaultUnitNames;

    QStringList unitNames;

    // TODO: store default names in here. Otherwise they have to be generated
    // in multiple places.

    Level0();
};

struct Level1
{
    static const std::array<LUT_Connections, trigger_io::Level1::LUTCount> StaticConnections;

    std::array<LUT, trigger_io::Level1::LUTCount> luts;

    Level1();
};

// TODO: merge with trigger_io::Level2
struct Level2
{
    static const std::array<LUT_Connections, trigger_io::Level2::LUTCount> StaticConnections;
    //static const std::array<UnitAddress, 2 * trigger_io::LUT::OutputBits> OutputPinMapping;

    std::array<LUT, trigger_io::Level2::LUTCount> luts;

    // The first 3 inputs of each LUT have dynamic connections. The selected
    // value is stored here.
    std::array<LUT_DynConValues, trigger_io::Level2::LUTCount> lutConnections;
    std::array<unsigned, trigger_io::Level2::LUTCount> strobeConnections;

    Level2();
};

using UnitAddressVector = std::vector<UnitAddress>;

// Input choices for a single lut on level 2
struct Level2LUTDynamicInputChoices
{
    std::vector<UnitAddressVector> inputChoices;
    UnitAddressVector strobeInputChoices;
};

Level2LUTDynamicInputChoices make_level2_input_choices(unsigned unit);

struct Level3: public trigger_io::Level3
{
    static const std::array<QString, trigger_io::Level3::UnitCount> DefaultUnitNames;
    // A list of possible input addresses for each level 3 input pin.
    std::vector<UnitAddressVector> dynamicInputChoiceLists;

    QStringList unitNames;

    std::array<unsigned, trigger_io::Level3::UnitCount> connections = {};

    Level3();
    Level3(const Level3 &) = default;
    Level3 &operator=(const Level3 &) = default;
};

struct TriggerIOConfig
{
    Level0 l0;
    Level1 l1;
    Level2 l2;
    Level3 l3;
};

namespace gen_flags
{
    using Flag = u8;
    static const Flag Default = 0u;
    static const Flag MetaIncludeDefaultUnitNames = 1u << 0;
};

QString lookup_name(const TriggerIOConfig &cfg, const UnitAddress &addr);

QString generate_trigger_io_script_text(
    const TriggerIOConfig &ioCfg,
    const gen_flags::Flag &flags = gen_flags::MetaIncludeDefaultUnitNames);

TriggerIOConfig parse_trigger_io_script_text(const QString &text);

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config

#endif /* __MVME_MVLC_TRIGGER_IO_2_H__ */
