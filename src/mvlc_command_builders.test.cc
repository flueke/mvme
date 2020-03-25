#include <iostream>

#include "gtest/gtest.h"
#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "vme_constants.h"

using std::cout;
using std::endl;

using namespace mesytec::mvlc;
using SuperCT = SuperCommandType;
using StackCT = StackCommandType;

TEST(mvlc_commands, SuperReferenceWord)
{
    auto builder = SuperCommandBuilder().addReferenceWord(0x1337u);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::ReferenceWord);
    ASSERT_EQ(builder.getCommands()[0].value, 0x1337u);

    std::vector<u32> expected =
    {
        static_cast<u32>(SuperCT::CmdBufferStart) << super_commands::SuperCmdShift,
        (static_cast<u32>(SuperCT::ReferenceWord) << super_commands::SuperCmdShift) | 0x1337u,
        static_cast<u32>(SuperCT::CmdBufferEnd) << super_commands::SuperCmdShift,
    };

    auto buffer = make_command_buffer(builder);

    ASSERT_EQ(buffer, expected);
}

TEST(mvlc_commands, SuperReadLocal)
{
    auto builder = SuperCommandBuilder().addReadLocal(0x1337u);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::ReadLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 0);

    std::vector<u32> expected =
    {
        static_cast<u32>(SuperCT::CmdBufferStart) << super_commands::SuperCmdShift,
        (static_cast<u32>(SuperCT::ReadLocal) << super_commands::SuperCmdShift) | 0x1337u,
        static_cast<u32>(SuperCT::CmdBufferEnd) << super_commands::SuperCmdShift,
    };

    auto buffer = make_command_buffer(builder);

    ASSERT_EQ(buffer, expected);
}

TEST(mvlc_commands, SuperReadLocalBlock)
{
    auto builder = SuperCommandBuilder().addReadLocalBlock(0x1337u, 42);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::ReadLocalBlock);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 42);

    std::vector<u32> expected =
    {
        static_cast<u32>(SuperCT::CmdBufferStart) << super_commands::SuperCmdShift,
        (static_cast<u32>(SuperCT::ReadLocalBlock) << super_commands::SuperCmdShift) | 0x1337u,
        42,
        static_cast<u32>(SuperCT::CmdBufferEnd) << super_commands::SuperCmdShift,
    };

    auto buffer = make_command_buffer(builder);

    ASSERT_EQ(buffer, expected);
}

TEST(mvlc_commands, SuperWriteLocal)
{
    auto builder = SuperCommandBuilder().addWriteLocal(0x1337u, 42);

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::WriteLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 42);

    std::vector<u32> expected =
    {
        static_cast<u32>(SuperCT::CmdBufferStart) << super_commands::SuperCmdShift,
        (static_cast<u32>(SuperCT::WriteLocal) << super_commands::SuperCmdShift) | 0x1337u,
        42,
        static_cast<u32>(SuperCT::CmdBufferEnd) << super_commands::SuperCmdShift,
    };

    auto buffer = make_command_buffer(builder);

    ASSERT_EQ(buffer, expected);
}

TEST(mvlc_commands, SuperWriteReset)
{
    auto builder = SuperCommandBuilder().addWriteReset();

    ASSERT_EQ(builder.getCommands().size(), 1u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::WriteReset);
    ASSERT_EQ(builder.getCommands()[0].address, 0);
    ASSERT_EQ(builder.getCommands()[0].value, 0);

    std::vector<u32> expected =
    {
        static_cast<u32>(SuperCT::CmdBufferStart) << super_commands::SuperCmdShift,
        (static_cast<u32>(SuperCT::WriteReset) << super_commands::SuperCmdShift),
        static_cast<u32>(SuperCT::CmdBufferEnd) << super_commands::SuperCmdShift,
    };

    auto buffer = make_command_buffer(builder);

    ASSERT_EQ(buffer, expected);
}

