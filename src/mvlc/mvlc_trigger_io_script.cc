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
#include "mvlc/mvlc_trigger_io_script.h"

#include <boost/range/adaptor/indexed.hpp>
#include <boost/variant.hpp>
#include <QDebug>
#include <QMap>
#include <QVector>
#include <yaml-cpp/yaml.h>

#include "template_system.h"
#include "vme_script.h"

using boost::adaptors::indexed;

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

static const u16 StrobeGGIOOffset = 0x32u;

struct Write
{
    // Opt_HexValue indicates that the register value should be printed in
    // hexadecimal instead of decimal.
    static const unsigned Opt_HexValue = 1u << 0;;

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

// Basic part of the script: either a register write or a block comment.
using BasicPart = boost::variant<Write, QString>;
using BasicParts = QVector<BasicPart>;

// Represents a single DSO unit in the script. In the output script this is
// enclosed mvlc_stack_begin/end so that the unit setup is atomic (happening in
// a single stack transaction).
struct UnitBlock
{
    QString comment;
    QVector<BasicPart> parts;

    void operator+=(const BasicPart &part)
    {
        parts.push_back(part);
    }

    void operator+=(const BasicParts &parts_)
    {
        parts.append(parts_);
    }
};

// Top level parts of the script: basic parts or unit blocks.
using ScriptPart = boost::variant<Write, QString, UnitBlock>;
using ScriptParts = QVector<ScriptPart>;

BasicPart select_unit(int level, int unit, const QString &unitName = {})
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
BasicPart write_unit_reg(u16 reg, u16 value, const QString &comment, unsigned writeOpts = 0u)
{
    auto ret = Write { static_cast<u16>(0x0300u + reg), value, comment, writeOpts };

    return ret;
}

BasicPart write_unit_reg(u16 reg, u16 value, unsigned writeOpts = 0u)
{
    return write_unit_reg(reg, value, {}, writeOpts);
}

// Note: the desired unit must be selected prior to calling this function.
BasicPart write_connection(u16 offset, u16 value, const QString &sourceName = {})
{
    auto ret = Write { static_cast<u16>(0x0380u + offset), value };

    if (!sourceName.isEmpty())
        ret.comment = QString("connect input%1 to '%2'")
            .arg(offset / 2).arg(sourceName);

    return ret;
}

BasicPart write_strobe_connection(u16 offset, u16 value, const QString &sourceName = {})
{
    auto ret = Write { static_cast<u16>(0x0380u + offset), value };

    if (!sourceName.isEmpty())
        ret.comment = QString("connect strobe_input to '%1'").arg(sourceName);

    return ret;
}

BasicParts generate(const trigger_io::Timer &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret += write_unit_reg(2, static_cast<u16>(unit.range), "range (0:ns, 1:us, 2:ms, 3:s)");
    ret += write_unit_reg(4, unit.delay_ns, "delay [ns]");
    ret += write_unit_reg(6, unit.period, "period [in range units]");
    return ret;
}

namespace io_flags
{
    using Flags = u8;
    static const Flags HasDirection    = 1u << 0;
    static const Flags HasActivation   = 1u << 1;

