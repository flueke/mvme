/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __MVLC_SCRIPT_H__
#define __MVLC_SCRIPT_H__

#include <mesytec-mvlc/mvlc_constants.h>
#include "vme_script.h"
#include "libmvme_mvlc_export.h"

class QTextStream;

namespace mesytec
{
namespace mvme_mvlc
{
namespace script
{

// mvlc script - Text based MVLC control and setup
//
// Text based setup for the MVLC similar to the existing VME Script. The basic
// blocks are MVLC Super Commands. VME Command Stacks can be built via embedded
// VME Scripts.
//
// Super Commands are
// * ref_word <value>
//   Insert a reference word into the output stream
//
// * read_local <address>
//   Read the given local/register address
//
// * write_local <address> <value>
//   Write to the given local/register address
//
// * write_reset
//   Send the special reset command
//
// * stack_start [offset=0x00] [output=command]
//     <vme_script contents>
//   stack_end
//
//   Start a stack definition. In between stack_start and stack_end the parser
//   switches to vme_script syntax.
//   Arguments to stack_start:
//  - offset (defaults to 0)
//    Byte offset into the stack memory area. The two low bits must not be set.
//
//  - output
//    The output pipe for the stack. Either 0/1 or 'command'/'data'
//    respectively.

enum class CommandType
{
    Invalid,
    ReferenceWord,
    ReadLocal,
    ReadLocalBlock,
    WriteLocal,
    WriteReset,
    Stack,
};

struct Command
{
    struct Stack
    {
        vme_script::VMEScript contents;
        u8 outputPipe = 0;
        u16 offset = 0;
    };

    CommandType type = CommandType::Invalid;
    u32 address = 0;
    u32 value = 0;
    Stack stack;
    u32 lineNumber = 0;
};

using CommandList = QVector<Command>;

// Parsing of script text input and transformation into a list of commands.
CommandList LIBMVME_MVLC_EXPORT parse(QFile *input);
CommandList LIBMVME_MVLC_EXPORT parse(const QString &input);
CommandList LIBMVME_MVLC_EXPORT parse(QTextStream &input);
CommandList LIBMVME_MVLC_EXPORT parse(const std::string &input);

struct ParseError
{
    ParseError(const QString &message, int lineNumber = 0)
        : message(message)
        , lineNumber(lineNumber)
    {}

    QString toString() const
    {
        if (lineNumber >= 0)
            return QString("%1 on line %2").arg(message).arg(lineNumber);
        return message;
    }

    std::string what() const
    {
        return toString().toStdString();
    }

    QString message;
    int lineNumber;
};

// Helper to build a command list programmatically instead of parsing MVLC
// script text.
class MVLCCommandListBuilder
{
    public:
        // Super Commands
        void addReferenceWord(u16 refValue);
        void addReadLocal(u16 address);
        void addReadLocalBlock(u16 address, u16 words);
        void addWriteLocal(u16 address, u32 value);
        void addWriteReset();

        // Stacks containing VME commands
        void addStack(u8 outputPipe, u16 offset, const vme_script::VMEScript &contents);

        // Below are shortcut methods which internally create a stack using
        // outputPipe=CommandPipe(=0) and offset=0

        // single value reads
        void addVMERead(u32 address, u8 amod, mvlc::VMEDataWidth dataWidth);

        // block reads (BLT, MBLT, 2eSST64)
        void addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);
        //void add2eSST64Read(u32 address, u16 maxTransfers, Blk2eSSTRate rate = Rate300MB);

        // single value write
        void addVMEWrite(u32 address, u32 value, u8 amod, mvlc::VMEDataWidth dataWidth);

        CommandList getCommandList() const;

        // Append the Command from 'other' to this builders commands.
        void append(const MVLCCommandListBuilder &other);

        void clear() { m_commands.clear(); }

    private:
        CommandList m_commands;
};

// Transform a single MVLC Command into a list of 32-bit MVLC command words.
// Note: this does not include the CmdBufferStart and CmdBufferEnd words needed
// at the start and end to form a full, valid MVLC buffer.
std::vector<u32> LIBMVME_MVLC_EXPORT to_mvlc_buffer(const Command &cmd);

// Transform a list of commands into a full MVLC command buffer. The buffer
// starts with CmdBufferStart and ends with CmdBufferEnd.
// This form can be directly sent to the MVLC.
std::vector<u32> LIBMVME_MVLC_EXPORT to_mvlc_command_buffer(const CommandList &cmdList);

} // end namespace script
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVLC_SCRIPT_H__ */
