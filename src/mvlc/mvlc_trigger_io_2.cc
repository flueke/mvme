#include "mvlc/mvlc_trigger_io_2.h"

#include <boost/range/adaptor/indexed.hpp>
#include <boost/variant.hpp>
#include <QMap>
#include <QVector>
#include <yaml-cpp/yaml.h>

#include "vme_script.h"

using boost::adaptors::indexed;

namespace mesytec
{
namespace mvlc
{
namespace trigger_io_config
{

LUT::LUT()
{
    lutContents.fill({});
    outputNames.fill({});
}

static const QString UnitNotAvailable = "N/A";

const std::array<QString, trigger_io::Level0::OutputCount> Level0::DefaultUnitNames =
{
    "timer0",
    "timer1",
    "timer2",
    "timer3",
    "IRQ0",
    "IRQ1",
    "soft_trigger0",
    "soft_trigger1",
    "slave_trigger0",
    "slave_trigger1",
    "slave_trigger2",
    "slave_trigger3",
    "stack_busy0",
    "stack_busy1",
    UnitNotAvailable,
    UnitNotAvailable,
    "NIM0",
    "NIM1",
    "NIM2",
    "NIM3",
    "NIM4",
    "NIM5",
    "NIM6",
    "NIM7",
    "NIM8",
    "NIM9",
    "NIM10",
    "NIM11",
    "NIM12",
    "NIM13",
    "ECL0",
    "ECL1",
    "ECL2",
};

Level0::Level0()
{
    unitNames.reserve(DefaultUnitNames.size());

    std::copy(DefaultUnitNames.begin(), DefaultUnitNames.end(),
              std::back_inserter(unitNames));

    timers.fill({});
    irqUnits.fill({});
    slaveTriggers.fill({});
    stackBusy.fill({});
    ioNIM.fill({});
}

// Level 1 connections including internal ones between the LUTs.
const std::array<LUT_Connections, trigger_io::Level1::LUTCount> Level1::StaticConnections =
{
    {
        // L1.LUT0
        { { {0, 16}, {0, 17}, {0, 18}, {0, 19}, {0, 20}, {0, 21} } },
        // L1.LUT1
        { { {0, 20}, {0, 21}, {0, 22}, {0, 23}, {0, 24}, {0, 25} } },
        // L1.LUT2
        { { {0,  24}, {0,  25}, {0, 26}, {0, 27}, {0, 28}, {0, 29} }, },

        // L1.LUT3
        { { {1, 0, 0}, {1, 0, 1}, {1, 0, 2}, {1, 1, 0}, {1, 1, 1}, {1, 1, 2} }, },
        // L1.LUT4
        { { {1, 1, 0}, {1, 1, 1}, {1, 1, 2}, {1, 2, 0}, {1, 2, 1}, {1, 2, 2} }, },
    },
};

Level1::Level1()
{
    for (size_t unit = 0; unit < luts.size(); unit++)
    {
        for (size_t output = 0; output < luts[unit].outputNames.size(); output++)
        {
            luts[unit].outputNames[output] = QString("L1.LUT%1.OUT%2").arg(unit).arg(output);
        }
    }
}

// Level 2 connections. This table includes fixed and dynamic connections.
static const UnitConnection Dynamic = UnitConnection::makeDynamic();

// Using level 1 unit + output address values (basically full addresses
// without the need for a Level1::OutputPinMapping).
const std::array<LUT_Connections, trigger_io::Level2::LUTCount> Level2::StaticConnections =
{
    {
        // L2.LUT0
        { Dynamic, Dynamic, Dynamic , { 1, 3, 0 }, { 1, 3, 1 }, { 1, 3, 2 } },
        // L2.LUT1
        { Dynamic, Dynamic, Dynamic , { 1, 4, 0 }, { 1, 4, 1 }, { 1, 4, 2 } },
    },
};

// Unit is 0/1 for LUT0/1
Level2LUTDynamicInputChoices make_level2_input_choices(unsigned unit)
{
    // Common to all inputs: can connect to all Level0 utility outputs.
    std::vector<UnitAddress> common(trigger_io::Level0::UtilityUnitCount);

    for (unsigned i = 0; i < common.size(); i++)
        common[i] = { 0, i };

    Level2LUTDynamicInputChoices ret;
    ret.inputChoices = { common, common, common };
    ret.strobeInputChoices = common;

    if (unit == 0)
    {
        // L2.LUT0 can connect to L1.LUT4
        for (unsigned i = 0; i < 3; i++)
            ret.inputChoices[i].push_back({ 1, 4, i });
    }
    else if (unit == 1)
    {
        // L2.LUT1 can connecto to L1.LUT3
        for (unsigned i = 0; i < 3; i++)
            ret.inputChoices[i].push_back({ 1, 3, i });
    }

    // Strobe inputs can connect to all 6 level 1 outputs
    for (unsigned i = 0; i < 3; i++)
        ret.strobeInputChoices.push_back({ 1, 3, i });

    for (unsigned i = 0; i < 3; i++)
        ret.strobeInputChoices.push_back({ 1, 4, i });

    return ret;
};

Level2::Level2()
{
    for (size_t unit = 0; unit < luts.size(); unit++)
    {
        for (size_t output = 0; output < luts[unit].outputNames.size(); output++)
        {
            luts[unit].outputNames[output] = QString("L2.LUT%1.OUT%2").arg(unit).arg(output);
        }
    }

    lutConnections.fill({});
    strobeConnections.fill({});
}

const std::array<QString, trigger_io::Level3::UnitCount> Level3::DefaultUnitNames =
{
    "StackStart0",
    "StackStart1",
    "StackStart2",
    "StackStart3",
    "MasterTrigger0",
    "MasterTrigger1",
    "MasterTrigger2",
    "MasterTrigger3",
    "Counter0",
    "Counter1",
    "Counter2",
    "Counter3",
    UnitNotAvailable,
    UnitNotAvailable,
    UnitNotAvailable,
    UnitNotAvailable,
    "NIM0",
    "NIM1",
    "NIM2",
    "NIM3",
    "NIM4",
    "NIM5",
    "NIM6",
    "NIM7",
    "NIM8",
    "NIM9",
    "NIM10",
    "NIM11",
    "NIM12",
    "NIM13",
    "ECL0",
    "ECL1",
    "ECL2",
};

static std::vector<UnitAddressVector> make_level3_dynamic_input_choice_lists()
{
    static const std::vector<UnitAddress> Level2Full =
    {
        { 2, 0, 0 },
        { 2, 0, 1 },
        { 2, 0, 2 },
        { 2, 1, 0 },
        { 2, 1, 1 },
        { 2, 1, 2 },
    };

    std::vector<UnitAddressVector> result;

    // Note: StackStarts, MasterTriggers and Counters had different connection
    // choices in early firmware versions., that's the reason they are still
    // separated below. Now they all can connect to L0 up to unit 13 and the 6
    // outputs of L2.

    for (size_t i = 0; i < trigger_io::Level3::StackStartCount; i++)
    {
        static const unsigned LastL0Unit = 13;

        std::vector<UnitAddress> choices;

        for (unsigned unit = 0; unit <= LastL0Unit; unit++)
            choices.push_back({0, unit });

        std::copy(Level2Full.begin(), Level2Full.end(), std::back_inserter(choices));
        result.emplace_back(choices);
    }

    for (size_t i = 0; i < trigger_io::Level3::MasterTriggersCount; i++)
    {
        static const unsigned LastL0Unit = 13;

        std::vector<UnitAddress> choices;

        // Can connect up to the IRQ units
        for (unsigned unit = 0; unit <= LastL0Unit; unit++)
            choices.push_back({0, unit });

        std::copy(Level2Full.begin(), Level2Full.end(), std::back_inserter(choices));
        result.emplace_back(choices);
    }

    for (size_t i = 0; i < trigger_io::Level3::CountersCount; i++)
    {
        static const unsigned LastL0Unit = 13;

        std::vector<UnitAddress> choices;

        // Can connect all L0 utilities up to StackBusy1
        for (unsigned unit = 0; unit <= LastL0Unit; unit++)
            choices.push_back({0, unit });

        std::copy(Level2Full.begin(), Level2Full.end(), std::back_inserter(choices));
        result.emplace_back(choices);
    }

    // 4 unused inputs between the last counter (11) and the first NIM_IO (16)
    for (size_t i = 0; i < 4; i++)
    {
        result.push_back({});
    }

    // NIM and ECL outputs can connect to Level2 only
    for (size_t i = 0; i < (trigger_io::NIM_IO_Count + trigger_io::ECL_OUT_Count); i++)
    {
        std::vector<UnitAddress> choices = Level2Full;
        result.emplace_back(choices);
    }

    return result;
}

Level3::Level3()
    : dynamicInputChoiceLists(make_level3_dynamic_input_choice_lists())
{
    stackStart.fill({});
    masterTriggers.fill({});
    counters.fill({});
    ioNIM.fill({});
    ioECL.fill({});

    connections.fill(0);

    std::copy(DefaultUnitNames.begin(), DefaultUnitNames.end(),
              std::back_inserter(unitNames));
}

QString lookup_name(const TriggerIOConfig &cfg, const UnitAddress &addr)
{
    switch (addr[0])
    {
        case 0:
            return cfg.l0.unitNames.value(addr[1]);

        case 1:
            return cfg.l1.luts[addr[1]].outputNames[addr[2]];

        case 2:
            return cfg.l2.luts[addr[1]].outputNames[addr[2]];

        case 3:
            return cfg.l3.unitNames.value(addr[1]);
    }

    return {};
}

struct Write
{
    // Opt_HexValue indicates that the register value should be printed in
    // hexadecimal instead of decimal.
    static const unsigned Opt_HexValue = 1u << 0;;