    static const Flags None             = 0u;
    static const Flags NIM_IO_Flags     = HasDirection | HasActivation;
    static const Flags ECL_IO_Flags     = HasActivation;
}

/* The IO structure is used for different units sharing IO properties:
 * NIM I/Os, ECL Outputs, slave triggers, and strobe gate generators.
 * The activation and direction registers are only written out if the
 * respective io_flags bit is set. The offset value is added to all base
 * register addresses.
 */
BasicParts generate(const trigger_io::IO &io, const io_flags::Flags &ioFlags, u16 offset = 0)
{
    BasicParts ret;

    ret += write_unit_reg(offset + 0, io.delay, "delay [ns]");
    ret += write_unit_reg(offset + 2, io.width, "width [ns]");
    ret += write_unit_reg(offset + 4, io.holdoff, "holdoff [ns]");
    ret += write_unit_reg(offset + 6, static_cast<u16>(io.invert),
                          "invert (start on trailing edge of input)");

    if (ioFlags & io_flags::HasDirection)
        ret += write_unit_reg(offset + 10, static_cast<u16>(io.direction), "direction (0:in, 1:out)");

    if (ioFlags & io_flags::HasActivation)
        ret += write_unit_reg(offset + 16, static_cast<u16>(io.activate), "output activate");

    return ret;
}

BasicParts generate(const trigger_io::TriggerResource &unit, int /*index*/)
{
    BasicParts ret;

    ret += write_unit_reg(0x80u, static_cast<u16>(unit.type),
                          "type: 0=IRQ, 1=SoftTrigger, 2=SlaveTrigger");

    ret += write_unit_reg(0, static_cast<u16>(unit.irqUtil.irqIndex),
                          "irq_index (zero-based: 0: IRQ1, .., 6: IRQ7)");

    auto ggParts = generate(unit.slaveTrigger.gateGenerator, io_flags::None, 6);

    for (auto &part: ggParts)
    {
        if (auto write = boost::get<Write>(&part))
        {
            write->comment = "slave_trigger: " + write->comment;
        }
    }

    ret += ggParts;

    ret += write_unit_reg(0x82u, unit.slaveTrigger.triggerIndex,
                          "slave trigger number (0..3)");

    return ret;
}

BasicParts generate(const trigger_io::StackBusy &unit, int index)
{
    (void) index;

    BasicParts ret;
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

BasicParts write_lut_ram(const trigger_io::LUT_RAM &ram)
{
    BasicParts ret;

    for (const auto &kv: ram | indexed(0))
    {
        u16 reg = kv.index() * sizeof(u16); // register address increment is 2 bytes
        u16 cell = reg * 2;
        auto comment = QString("cells %1-%2").arg(cell).arg(cell + 3);
        ret += write_unit_reg(reg, kv.value(), comment, Write::Opt_HexValue);
    }

    return ret;
}

BasicParts write_lut(const LUT &lut)
{
    return write_lut_ram(make_lut_ram(lut));
}

BasicParts generate(const trigger_io::StackStart &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret += write_unit_reg(0, static_cast<u16>(unit.activate), "activate");
    ret += write_unit_reg(2, unit.stackIndex, "stack index");
    ret += write_unit_reg(4, unit.delay_ns, "delay [ns]");
    return ret;
}

BasicParts generate(const trigger_io::MasterTrigger &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret += write_unit_reg(0, static_cast<u16>(unit.activate), "activate");
    return ret;
}

BasicParts generate(const trigger_io::Counter &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret += write_unit_reg(14, static_cast<u16>(unit.clearOnLatch), "clear on latch");
    return ret;
}

ScriptParts generate_trigger_io_script(const TriggerIO &ioCfg)
{
    ScriptParts ret;

    //
    // Level0
    //

    ret += "Level0 #####################################################";

    for (const auto &kv: ioCfg.l0.timers | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index()];
        ub += select_unit(0, kv.index());
        ub += generate(kv.value(), kv.index());
        ret += ub;
    }

    for (const auto &kv: ioCfg.l0.triggerResources | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.TriggerResourceOffset];
        ub += select_unit(0, kv.index() + ioCfg.l0.TriggerResourceOffset);
        ub += generate(kv.value(), kv.index());
        ret += ub;
    }

