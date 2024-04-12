#include <gtest/gtest.h>

#include "vmeconfig_to_crateconfig.h"
#include "vmeconfig_from_crateconfig.h"

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
    // contains absolute addresses importing results in 'read' or 'write'
    // commands.
    // FIXME (maybe): wouldn't it be better to produce the 'abs' versions when
    // importing? Otherwise when importing into a module script the base address
    // would be added to the absolute module address. Not an issue if all
    // imported modules are created with address 0x0.

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

//TEST(vmeconfig_crateconfig,
