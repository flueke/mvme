#include "mvlc/mvlc_trigger_io.h"
#include <boost/range/adaptor/indexed.hpp>

using boost::adaptors::indexed;

namespace mesytec
{
namespace mvlc
{
namespace trigger_io
{

#if 0
const UnitAddress UnitAddress::Invalid = UnitAddress{-1, -1, -1};
const UnitAddress UnitAddress::Dynamic = UnitAddress{-2, -2, -2};
#endif

LUT::LUT()
{
    lutContents.fill({});
    outputNames.fill({});
}

//
// Level0
//

// Note: ECL units not included here. They are only on level3.
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

//
// Level1
//
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
    luts.fill({});

    for (size_t unit = 0; unit < luts.size(); unit++)
    {
        for (size_t output = 0; output < luts[unit].outputNames.size(); output++)
        {
            luts[unit].defaultOutputNames[output] =
                QString("L1.LUT%1.OUT%2").arg(unit).arg(output);

            luts[unit].outputNames[output] = luts[unit].defaultOutputNames[output];
        }
    }
}

//
// Level2
//
namespace
{
std::array<Level2::LUTDynamicInputChoices, Level2::LUTCount> make_l2_input_choices()
{
    std::array<Level2::LUTDynamicInputChoices, Level2::LUTCount> result = {};

    for (size_t unit = 0; unit < result.size(); unit++)
    {

        // Common to all inputs: can connect to all Level0 utility outputs.
        std::vector<UnitAddress> common(trigger_io::Level0::UtilityUnitCount);

        for (unsigned i = 0; i < common.size(); i++)
            common[i] = { 0, i };

        result[unit].lutChoices = { common, common, common };
        result[unit].strobeChoices = common;

        if (unit == 0)
        {
            // L2.LUT0 can connect to L1.LUT4
            for (unsigned i = 0; i < Level2::LUT_DynamicInputCount; i++)
                result[unit].lutChoices[i].push_back(UnitAddress{ 1, 4, i });
        }
        else if (unit == 1)
        {
            // L2.LUT1 can connecto to L1.LUT3
            for (unsigned i = 0; i < Level2::LUT_DynamicInputCount; i++)
                result[unit].lutChoices[i].push_back(UnitAddress{ 1, 3, i });
        }

        // Strobe inputs can connect to all 6 level 1 outputs
        for (unsigned i = 0; i < 3; i++)
            result[unit].strobeChoices.push_back({ 1, 3, i });

        for (unsigned i = 0; i < 3; i++)
            result[unit].strobeChoices.push_back({ 1, 4, i });
    }

    return result;
}
} // end anon namespace

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


const std::array<Level2::LUTDynamicInputChoices, Level2::LUTCount>
    Level2::DynamicInputChoices = make_l2_input_choices();

Level2::Level2()
{
    for (size_t unit = 0; unit < luts.size(); unit++)
    {
        for (size_t output = 0; output < luts[unit].outputNames.size(); output++)
        {
            luts[unit].defaultOutputNames[output] =
                QString("L2.LUT%1.OUT%2").arg(unit).arg(output);

            luts[unit].outputNames[output] = luts[unit].defaultOutputNames[output];
        }
    }

    lutConnections.fill({});
    strobeConnections.fill({});
}

//
// Level3
//
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
    "Counter4",
    "Counter5",
    "Counter6",
    "Counter7",
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

namespace
{
std::vector<UnitAddressVector> make_l3_input_choices()
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

    // NIM and ECL outputs can connect to Level2 only
    for (size_t i = 0; i < (trigger_io::NIM_IO_Count + trigger_io::ECL_OUT_Count); i++)
    {
        std::vector<UnitAddress> choices = Level2Full;
        result.emplace_back(choices);
    }