    for (const auto &kv: ioCfg.l0.stackBusy | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.StackBusyOffset];
        ub += select_unit(0, kv.index() + ioCfg.l0.StackBusyOffset);
        ub += generate(kv.value(), kv.index());
        ret += ub;
    }

    for (const auto &kv: ioCfg.l0.ioNIM | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.NIM_IO_Offset];
        ub += select_unit(0, kv.index() + ioCfg.l0.NIM_IO_Offset);
        ub += generate(kv.value(), io_flags::NIM_IO_Flags);
        ret += ub;
    }

    for (const auto &kv: ioCfg.l0.ioIRQ | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.IRQ_Inputs_Offset];
        ub += select_unit(0, kv.index() + ioCfg.l0.IRQ_Inputs_Offset);
        ub += generate(kv.value(), io_flags::None);
        ret += ub;
    }

    //
    // Level1
    //

    ret += "Level1 #####################################################";

    for (const auto &kv: ioCfg.l1.luts | indexed(0))
    {
        unsigned unitIndex = kv.index();
        UnitBlock ub;
        ub.comment = QString("L1.LUT%1").arg(unitIndex);
        ub += select_unit(1, unitIndex);
        ub += write_lut(kv.value());

        if (unitIndex == 2)
        {
            const auto &inputChoices = Level1::LUT2DynamicInputChoices;

            for (size_t input = 0; input < LUT_DynamicInputCount; ++input)
            {
                unsigned conValue = ioCfg.l1.lut2Connections[input];
                UnitAddress conAddress = inputChoices[input][conValue];
                u16 regOffset = input * 2;

                ub += write_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
            }
        }

        ret += ub;
    }

    //
    // Level2
    //

    ret += "Level2 #####################################################";

    for (const auto &kv: ioCfg.l2.luts | indexed(0))
    {
        unsigned unitIndex = kv.index();
        UnitBlock ub;
        ub.comment = QString("L2.LUT%1").arg(unitIndex);
        ub += select_unit(2, unitIndex);
        ub += write_lut(kv.value());
        ub += write_unit_reg(0x20, kv.value().strobedOutputs.to_ulong(),
                              "strobed_outputs", Write::Opt_BinValue);

        const auto &l2InputChoices = Level2::DynamicInputChoices[unitIndex];

        for (size_t input = 0; input < LUT_DynamicInputCount; input++)
        {
            unsigned conValue = ioCfg.l2.lutConnections[unitIndex][input];
            UnitAddress conAddress = l2InputChoices.lutChoices[input][conValue];
            u16 regOffset = input * 2;

            ub += write_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
        }

        // strobe GG
        ub += QString("L2.LUT%1 strobe gate generator").arg(unitIndex);
        ub += generate(kv.value().strobeGG, io_flags::None, StrobeGGIOOffset);

        // strobe_input
        {
            unsigned conValue = ioCfg.l2.strobeConnections[unitIndex];
            UnitAddress conAddress = l2InputChoices.strobeChoices[conValue];
            u16 regOffset = 6;

            ub += write_strobe_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
        }

        ret += ub;
    }

    //
    // Level3
    //

    ret += "Level3 #####################################################";

    for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
    {
        unsigned unitIndex = kv.index();

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);
        ub += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
        ret += ub;
    }

    for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.MasterTriggersOffset;

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);
        ub += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
        ret += ub;
    }

    for (const auto &kv: ioCfg.l3.counters | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.CountersOffset;

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);
        ub += generate(kv.value(), unitIndex);

        // counter input
        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));

        // latch input
        conValue = ioCfg.l3.connections[unitIndex][1];
        conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][1][conValue];

        ub += write_connection(2, conValue, lookup_name(ioCfg, conAddress));
        ret += ub;
    }

    // Level3 NIM connections
    ret += "NIM unit connections (Note: NIM setup is done in the Level0 section)";
    for (size_t nim = 0; nim < trigger_io::NIM_IO_Count; nim++)
    {
        unsigned unitIndex = nim + ioCfg.l3.NIM_IO_Unit_Offset;

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
        ret += ub;
    }

    for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.ECL_Unit_Offset;

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);
        ub += generate(kv.value(), io_flags::ECL_IO_Flags);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
        ret += ub;
    }

    return ret;
}

