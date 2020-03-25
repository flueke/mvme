#include "gtest/gtest.h"
#include "mvlc_command_builders.h"
#include "mvlc_constants.h"

using namespace mesytec::mvlc;

TEST(mvlc_commands, SuperReferenceWord)
{
    auto builder = SuperCommandBuilder().addReferenceWord(0x1337u);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::ReferenceWord);
    ASSERT_EQ(builder.getCommands()[0].value, 0x1337u);
}

TEST(mvlc_commands, SuperReadLocal)
{
    auto builder = SuperCommandBuilder().addReadLocal(0x1337u);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::ReadLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 0);
}

TEST(mvlc_commands, SuperReadLocalBlock)
{
    auto builder = SuperCommandBuilder().addReadLocalBlock(0x1337u, 42);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::ReadLocalBlock);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 42);
}

TEST(mvlc_commands, SuperWriteLocal)
{
    auto builder = SuperCommandBuilder().addWriteLocal(0x1337u, 42);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::WriteLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 42);
}

TEST(mvlc_commands, SuperWriteReset)
{
    auto builder = SuperCommandBuilder().addWriteReset();

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::WriteReset);
    ASSERT_EQ(builder.getCommands()[0].address, 0);
    ASSERT_EQ(builder.getCommands()[0].value, 0);
}

TEST(mvlc_commands, SuperAddCommands)
{
    auto builder = SuperCommandBuilder().addCommands(
        {
            { SuperCommandType::ReadLocal, 0x1337u, 0 },
            { SuperCommandType::WriteLocal, 0x1337u, 42 },
        });

    ASSERT_EQ(builder.getCommands().size(), 2u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::ReadLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 0);

    ASSERT_EQ(builder.getCommands()[1].type, SuperCommandType::WriteLocal);
    ASSERT_EQ(builder.getCommands()[1].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[1].value, 42);
}

TEST(mvlc_commands, SuperVMERead)
{
    auto builder = SuperCommandBuilder().addVMERead(0x1337u, 0x09u, VMEDataWidth::D16);

    // Only checking that at least the WriteLocal commands for StackStart and
    // StackEnd have been added.
    auto commands = builder.getCommands();

    ASSERT_TRUE(commands.size() >= 2);

    ASSERT_EQ(commands.front().type, SuperCommandType::WriteLocal);
    ASSERT_EQ(commands.front().value >> stack_commands::CmdShift,
              static_cast<u32>(StackCommandType::StackStart));

    ASSERT_EQ(commands.back().type, SuperCommandType::WriteLocal);
    ASSERT_EQ(commands.back().value >> stack_commands::CmdShift,
              static_cast<u32>(StackCommandType::StackEnd));
}

TEST(mvlc_commands, StackVMERead)
{
    auto builder = StackCommandBuilder().addVMERead(0x1337u, 0x09u, VMEDataWidth::D32);
    auto commands = builder.getCommands();
    ASSERT_EQ(commands.size(), 1u);
    ASSERT_EQ(commands.front().type, StackCommandType::VMERead);
    ASSERT_EQ(commands.front().address, 0x1337u);
    ASSERT_EQ(commands.front().amod, 0x09u);
    ASSERT_EQ(commands.front().dataWidth, VMEDataWidth::D32);
}

TEST(mvlc_commands, StackVMEWrite)
{
    auto builder = StackCommandBuilder().addVMEWrite(0x1337u, 42u, 0x09u, VMEDataWidth::D32);
    auto commands = builder.getCommands();
    ASSERT_EQ(commands.size(), 1u);
    ASSERT_EQ(commands.front().type, StackCommandType::VMEWrite);
    ASSERT_EQ(commands.front().address, 0x1337u);
    ASSERT_EQ(commands.front().value, 42u);
    ASSERT_EQ(commands.front().amod, 0x09u);
    ASSERT_EQ(commands.front().dataWidth, VMEDataWidth::D32);
}

TEST(mvlc_commands, StackVMEBlockRead)
{
    auto builder = StackCommandBuilder().addVMEBlockRead(0x1337u, 0x09u, 111);
    auto commands = builder.getCommands();
    ASSERT_EQ(commands.size(), 1u);
    ASSERT_EQ(commands.front().type, StackCommandType::VMERead);
    ASSERT_EQ(commands.front().address, 0x1337u);
    ASSERT_EQ(commands.front().amod, 0x09u);
    ASSERT_EQ(commands.front().transfers, 111);
}

TEST(mvlc_commands, StackWriteMarker)
{
    auto builder = StackCommandBuilder().addWriteMarker(0x87654321u);
    auto commands = builder.getCommands();
    ASSERT_EQ(commands.size(), 1u);
    ASSERT_EQ(commands.front().type, StackCommandType::WriteMarker);
    ASSERT_EQ(commands.front().value, 0x87654321u);
}
