#include <gtest/gtest.h>

#include "vmeconfig_to_crateconfig.h"
#include "vmeconfig_from_crateconfig.h"

// Idea for testing the mvme VMEConfig <-> mvlc YAML conversion code:
// - Input is a single vme_script command line
// - Parse the line to get a vme_script::Command
// - Convert to mvlc::StackCommand using vme_script_to_mvlc_command()

// - Convert the mvlc::StackCommand back to a vme_script::Command
// - Compare the original vme_script::Command with the new one.
// If the commands are equal the conversion works as intended.

TEST(vmeconfig_to_crateconfig, ExportImportCommands)
{
    using namespace mesytec;

    auto test_one_command = [] (const QString &mvmeCmdString)
    {
        auto mvmeCmd = vme_script::parse(mvmeCmdString).first();
        // vme_script::parse() sets the lineNumber to 1,
        // mvme::stack_command_to_vme_script_command() leaves it set to 0.
        mvmeCmd.lineNumber = 0;

        auto mvlcCmd = mvme::vme_script_to_mvlc_command(mvmeCmd);

        std::cout << "mvme vmeScript command: " << mvmeCmdString.toLocal8Bit().data() << "\n";
        std::cout << "mvlc command string:    " << to_string(mvlcCmd) << "\n";

        auto mvmeCmdImported = mvme::stack_command_to_vme_script_command(mvlcCmd);

        if (mvmeCmd != mvmeCmdImported)
        {
            int x = 42;
        }

        ASSERT_EQ(mvmeCmd, mvmeCmdImported);
        std::cout << "\n";
    };


    // Note: No test for 'readabs' or 'writeabs'. The MVLC YAML file always
    // contains absolute addresses, so importing results in 'read' or 'write'
    // commands.
    // FIXME (maybe): wouldn't it be better to produce the 'abs' versions when
    // importing? Otherwise when importing into a module script the base address
    // would be added to the absolute module address. Not an issue if all
    // imported modules have address 0x0...

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

    test_one_command("wait 100");
    test_one_command("marker 0x1234");
}