class ScriptGenPartVisitor: public boost::static_visitor<>
{
    public:
        explicit ScriptGenPartVisitor(QStringList &lineBuffer, const gen_flags::Flag &flags)
            : m_lineBuffer(lineBuffer)
            , m_flags(flags)
        { }

        QString make_line(const Write &write)
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
            }
            else if (write.options & Write::Opt_BinValue)
            {
                prefix = "0b";
                width = 4;
                base = 2;
                fill ='0';
            }

            auto line = QString("0x%1 %2%3")
                .arg(write.address, 4, 16, QLatin1Char('0'))
                .arg(prefix)
                .arg(write.value, width, base, QLatin1Char(fill));

            if (!write.comment.isEmpty())
                line += "    # " + write.comment;

            return line;
        }

        void operator()(const Write &write)
        {
            m_lineBuffer.push_back(make_line(write));
        }

        void operator() (const QString &blockComment)
        {
            if (!blockComment.isEmpty())
            {
                m_lineBuffer.push_back({});
                m_lineBuffer.push_back("# " + blockComment);
            }
        }

        void operator()(const UnitBlock &ub)
        {
            m_lineBuffer.push_back({});
            m_lineBuffer.push_back(QSL("# %1").arg(ub.comment));

            if (m_flags & gen_flags::GroupIntoStackTransaction)
                m_lineBuffer.push_back("mvlc_stack_begin");

            for (const auto &unitPart: ub.parts)
            {
                if (auto write = boost::get<Write>(&unitPart))
                {
                    auto line = make_line(*write);
                    if (m_flags & gen_flags::GroupIntoStackTransaction)
                        line = QSL("  %1").arg(line);
                    m_lineBuffer.push_back(line);
                }
                else if (auto comment = boost::get<QString>(&unitPart))
                {
                    auto line = QSL("# %1").arg(*comment);
                    if (m_flags & gen_flags::GroupIntoStackTransaction)
                        line = QSL("  %1").arg(line);
                    m_lineBuffer.push_back(line);
                }
            }

            if (m_flags & gen_flags::GroupIntoStackTransaction)
                m_lineBuffer.push_back("mvlc_stack_end");
        }

    private:
        QStringList &m_lineBuffer;
        const gen_flags::Flag m_flags;
};

static QString generate_mvlc_meta_block(
    const TriggerIO &ioCfg,
    const gen_flags::Flag &flags)
{
    // unit number -> unit name
    using NameMap = std::map<unsigned, std::string>;

    YAML::Emitter out;
    assert(out.good()); // initial consistency check
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
    assert(out.good()); // consistency check

    //
    // Additional settings stored in the meta block, e.g. soft activate flags.
    //

    out << YAML::Key << "settings" << YAML::Value << YAML::BeginMap;

    {
        out << YAML::Key << "level0" << YAML::Value << YAML::BeginMap;

        for (const auto &kv: ioCfg.l0.timers | indexed(0))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            out << YAML::Key << unitIndex << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "soft_activate" << YAML::Value << unit.softActivate;
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
        assert(out.good()); // consistency check
    }

    {
        out << YAML::Key << "level3" << YAML::Value << YAML::BeginMap;

        for (const auto &kv: ioCfg.l3.counters | indexed(ioCfg.l3.CountersOffset))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            out << YAML::Key << unitIndex << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "soft_activate" << YAML::Value << unit.softActivate;
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
        assert(out.good()); // consistency check
    }

    out << YAML::EndMap;
    assert(out.good()); // consistency check

    return QString(out.c_str());
}

static const u32 MVLC_VME_InterfaceAddress = 0xffff0000u;
static const char *TriggerIoMvmeMinVersion = "1.9";

