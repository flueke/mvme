#include "gtest/gtest.h"
#include "vme_config_scripts.h"
#include <QDebug>

using namespace mesytec::mvme;

TEST(vme_config_scripts, EventSymbolTable)
{
    auto vme = std::make_unique<VMEConfig>();
    auto event = new EventConfig;

    vme->addEventConfig(event);

    auto eventScript = new VMEScriptConfig("eventScript", "", event);

    event->triggerCondition = TriggerCondition::Interrupt;
    event->irqLevel = 1;
    event->setMulticastByte(0xdd);

    {
        auto symtabs = parse_and_return_symbols(eventScript).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "1");
        ASSERT_EQ(lookup_variable("mcst", symtabs).value, "dd");
    }

    // Not IRQ triggered -> IRQ var must be "0"
    event->triggerCondition = TriggerCondition::Periodic;

    {
        auto symtabs = parse_and_return_symbols(eventScript).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "0");
        ASSERT_EQ(lookup_variable("mcst", symtabs).value, "dd");
    }
}

TEST(vme_config_scripts, SymbolTableBasic)
{
    auto vme = std::make_unique<VMEConfig>();
    auto event = new EventConfig;
    auto module1 = new ModuleConfig;
    auto module2 = new ModuleConfig;

    event->addModuleConfig(module1);
    event->addModuleConfig(module2);
    vme->addEventConfig(event);

    auto eventScript = new VMEScriptConfig("eventScript", "", event);
    auto module1Script = new VMEScriptConfig("module1Script", "", module1);
    auto module2Script = new VMEScriptConfig("module2Script", "", module2);

    event->triggerCondition = TriggerCondition::Interrupt;
    event->irqLevel = 1;
    event->setMulticastByte(0xdd);

    module1->setRaisesIRQ(false);
    module1->setRaisesIRQ(false);

    {
        auto symtabs = parse_and_return_symbols(eventScript).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "1");
        ASSERT_EQ(lookup_variable("mcst", symtabs).value, "dd");

        symtabs = parse_and_return_symbols(module1Script).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "0");

        symtabs = parse_and_return_symbols(module2Script).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "0");
    }

    module1->setRaisesIRQ(true);
    module2->setRaisesIRQ(false);

    {
        auto symtabs = parse_and_return_symbols(eventScript).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "1");
        ASSERT_EQ(lookup_variable("mcst", symtabs).value, "dd");

        symtabs = parse_and_return_symbols(module1Script).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "1");

        symtabs = parse_and_return_symbols(module2Script).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "0");
    }

    module1->setRaisesIRQ(false);
    module2->setRaisesIRQ(true);

    {
        auto symtabs = parse_and_return_symbols(eventScript).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "1");
        ASSERT_EQ(lookup_variable("mcst", symtabs).value, "dd");

        symtabs = parse_and_return_symbols(module1Script).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "0");

        symtabs = parse_and_return_symbols(module2Script).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "1");
    }

    // Not IRQ triggered -> IRQ var must be "0"
    event->triggerCondition = TriggerCondition::Periodic;

    {
        auto symtabs = parse_and_return_symbols(eventScript).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "0");
        ASSERT_EQ(lookup_variable("mcst", symtabs).value, "dd");

        symtabs = parse_and_return_symbols(module1Script).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "0");

        symtabs = parse_and_return_symbols(module2Script).second;
        ASSERT_EQ(lookup_variable("irq", symtabs).value, "0");
    }
}