TEST(mvlc_commands, SuperAddCommands)
{
    auto builder = SuperCommandBuilder().addCommands(
        {
            { SuperCommandType::ReadLocal, 0x1337u, 0 },
            { SuperCommandType::WriteLocal, 0x1338u, 42 },
        });

    ASSERT_EQ(builder.getCommands().size(), 2u);
    ASSERT_EQ(builder.getCommands()[0].type, SuperCommandType::ReadLocal);
    ASSERT_EQ(builder.getCommands()[0].address, 0x1337u);
    ASSERT_EQ(builder.getCommands()[0].value, 0);

    ASSERT_EQ(builder.getCommands()[1].type, SuperCommandType::WriteLocal);
    ASSERT_EQ(builder.getCommands()[1].address, 0x1338u);
    ASSERT_EQ(builder.getCommands()[1].value, 42);

    std::vector<u32> expected =
    {
        static_cast<u32>(SuperCT::CmdBufferStart) << super_commands::SuperCmdShift,
        (static_cast<u32>(SuperCT::ReadLocal) << super_commands::SuperCmdShift) | 0x1337u,
        (static_cast<u32>(SuperCT::WriteLocal) << super_commands::SuperCmdShift) | 0x1338u,
        42,
        static_cast<u32>(SuperCT::CmdBufferEnd) << super_commands::SuperCmdShift,
    };

    auto buffer = make_command_buffer(builder);

    ASSERT_EQ(buffer, expected);
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

    auto buffer = make_command_buffer(builder);

#if 0
    cout << "buffer size = " << buffer.size() << endl;

    for (u32 value: buffer)
    {
        cout << std::showbase << std::hex << value << endl;
    }
#endif
    ASSERT_EQ(buffer.size(), 10);

    ASSERT_EQ(buffer[0], static_cast<u32>(SuperCT::CmdBufferStart) << super_commands::SuperCmdShift);

    ASSERT_EQ(buffer[1], (static_cast<u32>(SuperCT::WriteLocal) << super_commands::SuperCmdShift) | stacks::StackMemoryBegin + AddressIncrement * 0);
    ASSERT_EQ(buffer[2], static_cast<u32>(StackCommandType::StackStart) << stack_commands::CmdShift);

    ASSERT_EQ(buffer[3], (static_cast<u32>(SuperCT::WriteLocal) << super_commands::SuperCmdShift) | stacks::StackMemoryBegin + AddressIncrement * 1);
    ASSERT_EQ(buffer[4], ((static_cast<u32>(StackCT::VMERead) << stack_commands::CmdShift)
                          | (0x09u << stack_commands::CmdArg0Shift)
                          | (static_cast<u32>(VMEDataWidth::D16) << stack_commands::CmdArg1Shift)));

    ASSERT_EQ(buffer[5], (static_cast<u32>(SuperCT::WriteLocal) << super_commands::SuperCmdShift) | stacks::StackMemoryBegin + AddressIncrement * 2);
    ASSERT_EQ(buffer[6], 0x1337);

    ASSERT_EQ(buffer[7], (static_cast<u32>(SuperCT::WriteLocal) << super_commands::SuperCmdShift) | stacks::StackMemoryBegin + AddressIncrement * 3);
    ASSERT_EQ(buffer[8], static_cast<u32>(StackCommandType::StackEnd) << stack_commands::CmdShift);

    ASSERT_EQ(buffer[9], static_cast<u32>(SuperCT::CmdBufferEnd) << super_commands::SuperCmdShift);
}

TEST(mvlc_commands, SuperFromBuffer)
{
    SuperCommandBuilder builder;
    builder.addReferenceWord(0xabcd);
    builder.addReadLocal(0x1337u);
    builder.addReadLocalBlock(0x1338u, 42);
    builder.addWriteLocal(0x1339u, 43);
    builder.addWriteReset();
    builder.addVMERead(0x6070, 0x09, VMEDataWidth::D16);
    builder.addVMEBlockRead(0x1234, vme_amods::BLT32, 44);
    builder.addVMEWrite(0x6070, 42, 0x09, VMEDataWidth::D32);

    auto buffer = make_command_buffer(builder);
    auto builder2 = super_builder_from_buffer(buffer);

    ASSERT_EQ(builder.getCommands(), builder2.getCommands());

#if 0
    cout << "buffer size = " << buffer.size() << endl;

    for (u32 value: buffer)
    {
        cout << std::showbase << std::hex << value << endl;
    }
#endif
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

TEST(mvlc_commands, StackFromBuffer)
{
    StackCommandBuilder builder;

    builder.addVMERead(0x1337u, 0x09u, VMEDataWidth::D16);
    builder.addVMEBlockRead(0x1338u, vme_amods::BLT32, 42);
    builder.addVMEWrite(0x1339u, 43, 0x09u, VMEDataWidth::D32);
    builder.addWriteMarker(0x87654321u);

    auto buffer = make_stack_buffer(builder);
    auto builder2 = stack_builder_from_buffer(buffer);

    ASSERT_EQ(builder.getCommands(), builder2.getCommands());
}