QString generate_trigger_io_script_text(
    const TriggerIO &ioCfg,
    const gen_flags::Flag &flags)
{
    QStringList lines =
    {
        "##############################################################",
        "# MVLC Trigger I/O  setup via internal VME interface         #",
        "##############################################################",
        "",
        "# Note: This file was generated by mvme. Editing existing",
        "# values is allowed and these changes will used by mvme",
        "# when next parsing the script. Changes to the basic",
        "# structure, like adding new write or read commands, are not",
        "# allowed. These changes will be lost the next time the file",
        "# is modified by mvme.",
        "",
        "# Check for minimum required mvme version",
        QString("mvme_require_version %1").arg(TriggerIoMvmeMinVersion),
        "",
        "# Internal MVLC VME interface address",
        QString("setbase 0x%1").arg(MVLC_VME_InterfaceAddress, 8, 16, QLatin1Char('0'))
    };

    ScriptGenPartVisitor visitor(lines, flags);
    auto parts = generate_trigger_io_script(ioCfg);

    for (const auto &part: parts)
    {
        boost::apply_visitor(visitor, part);
    }

    lines.append(
    {
        "",
        "##############################################################",
        "# MVLC Trigger I/O specific meta information                 #",
        "##############################################################",
        vme_script::MetaBlockBegin + " " + MetaTagMVLCTriggerIO,
        generate_mvlc_meta_block(ioCfg, flags),
        vme_script::MetaBlockEnd
    });

    return lines.join("\n");
}

static const size_t LevelCount = 4;
static const u16 UnitSelectRegister = 0x200u;
static const u16 UnitRegisterBase = 0x300u;
static const u16 UnitConnectBase = 0x80u;
//static const u16 UnitConnectMask = UnitConnectBase;

// Maps register address to register value
using RegisterWrites = QMap<u16, u16>;

// Holds per unit address register writes
using UnitWrites = QMap<u16, RegisterWrites>;

// Holds per level UnitWrites
using LevelWrites = std::array<UnitWrites, LevelCount>;

trigger_io::IO parse_io(const RegisterWrites &writes, u16 offset = 0)
{
    trigger_io::IO io = {};

    io.delay     = writes[offset + 0];
    io.width     = writes[offset + 2];
    io.holdoff   = writes[offset + 4];
    io.invert    = static_cast<bool>(writes[offset + 6]);

    io.direction = static_cast<trigger_io::IO::Direction>(writes[offset+10]);
    io.activate  = static_cast<bool>(writes[offset+16]);

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

LUT parse_lut(
    const RegisterWrites &writes,
    const std::array<QString, LUT::OutputBits> &outputNames,
    const std::array<QString, LUT::OutputBits> &defaultOutputNames)
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
    lut.strobeGG = parse_io(writes, StrobeGGIOOffset);

    // Force the width to 8 if it was set to 0 or not specified.
    if (lut.strobeGG.width == 0)
        lut.strobeGG.width = LUT::StrobeGGDefaultWidth;

    std::copy(outputNames.begin(), outputNames.end(),
              lut.outputNames.begin());

    std::copy(defaultOutputNames.begin(), defaultOutputNames.end(),
              lut.defaultOutputNames.begin());

    return lut;
}

