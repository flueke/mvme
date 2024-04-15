#include <gtest/gtest.h>

#include "vmeconfig_to_crateconfig.h"
#include "vmeconfig_from_crateconfig.h"
#include "mvlc_daq.h"

// Idea for testing the mvme VMEConfig <-> mvlc YAML conversion code:
// - Input is a single vme_script command line
// - Parse the line to get a vme_script::Command
// - Convert to mvlc::StackCommand using vme_script_command_to_mvlc_command()

// - Convert the mvlc::StackCommand back to a vme_script::Command
// - Compare the original vme_script::Command with the new one.
// If the commands are equal the conversion works as intended.

TEST(vmeconfig_crateconfig, ExportImportCommands)
{
    using namespace mesytec;

    auto test_one_command = [] (const QString &mvmeCmdString)
    {
        try
        {
            auto mvmeCmd = vme_script::parse(mvmeCmdString).first();
            // vme_script::parse() sets the lineNumber to 1,
            // mvme::mvlc_command_to_vme_script_command() leaves it set to 0.
            mvmeCmd.lineNumber = 0;

            auto mvlcCmd = mvme::vme_script_command_to_mvlc_command(mvmeCmd);

            std::cout << "mvme vmeScript command: " << mvmeCmdString.toLocal8Bit().data() << "\n";
            std::cout << "mvlc command string:    " << to_string(mvlcCmd) << "\n";

            auto mvmeCmdImported = mvme::mvlc_command_to_vme_script_command(mvlcCmd);

            if (mvmeCmd != mvmeCmdImported)
            {
                int x = 42; (void) x;  // for breakpoints
            }

            ASSERT_EQ(mvmeCmd, mvmeCmdImported);
            std::cout << "\n";
        }
        catch (const vme_script::ParseError &e)
        {
            std::cout << "vme_script::ParseError: " << e.toString().toLocal8Bit().data() << "\n";
            throw;
        }
    };


    // Note: No test for 'readabs' or 'writeabs'. The MVLC YAML file always
    // contains absolute addresses and currently 'read'/'write' commands are
    // used when importing.
    // FIXME (maybe): wouldn't it be better to produce the 'abs' versions when
    // importing? Otherwise when importing into a module script the base address
    // would be added to the absolute module address. Not an issue if all
    // imported modules are created with address 0x0 which currently is the
    // case.

    test_one_command("write a16 d16 0x1234 0xaffe");
    test_one_command("write a24 d32 0x1234 0xaffe");
    test_one_command("write a32 d16 0x1234 0xaffe");

    test_one_command("read a16 d16 0x1234");
    test_one_command("read a24 d32 0x1234 slow");
    test_one_command("read a32 d16 0x1234 late");
    test_one_command("read a16 d32 0x1234 fifo");
    test_one_command("read a24 d16 0x1234 mem");
    test_one_command("read a32 d32 0x1234 slow fifo");
    test_one_command("read a16 d16 0x1234 slow mem");

    test_one_command("blt a24 0x1234 1000");
    test_one_command("blt a32 0x4321 9000");
    test_one_command("bltfifo a24 0x1234 1000");
    test_one_command("bltfifo a32 0x4321 9000");

    // Note: a24 is not a valid amod for mblt transfers
    test_one_command("mblt a32 0x1234 1000");
    test_one_command("mbltfifo a32 0x4321 9000");
    test_one_command("mblts a32 0x1234 1000");
    test_one_command("mbltsfifo a32 0x1234 1000");

    test_one_command("2esst 0x1234 276mb 1000");
    test_one_command("2esstfifo 0x1234 276mb 1000");
    test_one_command("2esstmem 0x1234 276mb 1000");

    test_one_command("2essts 0x1234 276mb 1000");
    test_one_command("2esstsfifo 0x1234 276mb 1000");
    test_one_command("2esstsmem 0x1234 276mb 1000");

    test_one_command("wait 100");
    test_one_command("marker 0x1234");

    test_one_command("mvlc_wait 314159");
    test_one_command("mvlc_signal_accu");
    test_one_command("mvlc_mask_shift_accu 0x55 13");
    test_one_command("mvlc_set_accu 666");

    test_one_command("mvlc_read_to_accu a16 d16 0x1234");
    test_one_command("mvlc_read_to_accu a24 d32 0x1234 slow");
    test_one_command("mvlc_read_to_accu a32 d16 0x1234 late");

    test_one_command("mvlc_compare_loop_accu eq 42");
    test_one_command("mvlc_compare_loop_accu gt 42");
    test_one_command("mvlc_compare_loop_accu lt 42");

    test_one_command("mvlc_writespecial timestamp");
    test_one_command("mvlc_writespecial accu");
    test_one_command("mvlc_writespecial 1234");
}

