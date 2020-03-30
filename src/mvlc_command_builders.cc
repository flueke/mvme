#include <algorithm>
#include <cassert>

#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "vme_constants.h"

namespace mesytec
{
namespace mvlc
{

namespace
{

bool is_super_command(u16 v)
{
    using SuperCT = SuperCommandType;

    return (v == static_cast<u16>(SuperCT::CmdBufferStart)
            || v == static_cast<u16>(SuperCT::CmdBufferEnd)
            || v == static_cast<u16>(SuperCT::ReferenceWord)
            || v == static_cast<u16>(SuperCT::ReadLocal)
            || v == static_cast<u16>(SuperCT::ReadLocalBlock)
            || v == static_cast<u16>(SuperCT::WriteLocal)
            || v == static_cast<u16>(SuperCT::WriteReset));
}

bool is_stack_command(u8 v)
{
    using StackCT = StackCommandType;

    return (v == static_cast<u8>(StackCT::StackStart)
            || v == static_cast<u8>(StackCT::StackEnd)
            || v == static_cast<u8>(StackCT::VMEWrite)
            || v == static_cast<u8>(StackCT::VMERead)
            || v == static_cast<u8>(StackCT::WriteMarker)
            || v == static_cast<u8>(StackCT::WriteSpecial));
}

}

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

SuperCommandBuilder &SuperCommandBuilder::addCommand(const SuperCommand &cmd)
{
    m_commands.push_back(cmd);
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

SuperCommandBuilder &SuperCommandBuilder::addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers)
{
    auto stack = StackCommandBuilder().addVMEBlockRead(address, amod, maxTransfers);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    auto stack = StackCommandBuilder().addVMEWrite(address, value, amod, dataWidth);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addStackUpload(
    const StackCommandBuilder &stackBuilder,
    u8 stackOutputPipe, u16 stackMemoryOffset)
{
    return addCommands(make_stack_upload_commands(stackOutputPipe, stackMemoryOffset, stackBuilder));
}

SuperCommandBuilder &SuperCommandBuilder::addStackUpload(
    const std::vector<u32> &stackBuffer,
    u8 stackOutputPipe, u16 stackMemoryOffset)
{
    return addCommands(make_stack_upload_commands(stackOutputPipe, stackMemoryOffset, stackBuffer));
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
    cmd.type = StackCommandType::VMERead;
    cmd.address = address;
    cmd.amod = amod;
    cmd.dataWidth = dataWidth;

    addCommand(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers)
{
    StackCommand cmd = {};
    cmd.type = StackCommandType::VMERead;
    cmd.address = address;
    cmd.amod = amod;
    cmd.transfers = maxTransfers;

    addCommand(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    StackCommand cmd = {};
    cmd.type = StackCommandType::VMEWrite;
    cmd.address = address;
    cmd.value = value;
    cmd.amod = amod;
    cmd.dataWidth = dataWidth;

    addCommand(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addWriteMarker(u32 value)
{
    StackCommand cmd = {};
    cmd.type = StackCommandType::WriteMarker;
    cmd.value = value;

    addCommand(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addCommand(const StackCommand &cmd)
{
    if (!hasOpenGroup())
        beginGroup();

    assert(hasOpenGroup());

    m_groups.back().commands.push_back(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::beginGroup(const std::string &name)
{
    m_groups.emplace_back(Group{name, {}});
    return *this;
}

std::vector<StackCommand> StackCommandBuilder::getCommands() const
{
    std::vector<StackCommand> ret;

    std::for_each(
        std::begin(m_groups), std::end(m_groups),
        [&ret] (const Group &group)
        {
            std::copy(
                std::begin(group.commands), std::end(group.commands),
                std::back_inserter(ret));
        });

    return ret;
}

std::vector<StackCommand> StackCommandBuilder::getCommands(size_t groupIndex) const
{
    return getGroup(groupIndex).commands;
}

std::vector<StackCommand> StackCommandBuilder::getCommands(const std::string &groupName) const
{
    return getGroup(groupName).commands;
}

StackCommandBuilder::Group StackCommandBuilder::getGroup(size_t groupIndex) const
{
    if (groupIndex < getGroupCount())
        return m_groups[groupIndex];

    return {};
}

StackCommandBuilder::Group StackCommandBuilder::getGroup(const std::string &groupName) const
{
    auto it = std::find_if(
        std::begin(m_groups), std::end(m_groups),
        [&groupName] (const Group &group)
        {
            return group.name == groupName;
        });

    if (it != std::end(m_groups))
        return *it;

    return {};
}

//
// Conversion to the mvlc buffer format
//
std::vector<u32> make_command_buffer(const SuperCommandBuilder &commands)
{
    return make_command_buffer(commands.getCommands());
}

std::vector<u32> make_command_buffer(const std::vector<SuperCommand> &commands)
{
    using namespace super_commands;
    using SuperCT = SuperCommandType;

    std::vector<u32> result;

    // CmdBufferStart
    result.push_back(static_cast<u32>(SuperCT::CmdBufferStart) << SuperCmdShift);

    for (const auto &cmd: commands)
    {
        u32 cmdWord = (static_cast<u32>(cmd.type) << SuperCmdShift);

        switch (cmd.type)
        {
            case SuperCT::ReferenceWord:
                result.push_back(cmdWord | (cmd.value & SuperCmdArgMask));
                break;

            case SuperCT::ReadLocal:
                result.push_back(cmdWord | (cmd.address & SuperCmdArgMask));
                break;

            case SuperCT::ReadLocalBlock:
                result.push_back(cmdWord | (cmd.address & SuperCmdArgMask));
                result.push_back(cmd.value); // transfer count
                break;

            case SuperCT::WriteLocal:
                result.push_back(cmdWord | (cmd.address & SuperCmdArgMask));
                result.push_back(cmd.value);
                break;

            case SuperCT::WriteReset:
                result.push_back(cmdWord);
                break;

            // Similar to StackStart and StackEnd these should not be manually
            // added to the list of super commands but they are still handled
            // in here just in case.
            case SuperCT::CmdBufferStart:
            case SuperCT::CmdBufferEnd:
                result.push_back(cmdWord);
                break;
        }
    }

    // CmdBufferEnd
    result.push_back(static_cast<u32>(SuperCT::CmdBufferEnd) << SuperCmdShift);

    return result;
}

SuperCommandBuilder super_builder_from_buffer(const std::vector<u32> &buffer)
{
    using namespace super_commands;
    using SuperCT = SuperCommandType;

    SuperCommandBuilder result;

    for (auto it = buffer.begin(); it != buffer.end(); ++it)
    {
        u16 sct = (*it >> SuperCmdShift) & SuperCmdMask;

        if (!is_super_command(sct))
            break; // TODO: error

        SuperCommand cmd = {};
        cmd.type = static_cast<SuperCT>(sct);

        switch (cmd.type)
        {
            case SuperCT::CmdBufferStart:
            case SuperCT::CmdBufferEnd:
                continue;

            case SuperCT::ReferenceWord:
                cmd.value = (*it >> SuperCmdArgShift) & SuperCmdArgMask;
                break;

            case SuperCT::ReadLocal:
                cmd.address = (*it >> SuperCmdArgShift) & SuperCmdArgMask;
                break;

            case SuperCT::ReadLocalBlock:
                cmd.address = (*it >> SuperCmdArgShift) & SuperCmdArgMask;

                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it;

                break;

            case SuperCT::WriteLocal:
                cmd.address = (*it >> SuperCmdArgShift) & SuperCmdArgMask;

                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it;

                break;

            case SuperCT::WriteReset:
                break;
        }

        result.addCommand(cmd);
    }

    return result;
}

std::vector<u32> make_stack_buffer(const StackCommandBuilder &builder)
{
    return make_stack_buffer(builder.getCommands());
}

std::vector<u32> make_stack_buffer(const std::vector<StackCommand> &stack)
{
    std::vector<u32> result;

    for (const auto &cmd: stack)
    {
        u32 cmdWord = static_cast<u32>(cmd.type) << stack_commands::CmdShift;

        switch (cmd.type)
        {
            case StackCommandType::VMERead:
                if (!vme_amods::is_block_mode(cmd.amod))
                {
                    cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                    cmdWord |= static_cast<u32>(cmd.dataWidth) << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_blt_mode(cmd.amod))
                {
                    cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                    cmdWord |= (cmd.transfers & stack_commands::CmdArg1Mask) << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_mblt_mode(cmd.amod))
                {
                    cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                    cmdWord |= (cmd.transfers & stack_commands::CmdArg1Mask) << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_esst64_mode(cmd.amod))
                {
                    cmdWord |= (cmd.amod | (static_cast<u32>(cmd.rate) << Blk2eSSTRateShift))
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
                result.push_back(cmd.value);

                break;

            case StackCommandType::WriteMarker:
                result.push_back(cmdWord);
                result.push_back(cmd.value);

                break;

            case StackCommandType::WriteSpecial:
                cmdWord |= cmd.value & 0x00FFFFFFu;
                result.push_back(cmdWord);
                break;

            // Note: these two should not be manually added to the stack but
            // will be part of the command buffer used for uploading the stack.
            case StackCommandType::StackStart:
            case StackCommandType::StackEnd:
                result.push_back(cmdWord);
                break;
        }
    }

    return result;
}

StackCommandBuilder stack_builder_from_buffer(const std::vector<u32> &buffer)
{
    using namespace stack_commands;
    using StackCT = StackCommandType;

    StackCommandBuilder result;

    for (auto it = buffer.begin(); it != buffer.end(); ++it)
    {
        u8 sct = (*it >> CmdShift) & CmdMask;
        u8 arg0 = (*it >> CmdArg0Shift) & CmdArg0Mask;
        u16 arg1 = (*it >> CmdArg1Shift) & CmdArg1Mask;

        if (!is_stack_command(sct))
            break; // TODO: error

        StackCommand cmd = {};
        cmd.type = static_cast<StackCT>(sct);

        switch (cmd.type)
        {
            case StackCT::StackStart:
            case StackCT::StackEnd:
                continue;

            case StackCT::VMERead:
                cmd.amod = arg0;

                if (!vme_amods::is_block_mode(cmd.amod))
                {
                    cmd.dataWidth = static_cast<VMEDataWidth>(arg1);
                }
                else if (vme_amods::is_blt_mode(cmd.amod))
                {
                    cmd.transfers = arg1;
                }
                else if (vme_amods::is_mblt_mode(cmd.amod))
                {
                    cmd.transfers = arg1;
                }
                else if (vme_amods::is_esst64_mode(cmd.amod))
                {
                    cmd.rate = static_cast<Blk2eSSTRate>(cmd.amod >> Blk2eSSTRateShift);
                    cmd.transfers = arg1;
                }

                if (++it != buffer.end()) // TODO: else error
                    cmd.address = *it;

                break;

            case StackCT::VMEWrite:
                cmd.amod = arg0;
                cmd.dataWidth = static_cast<VMEDataWidth>(arg1);

                if (++it != buffer.end()) // TODO: else error
                    cmd.address = *it;

                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it;

                break;

            case StackCT::WriteMarker:
                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it;

                break;

            case StackCT::WriteSpecial:
                cmd.value = *it & 0x00FFFFFFu;

                break;
        }

        result.addCommand(cmd);
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

    return make_stack_upload_commands(stackOutputPipe, stackMemoryOffset, make_stack_buffer(stack));
}

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackBuffer)
{
    SuperCommandBuilder super;

    u16 address = stacks::StackMemoryBegin + stackMemoryOffset;

    // StackStart
    super.addWriteLocal(
        address,
        (static_cast<u32>(StackCommandType::StackStart) << stack_commands::CmdShift
         | stackOutputPipe << stack_commands::CmdArg0Shift));
    address += AddressIncrement;

    // A write for each data word of the stack.
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

}
}
