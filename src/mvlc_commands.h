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
    SuperCommandType cmd;
    u16 address;
    u32 value;
};

struct StackCommand
{
    StackCommandType cmd;
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
        // outputPipe=CommandPipe(=0) and offset=0
        SuperCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        SuperCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);
        SuperCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);

        std::vector<SuperCommand> getCommands() const;

    private:
        std::vector<SuperCommand> m_commands;
};

class StackCommandBuilder
{
    public:
        StackCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);
        StackCommandBuilder &addWriteMarker(u32 value);

        std::vector<StackCommand> getCommands() const;

    private:
        std::vector<StackCommand> m_commands;
};

//
// Conversion to the mvlc buffer format
//

std::vector<u32> make_stack_buffer(const StackCommandBuilder &builder);
std::vector<u32> make_stack_buffer(const std::vector<StackCommand> &stack);

std::vector<u32> make_command_buffer(const SuperCommandBuilder &commands);

// Enclosed between StackStart and StackEnd, interleaved with WriteLocal commands.
std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const StackCommandBuilder &stack);

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<StackCommand> &stack);

std::vector<u32> stack_command_to_buffer(const StackCommand &cmd);
void stack_command_to_buffer(const StackCommand &cmd, std::vector<u32> &dest);

}
}

#endif /* __MESYTEC_MVLC_MVLC_COMMANDS_H__ */