    // TODO: support this when generating the script
    // Opt_BinValue indicates that the register value should be printed in
    // binary (0bxyz literatl) instead of decimal.
    static const unsigned Opt_BinValue = 1u << 1;

    // Relative register address. Only the low two bytes are stored.
    u16 address;

    // 16 bit MVLC register value.
    u16 value;

    // Comment written one the same line as the write.
    QString comment;

    // OR of the Opt_* constants defined above.
    unsigned options = 0u;

    Write() = default;

    Write(u16 address_, u16 value_, const QString &comment_ = {}, unsigned options_ = 0u)
        : address(address_)
        , value(value_)
        , comment(comment_)
        , options(options_)
    {}

    Write(u16 address_, u16 value_, unsigned options_)
        : address(address_)
        , value(value_)
        , options(options_)
    {}
};

// Variant containing either a register write or a block comment. If the 2nd
// type is set it indicates the start of a new block in the generated script
// text. The following writes will be preceded by and empty line and a comment
// containing the string value on a separate line.
using ScriptPart = boost::variant<Write, QString>;
using ScriptParts = QVector<ScriptPart>;

ScriptPart select_unit(int level, int unit, const QString &unitName = {})
{
    auto ret = Write{ 0x0200,  static_cast<u16>(((level << 8) | unit)), Write::Opt_HexValue };

#if 1
    ret.comment = QString("select L%1.Unit%2").arg(level).arg(unit);

    if (!unitName.isEmpty())
        ret.comment += unitName;
#endif

    return ret;
};

// Note: the desired unit must be selected prior to calling this function.
ScriptPart write_unit_reg(u16 reg, u16 value, const QString &comment, unsigned writeOpts = 0u)
{
    auto ret = Write { static_cast<u16>(0x0300u + reg), value, comment, writeOpts };

    return ret;
}

ScriptPart write_unit_reg(u16 reg, u16 value, unsigned writeOpts = 0u)
{
    return write_unit_reg(reg, value, {}, writeOpts);
}

// FIXME: unify write_connection and write_strobe_connection

// Note: the desired unit must be selected prior to calling this function.
ScriptPart write_connection(u16 offset, u16 value, const QString &sourceName = {})
{
    auto ret = Write { static_cast<u16>(0x0380u + offset), value };

    if (!sourceName.isEmpty())
        ret.comment = QString("connect input%1 to '%2'")
            .arg(offset / 2).arg(sourceName);

    return ret;
}

ScriptPart write_strobe_connection(u16 offset, u16 value, const QString &sourceName = {})
{
    auto ret = Write { static_cast<u16>(0x0380u + offset), value };

    if (!sourceName.isEmpty())
        ret.comment = QString("connect strobe_input to '%1'").arg(sourceName);

    return ret;
}

ScriptParts generate(const trigger_io::Timer &unit, int index)
{
    ScriptParts ret;
    ret += write_unit_reg(2, static_cast<u16>(unit.range), "range (ns, us, ms, s)");
    ret += write_unit_reg(4, unit.delay_ns, "delay [ns]");
    ret += write_unit_reg(6, unit.period, "period");
    return ret;
}

ScriptParts generate(const trigger_io::IRQ_Unit &unit, int index)
{
    ScriptParts ret;
    ret += write_unit_reg(0, static_cast<u16>(unit.irqIndex), "irq_index");
    return ret;
}

namespace io_flags
{
    using Flags = u8;
    static const Flags HasDirection    = 1u << 0;
    static const Flags HasActivation   = 1u << 1;
    static const Flags StrobeGGOffsets = 1u << 2;

