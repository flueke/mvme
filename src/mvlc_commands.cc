#include "mvlc_commands.h"
#include "mvlc_constants.h"
#include "vme_constants.h"

namespace mesytec
{
namespace mvlc
{

//
// SuperCommandBuilder
//

SuperCommandBuilder &SuperCommandBuilder::addReferenceWord(u16 refValue)
{
    m_commands.push_back({ SuperCommandType::ReferenceWord, 0u, refValue });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addReadLocal(u16 address)
{
    m_commands.push_back({ SuperCommandType::ReadLocal, address, 0 });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addReadLocalBlock(u16 address, u16 words)
{
    m_commands.push_back({ SuperCommandType::ReadLocalBlock, address, words });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addWriteLocal(u16 address, u32 value)
{
    m_commands.push_back({ SuperCommandType::WriteLocal, address, value });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addWriteReset()
{
    m_commands.push_back({ SuperCommandType::WriteReset, 0, 0 });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addCommands(const std::vector<SuperCommand> &commands)
{
    std::copy(std::begin(commands), std::end(commands), std::back_inserter(m_commands));
    return *this;
}


// Below are shortcut methods which internally create a stack using
// outputPipe=CommandPipe(=0) and offset=0
SuperCommandBuilder &SuperCommandBuilder::addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth)
{
    auto stack = StackCommandBuilder().addVMERead(address, amod, dataWidth);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    auto stack = StackCommandBuilder().addVMEWrite(address, value, amod, dataWidth);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers)
{
    auto stack = StackCommandBuilder().addVMEBlockRead(address, amod, maxTransfers);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

std::vector<SuperCommand> SuperCommandBuilder::getCommands() const
{
    return m_commands;
}

//
// StackCommandBuilder
//

StackCommandBuilder &StackCommandBuilder::addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth)
{
    StackCommand cmd = {};
    cmd.cmd = StackCommandType::VMERead;
    cmd.address = address;
    cmd.amod = amod;
    cmd.dataWidth = dataWidth;

    m_commands.push_back(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    StackCommand cmd = {};
    cmd.cmd = StackCommandType::VMEWrite;
    cmd.address = address;
    cmd.value = value;
    cmd.amod = amod;
    cmd.dataWidth = dataWidth;

    m_commands.push_back(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers)
{
    StackCommand cmd = {};
    cmd.cmd = StackCommandType::VMERead;
    cmd.address = address;
    cmd.amod = amod;
    cmd.transfers = maxTransfers;

    m_commands.push_back(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addWriteMarker(u32 value)
{
    StackCommand cmd = {};
    cmd.cmd = StackCommandType::WriteMarker;
    cmd.value = value;

    m_commands.push_back(cmd);

    return *this;
}

std::vector<StackCommand> StackCommandBuilder::getCommands() const
{
    return m_commands;
}

//
// Conversion to the mvlc buffer format
//
std::vector<u32> make_stack_buffer(const StackCommandBuilder &builder)
{
    return make_stack_buffer(builder.getCommands());
}

std::vector<u32> make_stack_buffer(const std::vector<StackCommand> &stack)
{
    std::vector<u32> result;

    for (const auto &cmd: stack)
    {
        u32 cmdWord = static_cast<u32>(cmd.cmd) << stack_commands::CmdShift;

        switch (cmd.cmd)
        {
            case StackCommandType::VMERead:
                if (!vme_amods::is_block_mode(cmd.amod))
                {
                    cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                    cmdWord |= static_cast<u32>(cmd.dataWidth) << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_blt_mode(cmd.amod))
                {
                    cmdWord |= vme_amods::BLT32 << stack_commands::CmdArg0Shift;
                    cmdWord |= (cmd.transfers & stack_commands::CmdArg1Mask) << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_mblt_mode(cmd.amod))
                {
                    cmdWord |= vme_amods::MBLT64 << stack_commands::CmdArg0Shift;
                    cmdWord |= (cmd.transfers & stack_commands::CmdArg1Mask) << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_esst64_mode(cmd.amod))
                {
                    cmdWord |= (vme_amods::Blk2eSST64 | (static_cast<u32>(cmd.rate) << Blk2eSSTRateShift))
                        << stack_commands::CmdArg0Shift;
                    cmdWord |= (cmd.transfers & stack_commands::CmdArg1Mask) << stack_commands::CmdArg1Shift;
                }

                result.push_back(cmdWord);
                result.push_back(cmd.address);

                break;

            case StackCommandType::VMEWrite:
                cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                cmdWord |= static_cast<u32>(cmd.dataWidth) << stack_commands::CmdArg1Shift;

                result.push_back(cmdWord);
                result.push_back(cmd.address);

                break;

            case StackCommandType::WriteMarker:
                result.push_back(cmdWord);
                result.push_back(cmd.value);

                break;

            case StackCommandType::WriteSpecial:
                cmdWord |= cmd.value & 0x00FFFFFFu;
                result.push_back(cmdWord);
                break;

            // Note: these two should not be added to the stack but will be
            // part of the command buffer used for uploading the stack.
            case StackCommandType::StackStart:
            case StackCommandType::StackEnd:
                result.push_back(cmdWord);
                break;
        }
    }

    return result;
}

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 stackMemoryOffset, const StackCommandBuilder &stack)
{
    return make_stack_upload_commands(stackOutputPipe, stackMemoryOffset, stack.getCommands());
}

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<StackCommand> &stack)
{
    auto stackBuffer = make_stack_buffer(stack);

    SuperCommandBuilder super;

    u16 address = stacks::StackMemoryBegin + stackMemoryOffset;

    // StackStart
    super.addWriteLocal(
        address,
        (static_cast<u32>(StackCommandType::StackStart) << stack_commands::CmdShift
         | stackOutputPipe << stack_commands::CmdArg0Shift));
    address += AddressIncrement;

    for (u32 stackWord: stackBuffer)
    {
        super.addWriteLocal(address, stackWord);
        address += AddressIncrement;
    }

    // StackEnd
    super.addWriteLocal(
        address,
        static_cast<u32>(StackCommandType::StackEnd) << stack_commands::CmdShift);
    address += AddressIncrement;

    return super.getCommands();
}

std::vector<u32> make_command_buffer(const SuperCommandBuilder &commands)
{
}

}
}