void parse_mvlc_meta_block(const vme_script::MetaBlock &meta, TriggerIO &ioCfg)
{
    auto y_to_qstr = [](const YAML::Node &y) -> QString
    {
        assert(y);
        return QString::fromStdString(y.as<std::string>());
    };

    assert(meta.tag() == MetaTagMVLCTriggerIO);

    YAML::Node yRoot = YAML::Load(meta.textContents.toStdString());

    if (!yRoot || !yRoot["names"]) return;

    //
    // Names
    //
    const auto &yLevelNames = yRoot["names"];

    // Level0 - flat list of unitnames
    if (const auto &yNames = yLevelNames["level0"])
    {
        for (const auto &kv: ioCfg.l0.unitNames | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &unitName = kv.value();

            if (yNames[unitIndex])
                unitName = y_to_qstr(yNames[unitIndex]);

            // Copy NIM_IO names to the level3 structure.
            if (Level3::NIM_IO_Unit_Offset <= static_cast<unsigned>(unitIndex)
                && static_cast<unsigned>(unitIndex) < (Level3::NIM_IO_Unit_Offset +
                                                       trigger_io::NIM_IO_Count))
            {
                ioCfg.l3.unitNames[unitIndex] = unitName;
            }
        }
    }

    // Level1 - per lut output names
    if (const auto &yUnitMaps = yLevelNames["level1"])
    {
        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &lut = kv.value();

            if (const auto &yNames = yUnitMaps[unitIndex])
            {
                for (const auto &kv2: lut.outputNames | indexed(0))
                {
                    const auto &outputIndex = kv2.index();
                    auto &outputName = kv2.value();

                    if (yNames[outputIndex])
                        outputName = y_to_qstr(yNames[outputIndex]);
                }
            }
        }
    }

    // Level2 - per lut output names
    if (const auto &yUnitMaps = yLevelNames["level2"])
    {
        for (const auto &kv: ioCfg.l2.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &lut = kv.value();

            if (const auto &yNames = yUnitMaps[unitIndex])
            {
                for (const auto &kv2: lut.outputNames | indexed(0))
                {
                    const auto &outputIndex = kv2.index();
                    auto &outputName = kv2.value();

                    if (yNames[outputIndex])
                        outputName = y_to_qstr(yNames[outputIndex]);
                }
            }
        }
    }

    // Level3 - flat list of unitnames
    if (const auto &yNames = yLevelNames["level3"])
    {
        for (const auto &kv: ioCfg.l3.unitNames | indexed(0))
        {
            const size_t &unitIndex = kv.index();
            auto &unitName = kv.value();

            // Skip NIM I/Os as these are included in level0
            if (Level3::NIM_IO_Unit_Offset <= unitIndex
                && unitIndex < Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count)
            {
                continue;
            }

            if (yNames[unitIndex])
                unitName = y_to_qstr(yNames[unitIndex]);
        }
    }

    //
    // Additional settings stored in the meta block, e.g. soft activate flags.
    //

    if (!yRoot["settings"]) return;

    const auto &ySettings = yRoot["settings"];

    if (const auto &yLevelSettings = ySettings["level0"])
    {
        for (const auto &kv: ioCfg.l0.timers | indexed(0))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            try
            {
                unit.softActivate = yLevelSettings[unitIndex]["soft_activate"].as<bool>();
            }
            catch (const std::runtime_error &)
            {}
        }
    }

    if (const auto &yLevelSettings = ySettings["level3"])
    {
        for (const auto &kv: ioCfg.l3.counters | indexed(ioCfg.l3.CountersOffset))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();
            try
            {
                unit.softActivate = yLevelSettings[unitIndex]["soft_activate"].as<bool>();
            }
            catch (const std::runtime_error &)
            {}
        }
    }
}