    return result;
}
} // end anon namespace

const std::vector<UnitAddressVector>
    Level3::DynamicInputChoiceLists = make_l3_input_choices();

Level3::Level3()
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

//
//
//
QString lookup_name(const TriggerIO &cfg, const UnitAddress &addr)
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

#if 0
QString lookup_name_2(const TriggerIO &cfg, const UnitAddress &addr)
{
    try
    {
        // FIXME: use the pin address value -1 to indicate that a unit name is
        // being looked up
        //if (addr[2] < 0)
        //    return cfg.unitNames.at(addr[0]).at(addr[1]);
        return cfg.pinNames.at(addr[0]).at(addr[1]).at(addr[2]);
        //return cfg.unitNames.at(addr.level()).at(addr.unit()).at(addr.pin());
    }
    catch (const std::out_of_range &)
    { }

    return {};
}
#endif

QString lookup_default_name(const TriggerIO &cfg, const UnitAddress &addr)
{
    switch (addr[0])
    {
        case 0:
            if (addr[1] < cfg.l0.DefaultUnitNames.size())
                return cfg.l0.DefaultUnitNames[addr[1]];
            break;

        case 1:
            return cfg.l1.luts[addr[1]].defaultOutputNames[addr[2]];

        case 2:
            return cfg.l2.luts[addr[1]].defaultOutputNames[addr[2]];

        case 3:
            if (addr[1] < cfg.l3.DefaultUnitNames.size())
                return cfg.l3.DefaultUnitNames[addr[1]];
            break;
    }

    return {};
}

void reset_names(TriggerIO &ioCfg)
{
    // l0
    ioCfg.l0.unitNames.clear();
    std::copy(ioCfg.l0.DefaultUnitNames.begin(),
              ioCfg.l0.DefaultUnitNames.end(),
              std::back_inserter(ioCfg.l0.unitNames));

    // l1
    for (const auto &kv: ioCfg.l1.luts | indexed(0))
    {
        auto &lut = kv.value();

        std::copy(lut.defaultOutputNames.begin(), lut.defaultOutputNames.end(),
                  lut.outputNames.begin());
    }

    // l2
    for (const auto &kv: ioCfg.l2.luts | indexed(0))
    {
        auto &lut = kv.value();

        std::copy(lut.defaultOutputNames.begin(), lut.defaultOutputNames.end(),
                  lut.outputNames.begin());
    }

    // l3
    ioCfg.l3.unitNames.clear();
    std::copy(ioCfg.l3.DefaultUnitNames.begin(),
              ioCfg.l3.DefaultUnitNames.end(),
              std::back_inserter(ioCfg.l3.unitNames));
}

unsigned get_connection_value(const TriggerIO &ioCfg, const UnitAddress &addr)
{
    switch (addr[0])
    {
        case 0:
        case 1:
            return 0;

        case 2:
            if (addr[2] == LUT::InputBits)
                return ioCfg.l2.strobeConnections[addr[1]];
            return ioCfg.l2.lutConnections[addr[1]][addr[2]];

        case 3:
            return ioCfg.l3.connections[addr[1]];
    }

    return 0;
}

UnitAddress get_connection_unit_address(const TriggerIO &ioCfg, const UnitAddress &addr)
{
    unsigned conValue = get_connection_value(ioCfg, addr);

    switch (addr[0])
    {
        case 0:
        case 1:
            return {};

        case 2:
            if (addr[2] == LUT::StrobeGGInput)
                return ioCfg.l2.DynamicInputChoices[addr[1]].strobeChoices[conValue];

            if (addr[2] < ioCfg.l2.DynamicInputChoices[addr[1]].lutChoices.size())
                return ioCfg.l2.DynamicInputChoices[addr[1]].lutChoices[addr[2]][conValue];

            if (addr[2] < LUT::InputBits)
                return ioCfg.l2.StaticConnections[addr[1]][addr[2]].address;

            return {};

        case 3:
            return ioCfg.l3.DynamicInputChoiceLists[addr[1]][conValue];
    }

    return {};
}

} // end namespace trigger_io
} // end namespace mvlc
} // end namespace mesytec
