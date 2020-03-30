#ifndef __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__
#define __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__

#include <string>
#include <vector>

#include "mesytec-mvlc_export.h"
#include "mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

//
// SuperCommands for direct communication with the MVLC
//

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

class StackCommandBuilder;

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

        SuperCommandBuilder &addStackUpload(
            const StackCommandBuilder &stackBuilder,
            u8 stackOutputPipe, u16 stackMemoryOffset);

        SuperCommandBuilder &addStackUpload(
            const std::vector<u32> &stackBuffer,
            u8 stackOutputPipe, u16 stackMemoryOffset);

        std::vector<SuperCommand> getCommands() const;

    private:
        std::vector<SuperCommand> m_commands;
};

//
// StackCommands for direct execution and VME readout
//

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

class MESYTEC_MVLC_EXPORT StackCommandBuilder
{
    public:
        struct Group
        {
            std::string name;
            std::vector<StackCommand> commands;
        };

        // These methods each add a single command to the currently open group.
        // If there exists no open group a new group with an empty name will be
        // created.
        StackCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);
        StackCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addWriteMarker(u32 value);
        StackCommandBuilder &addCommand(const StackCommand &cmd);

        // Begins a new group using the supplied name.
        StackCommandBuilder &beginGroup(const std::string &name = {});

        // Returns true if at least one group exists in this StackCommandBuilder.
        bool hasOpenGroup() const { return !m_groups.empty(); }

        // Returns the number of groups in this StackCommandBuilder.
        size_t getGroupCount() const { return m_groups.size(); }

        // Returns the list of groups forming the stack.
        std::vector<Group> getGroups() const { return m_groups; }

        // Returns the group with the given groupIndex or a default constructed
        // group if the index is out of range.
        Group getGroup(size_t groupIndex) const;

        // Returns the group with the given groupName or a default constructed
        // group if the index is out of range.
        Group getGroup(const std::string &groupName) const;

        // Returns a flattened list of the commands of all groups.
        std::vector<StackCommand> getCommands() const;

        // Returns the list of commands for the group with the given groupIndex
        // or an empty list if the index is out of range.
        std::vector<StackCommand> getCommands(size_t groupIndex) const;

        // Returns the list of commands for the group with the given groupName
        // or an empty list if no such group exists.
        std::vector<StackCommand> getCommands(const std::string &groupName) const;

    private:
        std::vector<Group> m_groups;
};

//
// Conversion to the mvlc buffer format
//

MESYTEC_MVLC_EXPORT std::vector<u32> make_command_buffer(const SuperCommandBuilder &commands);
MESYTEC_MVLC_EXPORT std::vector<u32> make_command_buffer(const std::vector<SuperCommand> &commands);

MESYTEC_MVLC_EXPORT SuperCommandBuilder super_builder_from_buffer(const std::vector<u32> &buffer);

// Stack to raw stack commands. Not enclosed between StackStart and StackEnd,
// not interleaved with the write commands for uploading.
MESYTEC_MVLC_EXPORT std::vector<u32> make_stack_buffer(const StackCommandBuilder &builder);
MESYTEC_MVLC_EXPORT std::vector<u32> make_stack_buffer(const std::vector<StackCommand> &stack);

MESYTEC_MVLC_EXPORT StackCommandBuilder stack_builder_from_buffer(const std::vector<u32> &buffer);

// Enclosed between StackStart and StackEnd, interleaved with WriteLocal
// commands for uploading.
MESYTEC_MVLC_EXPORT std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const StackCommandBuilder &stack);

MESYTEC_MVLC_EXPORT std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<StackCommand> &stack);

MESYTEC_MVLC_EXPORT std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<u32> &stackBuffer);

}
}

#endif /* __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__ */