TriggerIO build_config_from_writes(const LevelWrites &levelWrites)
{
    TriggerIO ioCfg;

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

        for (const auto &kv: ioCfg.l0.triggerResources | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level0::TriggerResourceOffset;
            auto &unit = kv.value();

            unit.type = static_cast<TriggerResource::Type>(writes[unitIndex][0x80u]);
            unit.irqUtil.irqIndex = writes[unitIndex][0];
            unit.slaveTrigger.gateGenerator = parse_io(writes[unitIndex], 6);
            unit.slaveTrigger.triggerIndex = writes[unitIndex][0x82u];
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

            unit = parse_io(writes[unitIndex]);
        }

        for (const auto &kv: ioCfg.l0.ioIRQ | indexed(0))
        {

            unsigned unitIndex = kv.index() + Level0::IRQ_Inputs_Offset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex]);
        }
    }

    // level1
    {
        const auto &writes = levelWrites[1];

        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();
            unit = parse_lut(writes[unitIndex], unit.outputNames, unit.defaultOutputNames);

            if (unitIndex == 2)
            {
                // dynamic input connections
                for (size_t input = 0; input < LUT_DynamicInputCount; ++input)
                {
                    ioCfg.l1.lut2Connections[input] =
                        writes[unitIndex][UnitConnectBase + 2 * input];
                }
            }
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
            unit = parse_lut(writes[unitIndex], unit.outputNames, unit.defaultOutputNames);

            // dynamic input connections
            for (size_t input = 0; input < LUT_DynamicInputCount; ++input)
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
        // Copy NIM settings parsed from level0 data to the level3 NIM
        // structures.
        std::copy(ioCfg.l0.ioNIM.begin(),
                  ioCfg.l0.ioNIM.end(),
                  ioCfg.l3.ioNIM.begin());


        const auto &writes = levelWrites[3];

        for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();

            unit.activate = static_cast<bool>(writes[unitIndex][0]);
            unit.stackIndex = writes[unitIndex][2];
            unit.delay_ns = writes[unitIndex][4];

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80] };
        }

        for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::MasterTriggersOffset;
            auto &unit = kv.value();

            unit.activate = static_cast<bool>(writes[unitIndex][0]);

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80] };
        }

        for (const auto &kv: ioCfg.l3.counters | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::CountersOffset;
            auto &unit = kv.value();

            unit.clearOnLatch = static_cast<bool>(writes[unitIndex][14]);

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80], writes[unitIndex][0x82] };
        }

        for (const auto &kv: ioCfg.l3.ioNIM | indexed(0))
        {
            // level3 NIM connections (setup is done in level0)
            unsigned unitIndex = kv.index() + Level3::NIM_IO_Unit_Offset;

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80] };
        }

        for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::ECL_Unit_Offset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex]);

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80] };
        }
    }

    return ioCfg;
}

TriggerIO parse_trigger_io_script_text(const QString &text)
{
    auto commands = vme_script::parse(text);

    LevelWrites levelWrites;

    u16 level = 0;
    u16 unit  = 0;

    auto handle_command = [&] (const vme_script::Command &cmd)
    {
        assert(cmd.type == vme_script::CommandType::Write);

        u32 address = cmd.address;

        // Clear the uppper 16 bits of the 32 bit address value. In the
        // generated script these are set by the setbase command on the very first line.
        address &= ~MVLC_VME_InterfaceAddress;

        if (address == UnitSelectRegister)
        {
            level = (cmd.value >> 8) & 0b11;
            unit  = (cmd.value & 0xff);
        }
        else if (level < levelWrites.size())
        {
            // Store all other writes in the map structure under the current
            // level and unit. Also subtract the UnitRegisterBase from the
            // write address to get the plain register address.
            address -= UnitRegisterBase;
            levelWrites[level][unit][address] = cmd.value;
        }
    };

    for (const auto &cmd: commands)
    {
        if (cmd.type == vme_script::CommandType::MVLC_InlineStack)
        {
            for (auto innerCmd: cmd.mvlcInlineStack)
                handle_command(*innerCmd);
        }
        else if (cmd.type == vme_script::CommandType::Write)
        {
            handle_command(cmd);
        }
    }

    auto ioCfg = build_config_from_writes(levelWrites);

    // meta block handling
    auto metaCmd = get_first_meta_block(commands);

    if (metaCmd.metaBlock.tag() == MetaTagMVLCTriggerIO)
        parse_mvlc_meta_block(metaCmd.metaBlock, ioCfg);

    return ioCfg;
}

TriggerIO load_default_trigger_io()
{
    auto scriptContents = vats::read_default_mvlc_trigger_io_script().contents;
    return parse_trigger_io_script_text(scriptContents);
}

} // end namespace mvme_mvlc
} // end namespace mesytec
} // end namespace trigger_io
