#include "gtest/gtest.h"
#include "mvlc_commands.h"
#include "mvlc_constants.h"

using namespace mesytec::mvlc;

TEST(mvlc_commands, SuperReferenceWord)
{
    auto builder = SuperCommandBuilder().addReferenceWord(0x1337u);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].cmd, SuperCommandType::ReferenceWord);
    ASSERT_EQ(builder.getCommands()[0].value, 0x1337u);
}

TEST(mvlc_commands, SuperReadLocal)
{
    auto builder = SuperCommandBuilder().addReadLocal(0x1337u);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].cmd, SuperCommandType::ReadLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 0);
}

TEST(mvlc_commands, SuperReadLocalBlock)
{
    auto builder = SuperCommandBuilder().addReadLocalBlock(0x1337u, 42);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].cmd, SuperCommandType::ReadLocalBlock);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 42);
}

TEST(mvlc_commands, SuperWriteLocal)
{
    auto builder = SuperCommandBuilder().addWriteLocal(0x1337u, 42);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].cmd, SuperCommandType::WriteLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 42);
}

TEST(mvlc_commands, SuperWriteReset)
{
    auto builder = SuperCommandBuilder().addWriteReset();

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].cmd, SuperCommandType::WriteReset);
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
    ASSERT_EQ(builder.getCommands()[0].cmd, SuperCommandType::ReadLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 0);

    ASSERT_EQ(builder.getCommands()[1].cmd, SuperCommandType::WriteLocal);
    ASSERT_EQ(builder.getCommands()[1].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[1].value, 42);
}

TEST(mvlc_commands, SuperVMERead)
{
    auto builder = SuperCommandBuilder().addVMERead(0x1337u, 0x09u, VMEDataWidth::D16);

    // Only checking that at least the writeLocal commands for StackStart and
    // StackEnd have been added.
    auto commands = builder.getCommands();

    ASSERT_TRUE(commands.size() >= 2);

    ASSERT_EQ(commands.front().cmd, SuperCommandType::WriteLocal);
    ASSERT_EQ(commands.front().value >> stack_commands::CmdShift,
              static_cast<u32>(StackCommandType::StackStart));

    ASSERT_EQ(commands.back().cmd, SuperCommandType::WriteLocal);
    ASSERT_EQ(commands.back().value >> stack_commands::CmdShift,
              static_cast<u32>(StackCommandType::StackEnd));

}