    static const Flags None             = 0u;
    static const Flags NIM_IO_Flags     = HasDirection | HasActivation;
    static const Flags ECL_IO_Flags     = HasActivation;
    static const Flags StrobeGG_Flags   = StrobeGGOffsets;
}

/* The IO structure is used for different units sharing IO properties:
 * NIM I/Os, ECL Outputs, slave triggers, and strobe gate generators.
 * The common properties are delay, width, holdoff and invert. They start at
 * register offset 0 except for the strobe GGs where the registers are offset
 * by one address increment (2 bytes).
 * The activation and direction registers are at offsets 10 and 16. They are
 * only written out if the respective io_flags bit is set.
 */
ScriptParts generate(const trigger_io::IO &io, const io_flags::Flags &ioFlags)
{
    ScriptParts ret;

    u16 offset = (ioFlags & io_flags::StrobeGGOffsets) ? 0x32u : 0u;

    ret += write_unit_reg(offset + 0, io.delay, "delay");
    ret += write_unit_reg(offset + 2, io.width, "width");
    ret += write_unit_reg(offset + 4, io.holdoff, "holdoff");
    ret += write_unit_reg(offset + 6, static_cast<u16>(io.invert), "invert");

    if (ioFlags & io_flags::HasDirection)
        ret += write_unit_reg(10, static_cast<u16>(io.direction), "direction (0=in, 1=out)");

    if (ioFlags & io_flags::HasActivation)
        ret += write_unit_reg(16, static_cast<u16>(io.activate), "output activate");

    return ret;
}

ScriptParts generate(const trigger_io::StackBusy &unit, int index)
{
    ScriptParts ret;
    ret += write_unit_reg(0, unit.stackIndex, "stack_index");
    return ret;
}

trigger_io::LUT_RAM make_lut_ram(const LUT &lut)
{
    trigger_io::LUT_RAM ram = {};

    for (size_t address = 0; address < lut.lutContents[0].size(); address++)
    {
        unsigned ramValue = 0u;

        // Combine the three separate output entries into a single value
        // suitable for the MVLC LUT RAM.
        for (unsigned output = 0; output < lut.lutContents.size(); output++)
        {
            if (lut.lutContents[output].test(address))
            {
                ramValue |= 1u << output;
                assert(ramValue < (1u << trigger_io::LUT::OutputBits));
            }
        }

        trigger_io::set(ram, address, ramValue);
    }

    return ram;
}

ScriptParts write_lut_ram(const trigger_io::LUT_RAM &ram)
{
    ScriptParts ret;

    for (const auto &kv: ram | indexed(0))
    {
        u16 reg = kv.index() * sizeof(u16); // register address increment is 2 bytes
        u16 cell = reg * 2;
        auto comment = QString("cells %1-%2").arg(cell).arg(cell + 3);
        ret += write_unit_reg(reg, kv.value(), comment, Write::Opt_HexValue);
    }

    return ret;
}

ScriptParts write_lut(const LUT &lut)
{
    return write_lut_ram(make_lut_ram(lut));
}

ScriptParts generate(const trigger_io::StackStart &unit, int index)
{
    ScriptParts ret;
    ret += write_unit_reg(0, static_cast<u16>(unit.activate), "activate");
    ret += write_unit_reg(2, unit.stackIndex, "stack index");
    return ret;
}

ScriptParts generate(const trigger_io::MasterTrigger &unit, int index)
{
    ScriptParts ret;
    ret += write_unit_reg(0, static_cast<u16>(unit.activate), "activate");
    return ret;
}

ScriptParts generate(const trigger_io::Counter &unit, int index)
{
    return {};
}

ScriptParts generate_trigger_io_script(const TriggerIOConfig &ioCfg)
{
    ScriptParts ret;

    //
    // Level0
    //

    ret += "Level0 ##############################";

    for (const auto &kv: ioCfg.l0.timers | indexed(0))
    {
        ret += ioCfg.l0.unitNames[kv.index()];
        ret += select_unit(0, kv.index());
        ret += generate(kv.value(), kv.index());
    }

    for (const auto &kv: ioCfg.l0.irqUnits | indexed(0))
    {
        ret += ioCfg.l0.unitNames[kv.index() + ioCfg.l0.IRQ_UnitOffset];
        ret += select_unit(0, kv.index() + ioCfg.l0.IRQ_UnitOffset);
        ret += generate(kv.value(), kv.index());
    }

    for (const auto &kv: ioCfg.l0.slaveTriggers | indexed(0))
    {
        ret += ioCfg.l0.unitNames[kv.index() + ioCfg.l0.SlaveTriggerOffset];
        ret += select_unit(0, kv.index() + ioCfg.l0.SlaveTriggerOffset);
        ret += generate(kv.value(), io_flags::None);
    }

    for (const auto &kv: ioCfg.l0.stackBusy | indexed(0))
    {
        ret += ioCfg.l0.unitNames[kv.index() + ioCfg.l0.StackBusyOffset];
        ret += select_unit(0, kv.index() + ioCfg.l0.StackBusyOffset);
        ret += generate(kv.value(), kv.index());
    }

    for (const auto &kv: ioCfg.l0.ioNIM | indexed(0))
    {
        ret += ioCfg.l0.unitNames[kv.index() + ioCfg.l0.NIM_IO_Offset];
        ret += select_unit(0, kv.index() + ioCfg.l0.NIM_IO_Offset);
        ret += generate(kv.value(), io_flags::NIM_IO_Flags);
    }

    //
    // Level1
    //

    ret += "Level1 ##############################";

    for (const auto &kv: ioCfg.l1.luts | indexed(0))
    {
        unsigned unitIndex = kv.index();
        ret += QString("L1.LUT%1").arg(unitIndex);
        ret += select_unit(1, unitIndex);
        ret += write_lut(kv.value());
    }

    //
    // Level2
    //

    ret += "Level2 ##############################";

    for (const auto &kv: ioCfg.l2.luts | indexed(0))
    {
        unsigned unitIndex = kv.index();
        ret += QString("L2.LUT%1").arg(unitIndex);
        ret += select_unit(2, unitIndex);
        ret += write_lut(kv.value());
        // TODO: move this into write_lut() and add a flag on whether the LUT
        // uses the strobe or not.
        // TODO: use a binary literal when writing out the strobes. it's a bit mask
        ret += write_unit_reg(0x20, kv.value().strobedOutputs.to_ulong(), "strobed_outputs");

        const auto l2InputChoices = make_level2_input_choices(unitIndex);

        for (size_t input = 0; input < Level2LUT_VariableInputCount; input++)
        {
            unsigned conValue = ioCfg.l2.lutConnections[unitIndex][input];
            UnitAddress conAddress = l2InputChoices.inputChoices[input][conValue];
            u16 regOffset = input * 2;

            ret += write_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
        }

        // strobe GG
        ret += QString("L2.LUT%1 strobe gate generator").arg(unitIndex);
        ret += generate(kv.value().strobeGG, io_flags::StrobeGG_Flags);

        // strobe_input
        {
            unsigned conValue = ioCfg.l2.strobeConnections[unitIndex];
            UnitAddress conAddress = l2InputChoices.strobeInputChoices[conValue];
            u16 regOffset = 6;

            ret += write_strobe_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
        }
    }

    //
    // Level3
    //

    ret += "Level3 ##############################";

    for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
    {
        unsigned unitIndex = kv.index();

        ret += ioCfg.l3.unitNames[unitIndex];
        ret += select_unit(3, unitIndex);
        ret += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.dynamicInputChoiceLists[unitIndex][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
    }

    for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.MasterTriggersOffset;

        ret += ioCfg.l3.unitNames[unitIndex];
        ret += select_unit(3, unitIndex);
        ret += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.dynamicInputChoiceLists[unitIndex][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
    }

    for (const auto &kv: ioCfg.l3.counters | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.CountersOffset;

        ret += ioCfg.l3.unitNames[unitIndex];
        ret += select_unit(3, unitIndex);
        ret += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.dynamicInputChoiceLists[unitIndex][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
    }

    // Level3 NIM connections
    ret += "NIM unit connections (Note: setup is done in the Level0 section)";
    for (size_t nim = 0; nim < trigger_io::NIM_IO_Count; nim++)
    {
        unsigned unitIndex = nim + ioCfg.l3.NIM_IO_Unit_Offset;

        ret += ioCfg.l3.unitNames[unitIndex];
        ret += select_unit(3, unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.dynamicInputChoiceLists[unitIndex][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));

    }

    for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.ECL_Unit_Offset;

        ret += ioCfg.l3.unitNames[unitIndex];
        ret += select_unit(3, unitIndex);
        ret += generate(kv.value(), io_flags::ECL_IO_Flags);

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.dynamicInputChoiceLists[unitIndex][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
    }

    return ret;
}

class ScriptGenPartVisitor: public boost::static_visitor<>
{
    public:
        ScriptGenPartVisitor(QStringList &lineBuffer)
            : m_lineBuffer(lineBuffer)
        { }

        void operator()(const Write &write)
        {
            QString prefix;
            int width = 6;
            int base = 10;
            char fill = ' ';

            if (write.options & Write::Opt_HexValue)
            {
                prefix = "0x";
                width = 4;
                base = 16;
                fill = '0';
            };

            auto line = QString("0x%1 %2%3")
                .arg(write.address, 4, 16, QLatin1Char('0'))
                .arg(prefix)
                .arg(write.value, width, base, QLatin1Char(fill));

            if (!write.comment.isEmpty())
                line += "    # " + write.comment;

            m_lineBuffer.push_back(line);
        }

        void operator() (const QString &blockComment)
        {
            if (!blockComment.isEmpty())
            {
                m_lineBuffer.push_back({});
                m_lineBuffer.push_back("# " + blockComment);
            }
        }

    private:
        QStringList &m_lineBuffer;
};

static QString generate_meta_block(
    const TriggerIOConfig &ioCfg,
    const gen_flags::Flag &flags)
{
    // unit number -> unit name
    using NameMap = std::map<unsigned, std::string>;

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "names" << YAML::Value << YAML::BeginMap;

    // Level0 - flat list of unitnames
    {
        NameMap m;

        for (const auto &kv: ioCfg.l0.DefaultUnitNames | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &defaultName = kv.value();
            const auto &unitName = ioCfg.l0.unitNames.value(unitIndex);

            if (defaultName == UnitNotAvailable)
                continue;

            if (unitName != defaultName ||
                (flags & gen_flags::MetaIncludeDefaultUnitNames))
            {
                m[unitIndex] = unitName.toStdString();
            }
        }

        if (!m.empty())
            out << YAML::Key << "level0" << YAML::Value << m;
    }

    // Level1 - per lut output names
    {
        // Map structure is  [lutIndex][outputIndex] = outputName

        std::map<unsigned, NameMap> lutMaps;

        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &lut = kv.value();

            NameMap m;

            for (const auto &kv2: lut.outputNames | indexed(0))
            {
                const auto &outputIndex = kv2.index();
                const auto &outputName = kv2.value();
                const auto DefaultOutputName = QString("L1.LUT%1.OUT%2")
                    .arg(unitIndex).arg(outputIndex);

                if (outputName != DefaultOutputName ||
                    (flags & gen_flags::MetaIncludeDefaultUnitNames))
                {
                    m[outputIndex] = outputName.toStdString();
                }
            }

            if (!m.empty())
                lutMaps[unitIndex] = m;
        }

        if (!lutMaps.empty())
            out << YAML::Key << "level1" << YAML::Value << lutMaps;
    }

    // Level2 - per lut output names
    {
        // Map structure is  [lutIndex][outputIndex] = outputName

        std::map<unsigned, NameMap> lutMaps;

        for (const auto &kv: ioCfg.l2.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &lut = kv.value();

            NameMap m;

            for (const auto &kv2: lut.outputNames | indexed(0))
            {
                const auto &outputIndex = kv2.index();
                const auto &outputName = kv2.value();
                const auto DefaultOutputName = QString("L2.LUT%1.OUT%2")
                    .arg(unitIndex).arg(outputIndex);

                if (outputName != DefaultOutputName ||
                    (flags & gen_flags::MetaIncludeDefaultUnitNames))
                {
                    m[outputIndex] = outputName.toStdString();
                }
            }

            if (!m.empty())
                lutMaps[unitIndex] = m;
        }

        if (!lutMaps.empty())
            out << YAML::Key << "level2" << YAML::Value << lutMaps;
    }

    // Level3 - flat list of unitnames
    {
        NameMap m;

        for (const auto &kv: ioCfg.l3.DefaultUnitNames | indexed(0))
        {
            const size_t unitIndex = kv.index();

            // Skip NIM I/Os as these are included in level0
            if (Level3::NIM_IO_Unit_Offset <= unitIndex
                && unitIndex < Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count)
            {
                continue;
            }

            const auto &defaultName = kv.value();
            const auto &unitName = ioCfg.l3.unitNames.value(unitIndex);

            if (defaultName == UnitNotAvailable)
                continue;

            if (unitName != defaultName ||
                (flags & gen_flags::MetaIncludeDefaultUnitNames))
            {
                m[unitIndex] = unitName.toStdString();
            }
        }

        if (!m.empty())
            out << YAML::Key << "level3" << YAML::Value << m;
    }

    out << YAML::EndMap;

    //out << rootMap;

    return QString(out.c_str());
}

static const u32 MVLC_VME_InterfaceAddress = 0xffff0000u;

/* First iteration: generate vme writes to setup all of the IO/trigger units
 * and the dynamic connections.
 * Later: come up with a format to write out the user set output names.
 * Also: create the reverse function which takes a list of VME writes and
 * recreates the corresponding TriggerIOConfig structure.
 */
QString generate_trigger_io_script_text(
    const TriggerIOConfig &ioCfg,
    const gen_flags::Flag &flags)
{
    QStringList lines =
    {
        "########################################",
        "#       MVLC Trigger I/O  Setup        #",
        "########################################",
        "",
    };

    lines.append(
    {
        vme_script::MetaBlockBegin + " " + vme_script::MetaTagMVLCTriggerIO,
        generate_meta_block(ioCfg, flags),
        vme_script::MetaBlockEnd
    });

    lines.append(
    {
        "",
        "# Internal MVLC VME interface address",
        QString("setbase 0x%1")
            .arg(MVLC_VME_InterfaceAddress, 8, 16, QLatin1Char('0'))
    });


    ScriptGenPartVisitor visitor(lines);
    auto parts = generate_trigger_io_script(ioCfg);

    for (const auto &part: parts)
    {
        boost::apply_visitor(visitor, part);
    }

    return lines.join("\n");
}

static const size_t LevelCount = 4;
static const u16 UnitSelectRegister = 0x200u;
static const u16 UnitRegisterBase = 0x300u;
static const u16 UnitConnectBase = 0x80u;
static const u16 UnitConnectMask = UnitConnectBase;

// Maps register address to register value
using RegisterWrites = QMap<u16, u16>;

// Holds per unit address register writes
using UnitWrites = QMap<u16, RegisterWrites>;

// Holds per level UnitWrites
using LevelWrites = std::array<UnitWrites, LevelCount>;

trigger_io::IO parse_io(const RegisterWrites &writes, const io_flags::Flags &ioFlags)
{
    trigger_io::IO io = {};

    u16 offset = (ioFlags & io_flags::StrobeGGOffsets) ? 0x32u : 0u;

    io.delay     = writes[offset + 0];
    io.width     = writes[offset + 2];
    io.holdoff   = writes[offset + 4];
    io.invert    = static_cast<bool>(writes[offset + 6]);

    io.direction = static_cast<trigger_io::IO::Direction>(writes[10]);
    io.activate  = static_cast<bool>(writes[16]);

    return io;
}

trigger_io::LUT_RAM parse_lut_ram(const RegisterWrites &writes)
{
    trigger_io::LUT_RAM ram = {};

    for (size_t line = 0; line < ram.size(); line++)
    {
        u16 regAddress = line * 2;
        ram[line] = writes[regAddress];
    }

    return ram;
}

LUT parse_lut(const RegisterWrites &writes)
{
    auto ram = parse_lut_ram(writes);

    LUT lut = {};

    for (size_t address = 0; address < lut.lutContents[0].size(); address++)
    {
        u8 ramValue = trigger_io::lookup(ram, address);

        // Distribute the 3 output bits stored in a single RAM cell to the 3
        // output arrays in lut.lutContents.
        for (unsigned output = 0; output < lut.lutContents.size(); output++)
        {
            lut.lutContents[output][address] = (ramValue >> output) & 0b1;
        }
    }

    lut.strobedOutputs = writes[0x20];
    lut.strobeGG = parse_io(writes, io_flags::StrobeGG_Flags);

    return lut;
}

TriggerIOConfig build_config_from_writes(const LevelWrites &levelWrites)
{
    TriggerIOConfig ioCfg;

    // level0
    {
        const auto &writes = levelWrites[0];

        for (const auto &kv: ioCfg.l0.timers | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();

            unit.range = static_cast<trigger_io::Timer::Range>(writes[unitIndex][2]);
            unit.delay_ns = writes[unitIndex][4];
            unit.period = writes[unitIndex][6];
        }

        for (const auto &kv: ioCfg.l0.irqUnits | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level0::IRQ_UnitOffset;
            auto &unit = kv.value();

            unit.irqIndex = writes[unitIndex][0];
        }

        for (const auto &kv: ioCfg.l0.slaveTriggers | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level0::SlaveTriggerOffset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex], io_flags::None);
        }

        for (const auto &kv: ioCfg.l0.stackBusy | indexed(0))
        {

            unsigned unitIndex = kv.index() + Level0::StackBusyOffset;
            auto &unit = kv.value();

            unit.stackIndex = writes[unitIndex][0];
        }

        for (const auto &kv: ioCfg.l0.ioNIM | indexed(0))
        {

            unsigned unitIndex = kv.index() + Level0::NIM_IO_Offset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex], io_flags::NIM_IO_Flags);
        }
    }

