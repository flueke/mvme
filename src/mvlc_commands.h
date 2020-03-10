#ifndef __MESYTEC_MVLC_MVLC_COMMANDS_H__
#define __MESYTEC_MVLC_MVLC_COMMANDS_H__

#include <vector>

#include "mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

struct SuperCommand
{
    SuperCommandType type;
    u16 address;
    u32 value;
};

struct StackCommand
{
    StackCommandType type;
    u32 address;
    u32 value;
    u8 amod;
    VMEDataWidth dataWidth;
    u16 transfers;
    Blk2eSSTRate rate;
};

class SuperCommandBuilder
{
    public:
        SuperCommandBuilder &addReferenceWord(u16 refValue);
        SuperCommandBuilder &addReadLocal(u16 address);
        SuperCommandBuilder &addReadLocalBlock(u16 address, u16 words);
        SuperCommandBuilder &addWriteLocal(u16 address, u32 value);
        SuperCommandBuilder &addWriteReset();
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

class StackCommandBuilder
{
    public:
        StackCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);
        StackCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addWriteMarker(u32 value);

        std::vector<StackCommand> getCommands() const;

    private:
        std::vector<StackCommand> m_commands;
};

//
// Conversion to the mvlc buffer format
//

// Stack to raw stack commands. Not enclosed between StackStart and StackEnd,
// not interleaved with the write commands for uploading.
std::vector<u32> make_stack_buffer(const StackCommandBuilder &builder);
std::vector<u32> make_stack_buffer(const std::vector<StackCommand> &stack);

// Enclosed between StackStart and StackEnd, interleaved with WriteLocal commands.
std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const StackCommandBuilder &stack);

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<StackCommand> &stack);

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<u32> &stackBuffer);

std::vector<u32> make_command_buffer(const SuperCommandBuilder &commands);
std::vector<u32> make_command_buffer(const std::vector<SuperCommand> &commands);


}
}

#endif /* __MESYTEC_MVLC_MVLC_COMMANDS_H__ */