TEST(vmeconfig_crateconfig, TriggerValues)
{
    using namespace mesytec;

    // ev0: irq3 with iack
    auto ev0 = new EventConfig; // Qt raw pointer ownership :-(
    ev0->triggerCondition = TriggerCondition::Interrupt;
    ev0->triggerOptions["IRQUseIACK"] = true;
    ev0->irqLevel = 3;

    // ev1: irq1 without iack
    auto ev1 = new EventConfig;
    ev1->triggerCondition = TriggerCondition::Interrupt;
    ev1->triggerOptions["IRQUseIACK"] = false;
    ev1->irqLevel = 3;

    // ev2: periodic via trigger io. The stack trigger is set to "External".
    // Setup of the trigger io must be done elsewhere, it's not part of
    // calculating the trigger values.
    auto ev2 = new EventConfig;
    ev2->triggerCondition = TriggerCondition::Periodic;

    // ev3: via trigger io. Same as Periodic above when it comes to calculating
    // the trigger value.
    auto ev3 = new EventConfig;
    ev3->triggerCondition = TriggerCondition::TriggerIO;

    // ev4: via stack timer (MVLC >= FW0037).
    auto ev4 = new EventConfig;
    ev4->triggerCondition = TriggerCondition::MvlcStackTimer;

    // ev5: via stack timer (MVLC >= FW0037).
    auto ev5 = new EventConfig;
    ev5->triggerCondition = TriggerCondition::MvlcStackTimer;

    // ev6: on slave trigger (MVLC >= FW0037).
    auto ev6 = new EventConfig;
    ev6->triggerCondition = TriggerCondition::MvlcOnSlaveTrigger;
    ev6->triggerOptions["mvlc.slavetrigger_index"] = 2;

    // ev7: on slave trigger (MVLC >= FW0037).
    auto ev7 = new EventConfig;
    ev7->triggerCondition = TriggerCondition::MvlcOnSlaveTrigger;
    ev7->triggerOptions["mvlc.slavetrigger_index"] = 1;

    auto vmeCfg = std::make_unique<VMEConfig>();

    for (auto ev: { ev0, ev1, ev2, ev3, ev4, ev5, ev6, ev7 })
        vmeCfg->addEventConfig(ev);

    auto crateConfig = mvme::vmeconfig_to_crateconfig(vmeCfg.get());
    auto vmeCfgImported = mvme::vmeconfig_from_crateconfig(crateConfig);

    ASSERT_EQ(vmeCfg->getEventConfigs().size(), vmeCfgImported->getEventConfigs().size());

    #if 0
    fmt::print("trigger_values: {}\n", fmt::join(mvme_mvlc::get_trigger_values(*vmeCfg).first, ", "));
    fmt::print("trigger_values: {}\n", fmt::join(mvme_mvlc::get_trigger_values(*vmeCfgImported).first, ", "));
    fmt::print("trigger_values: {}\n", fmt::join(crateConfig.triggers, ", "));
    #endif

    ASSERT_EQ(mvme_mvlc::get_trigger_values(*vmeCfg), mvme_mvlc::get_trigger_values(*vmeCfg));

}

TEST(vmeconfig_crateconfig, ReadoutCommands)
{
    using namespace mesytec;

    auto mod0 = new ModuleConfig;
    mod0->setBaseAddress(0x12340000u);
    QStringList readout0;
    readout0 << "mbltfifo a32 0xaaaa 9000";
    readout0 << "write a32 d16 0x6034 1";
    mod0->getReadoutScript()->setScriptContents(readout0.join("\n"));

    auto mod1 = new ModuleConfig;
    mod1->setBaseAddress(0x02000000u);
    QStringList readout1;
    readout1 << "read a32 d16 0xbbbb";
    readout1 << "2essts 0x1234 276mb 1000";
    readout1 << "read a24 d32 0x2222";
    mod1->getReadoutScript()->setScriptContents(readout1.join("\n"));

    auto ev0 = new EventConfig;
    ev0->addModuleConfig(mod0);
    ev0->addModuleConfig(mod1);

    auto vmeCfg = std::make_unique<VMEConfig>();

    for (auto ev: { ev0 })
        vmeCfg->addEventConfig(ev);

    auto crateConfig = mvme::vmeconfig_to_crateconfig(vmeCfg.get());
    auto vmeCfgImported = mvme::vmeconfig_from_crateconfig(crateConfig);

    ASSERT_EQ(vmeCfgImported->getEventConfigs().size(), 1);

    auto ev0Imported = vmeCfgImported->getEventConfigs().at(0);

    ASSERT_EQ(ev0Imported->getModuleConfigs().size(), 2);

    {
        auto mod0Imported = ev0Imported->getModuleConfigs().at(0);
        auto mod0ReadoutImported = vme_script::parse(mod0Imported->getReadoutScript()->getScriptContents());
        ASSERT_EQ(mod0ReadoutImported.size(), 2);
        ASSERT_EQ(mod0ReadoutImported.at(0).type, vme_script::CommandType::MBLTFifo);
        ASSERT_EQ(mod0ReadoutImported.at(0).address, 0x1234aaaa);
        ASSERT_EQ(mod0ReadoutImported.at(0).transfers, 9000);
        ASSERT_EQ(mod0ReadoutImported.at(1).type, vme_script::CommandType::Write);
        ASSERT_EQ(mod0ReadoutImported.at(1).address, 0x12346034);
        ASSERT_EQ(mod0ReadoutImported.at(1).value, 1);
    }

    {
        auto mod1Imported = ev0Imported->getModuleConfigs().at(1);
        auto mod1ReadoutImported = vme_script::parse(mod1Imported->getReadoutScript()->getScriptContents());

        ASSERT_EQ(mod1ReadoutImported.size(), 3);

        ASSERT_EQ(mod1ReadoutImported.at(0).type, vme_script::CommandType::Read);
        ASSERT_EQ(mod1ReadoutImported.at(0).address, 0x0200bbbb);
        ASSERT_EQ(mod1ReadoutImported.at(1).type, vme_script::CommandType::Blk2eSST64SwappedFifo);
        ASSERT_EQ(mod1ReadoutImported.at(1).address, 0x02001234);
        ASSERT_EQ(mod1ReadoutImported.at(2).type, vme_script::CommandType::Read);
        ASSERT_EQ(mod1ReadoutImported.at(2).address, 0x02002222);
    }
}