    // level1
    {
        const auto &writes = levelWrites[1];

        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();

            auto parsed = parse_lut(writes[unitIndex]);
            // FIXME: hack to keep the original output names. can this be done
            // in a cleaner way?
            parsed.outputNames = unit.outputNames;
            unit = parsed;
        }
    }

    // level2
    {
        const auto &writes = levelWrites[2];

        for (const auto &kv: ioCfg.l2.luts | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();

            // This parses the LUT and the strobe GG settings
            auto parsed = parse_lut(writes[unitIndex]);
            // FIXME: hack to keep the original output names. can this be done
            // in a cleaner way?
            parsed.outputNames = unit.outputNames;
            unit = parsed;

            // dynamic input connections
            for (size_t input = 0; input < Level2LUT_VariableInputCount; ++input)
            {
                ioCfg.l2.lutConnections[unitIndex][input] =
                    writes[unitIndex][UnitConnectBase + 2 * input];
            }

            // strobe GG connection
            ioCfg.l2.strobeConnections[unitIndex] = writes[unitIndex][UnitConnectBase + 6];
        }
    }

    // level3
    {
        const auto &writes = levelWrites[3];

        for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();

            unit.activate = static_cast<bool>(writes[unitIndex][0]);
            unit.stackIndex = writes[unitIndex][2];
        }

        for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::MasterTriggersOffset;
            auto &unit = kv.value();

            unit.activate = static_cast<bool>(writes[unitIndex][0]);
        }

        for (const auto &kv: ioCfg.l3.counters | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::CountersOffset;
            auto &unit = kv.value();

            ioCfg.l3.connections[unitIndex] = writes[unitIndex][0x80];
        }

        for (const auto &kv: ioCfg.l3.ioNIM | indexed(0))
        {
            // level3 NIM connections (setup is done in level0)
            unsigned unitIndex = kv.index() + Level3::NIM_IO_Unit_Offset;

            ioCfg.l3.connections[unitIndex] = writes[unitIndex][0x80];
        }

        for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::ECL_Unit_Offset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex], io_flags::ECL_IO_Flags);

            ioCfg.l3.connections[unitIndex] = writes[unitIndex][0x80];
        }
    }

    return ioCfg;
}

TriggerIOConfig parse_trigger_io_script_text(const QString &text)
{
    auto commands = vme_script::parse(text);

    LevelWrites levelWrites;

    u16 level = 0;
    u16 unit  = 0;

    for (const auto &cmd: commands)
    {
        if (!(cmd.type == vme_script::CommandType::Write))
            continue;

        u32 address = cmd.address;

        // Clear the uppper 16 bits of the 32 bit address value. In the
        // generated script these are set by the setbase command on the very first line.
        address &= ~MVLC_VME_InterfaceAddress;

        if (address == UnitSelectRegister)
        {
            level = (cmd.value >> 8) & 0b11;
            unit  = (cmd.value & 0xff);
        }
        else
        {
            // Store all other writes in the map structure under the current
            // level and unit. Also subtract the UnitRegisterBase from writes
            // value.
            address -= UnitRegisterBase;
            levelWrites[level][unit][address] = cmd.value;
        }
    }

    return build_config_from_writes(levelWrites);
}

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config
