#ifndef __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__
#define __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__

#include <vector>

#include "mesytec-mvlc_export.h"
#include "mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

struct MESYTEC_MVLC_EXPORT SuperCommand
{
    SuperCommandType type;
    u16 address;
    u32 value;

    bool operator==(const SuperCommand &o) const noexcept
    {
        return (type == o.type
                && address == o.address
                && value == o.value);
    }

    bool operator!=(const SuperCommand &o) const noexcept
    {
        return !(*this == o);
    }
};

struct MESYTEC_MVLC_EXPORT StackCommand
{
    StackCommandType type;
    u32 address;
    u32 value;
    u8 amod;
    VMEDataWidth dataWidth;
    u16 transfers;
    Blk2eSSTRate rate;

    bool operator==(const StackCommand &o) const noexcept
    {
        return (type == o.type
                && address == o.address
                && value == o.value
                && amod == o.amod
                && dataWidth == o.dataWidth
                && transfers == o.transfers
                && rate == o.rate);
    }

    bool operator!=(const StackCommand &o) const noexcept
    {
        return !(*this == o);
    }
};

class MESYTEC_MVLC_EXPORT SuperCommandBuilder
{
    public:
        SuperCommandBuilder &addReferenceWord(u16 refValue);
        SuperCommandBuilder &addReadLocal(u16 address);
        SuperCommandBuilder &addReadLocalBlock(u16 address, u16 words);
        SuperCommandBuilder &addWriteLocal(u16 address, u32 value);
        SuperCommandBuilder &addWriteReset();
        SuperCommandBuilder &addCommand(const SuperCommand &cmd);
        SuperCommandBuilder &addCommands(const std::vector<SuperCommand> &commands);

        // Below are shortcut methods which internally create a stack using
        // outputPipe=CommandPipe(=0) and stackMemoryOffset=0
        SuperCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        SuperCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);
        SuperCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

        std::vector<SuperCommand> getCommands() const;

    private:
        std::vector<SuperCommand> m_commands;
};

class MESYTEC_MVLC_EXPORT StackCommandBuilder
{
    public:
        StackCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);
        StackCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addWriteMarker(u32 value);
        StackCommandBuilder &addCommand(const StackCommand &cmd);

        std::vector<StackCommand> getCommands() const;

    private:
        std::vector<StackCommand> m_commands;
};

//
// Conversion to the mvlc buffer format
//

std::vector<u32> make_command_buffer(const SuperCommandBuilder &commands);
std::vector<u32> make_command_buffer(const std::vector<SuperCommand> &commands);

SuperCommandBuilder super_builder_from_buffer(const std::vector<u32> &buffer);

// Stack to raw stack commands. Not enclosed between StackStart and StackEnd,
// not interleaved with the write commands for uploading.
std::vector<u32> make_stack_buffer(const StackCommandBuilder &builder);
std::vector<u32> make_stack_buffer(const std::vector<StackCommand> &stack);

StackCommandBuilder stack_builder_from_buffer(const std::vector<u32> &buffer);

// Enclosed between StackStart and StackEnd, interleaved with WriteLocal commands.
std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const StackCommandBuilder &stack);

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<StackCommand> &stack);

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<u32> &stackBuffer);

}
}

#endif /* __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__ */
