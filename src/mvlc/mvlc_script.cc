/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "mvlc_script.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <cassert>

#include <mesytec-mvlc/mvlc_constants.h>
#include "mvlc_util.h"

#ifndef QSL
#define QSL(str) QStringLiteral(str)
#endif

using vme_address_modes::is_block_mode;

namespace mesytec
{
namespace mvme_mvlc
{
namespace script
{

// Strips the part of the input line that's inside a multiline comment.
// If the whole line is inside the return value will be empty.
static QString handle_multiline_comment(QString line, bool &in_multiline_comment)
{
    QString result;
    result.reserve(line.size());

    s32 i = 0;

    while (i < line.size())
    {
        auto mid_ref = line.midRef(i, 2);

        if (in_multiline_comment)
        {
            if (mid_ref == "*/")
            {
                in_multiline_comment = false;
                i += 2;
            }
            else
            {
                ++i;
            }
        }
        else
        {
            if (mid_ref == "/*")
            {
                in_multiline_comment = true;
                i += 2;
            }
            else
            {
                result.append(line.at(i));
                ++i;
            }
        }
    }

    return result;
}

struct PreparsedLine
{
    QStringList parts;
    QString trimmed;
    u32 lineNumber;
};

// Get rid of comment parts and empty lines and split each of the remaining
// lines into space separated parts while keeping track of the correct input
// line numbers.
QVector<PreparsedLine> pre_parse(QTextStream &input)
{
    static const QRegularExpression reWordSplit("\\s+");

    QVector<PreparsedLine> result;
    u32 lineNumber = 0;
    bool in_multiline_comment = false;

    while (true)
    {
        auto line = input.readLine();
        ++lineNumber;

        if (line.isNull())
            break;

        line = handle_multiline_comment(line, in_multiline_comment);

        int startOfComment = line.indexOf('#');

        if (startOfComment >= 0)
            line.resize(startOfComment);

        line = line.trimmed();

        if (line.isEmpty())
            continue;

        auto parts = line.split(reWordSplit, QString::SkipEmptyParts);

        if (parts.isEmpty())
            continue;

        parts[0] = parts[0].toLower();

        result.push_back({ parts, line, lineNumber });
    }

    return result;
}

static long find_index_of_next_command(
    const QString &cmd,
    const QVector<PreparsedLine> splitLines,
    int searchStartOffset)
{
    auto it_start = splitLines.begin() + searchStartOffset;
    auto it_end   = splitLines.end();

    auto pred = [&cmd](const PreparsedLine &sl)
    {
        return sl.parts.at(0) == cmd;
    };

    auto it_result = std::find_if(it_start, it_end, pred);

    if (it_result != it_end)
    {
        return it_result - splitLines.begin();
    }

    return -1;
}

template<typename T>
T parseBinaryLiteral(const QString &str)
{
    QByteArray input = str.toLower().toLocal8Bit();

    s32 inputStart = input.startsWith("0b") ? 2 : 0;

    const size_t maxShift = (sizeof(T) * 8) - 1;
    size_t shift = 0;
    T result = 0;

    for (s32 i = input.size() - 1; i >= inputStart; --i)
    {
        char c = input.at(i);

        switch (c)
        {
            case '0':
            case '1':
                if (shift > maxShift)
                    throw "input value too large";

                result |= ((c - '0') << shift++);
                break;

            // Skip the digit separator
            case '\'':
                break;

            default:
                throw "invalid character in binary literal";
        }
    }

    return result;
}

// Parse the unsigned numeric type T from the given string.
// Performs a range check to make sure the parsed value is in range of the data
// type T
template<typename T>
T parseValue(const QString &str)
{
    static_assert(std::is_unsigned<T>::value, "parseValue works for unsigned types only");

    if (str.toLower().startsWith(QSL("0b")))
    {
        return parseBinaryLiteral<T>(str);
    }

    bool ok;
    qulonglong val = str.toULongLong(&ok, 0);

    if (!ok)
        throw "invalid data value";

    constexpr auto maxValue = std::numeric_limits<T>::max();

    if (val > maxValue)
        throw QString("given numeric value is out or range. max=%1")
            .arg(maxValue, 0, 16);

    return val;
}

// Full specialization of parseValue for type QString
template<>
QString parseValue(const QString &str)
{
    return str;
}


static Command handle_stack_command(const QVector<PreparsedLine> &lines,
                                    int stackStartIndex, int stackEndIndex)
{
    // stack_start syntax is: stack_start [offset=0] [output=0]
    //
    // 'offset' is the offset from StackMemoryBegin in bytes. The 2 low bits
    // must be 0 (register address step is 4).
    // 'output' is the output pipe. Either 0/1 or 'command'/'data' are valid.
    //
    // The rest of the preparsed command lines must contain vme_script commands
    // and are passed on to vme_script::parse()

    assert(lines.size() > 1);

    try
    {
        Command result;
        result.type = CommandType::Stack;

        PreparsedLine cmdLine = lines.at(stackStartIndex);

        assert(cmdLine.parts.at(0) == "stack_start");

        QSet<QString> seenKeywords;

        for (auto it = cmdLine.parts.begin() + 1; it != cmdLine.parts.end(); it++)
        {
            QStringList argParts = (*it).split('=', QString::SkipEmptyParts);

            if (argParts.size() != 2)
                throw QString("invalid argument to 'stack_start': %1").arg(*it);

            const QString &keyword = argParts.at(0);
            const QString &value   = argParts.at(1);

            if (seenKeywords.contains(keyword))
                throw QString("duplicate argument to 'stack_start': %1").arg(*it);

            seenKeywords.insert(keyword);

            if (keyword == "output")
            {
                if (value == "command" || value == "cmd")
                    result.stack.outputPipe = mesytec::mvlc::CommandPipe;
                else if (value == "data")
                    result.stack.outputPipe = mesytec::mvlc::DataPipe;
                else
                {
                    try
                    {
                        u8 outputPipe = parseValue<u8>(value);

                        if (outputPipe > mvlc::DataPipe)
                            throw QString("invalid output pipe specified "
                                          "(must be 0/1 or 'command'/'data')");

                        result.stack.outputPipe = outputPipe;
                    }
                    catch (const char *) // from parseValue()
                    {
                        throw QString("invalid argument to the 'output' option of the 'stack_start' command");
                    }
                }
            }
            else if (keyword == "offset")
            {
                u16 offset = parseValue<u16>(value);

                if (offset % mesytec::mvlc::AddressIncrement)
                    throw QString("invalid stack offset address. must be divisible by 4");

                result.stack.offset = offset;
            }
            else
            {
                throw QString("unknown argument to 'stack_start': %1").arg(*it);
            }
        }

        QStringList vmeScriptLines;

        for (int lineIndex = stackStartIndex + 1;
             lineIndex < stackEndIndex && lineIndex < lines.size();
             lineIndex++)
        {
            vmeScriptLines.push_back(lines[lineIndex].trimmed);
        }

        vme_script::VMEScript vmeScript = vme_script::parse(vmeScriptLines.join("\n"));

        // adjust VMEScript line numbers
        for (auto &vmeCommand: vmeScript)
        {
            vmeCommand.lineNumber += lines.at(stackStartIndex + 1).lineNumber;
        }

        result.stack.contents = vmeScript;

        return result;
    }
    catch (const char *message)
    {
        throw ParseError(message, lines.at(0).lineNumber);
    }
    catch (const QString &e)
    {
        throw ParseError(e, lines.at(0).lineNumber);
    }
}

static Command handle_single_line_command(const PreparsedLine &line)
{
    assert(!line.parts.isEmpty());

    Command result;

    const QStringList &parts = line.parts;
    auto cmd = parts.at(0);

    try
    {
        if (cmd == "ref_word")
        {
            if (parts.size() != 2)
                throw "Usage: ref_word <ref_value>";

            result.type = CommandType::ReferenceWord;
            result.value = parseValue<u32>(parts.at(1));
        }
        else if (cmd == "read_local")
        {
            if (parts.size() != 2)
                throw "Usage: read_local <address>";

            result.type = CommandType::ReadLocal;
            result.address = parseValue<u16>(parts.at(1));

        }
        else if (cmd == "read_local_block")
        {
            if (parts.size() != 3)
                throw "Usage: read_local_block <address> <words>";

            result.type = CommandType::ReadLocalBlock;
            result.address = parseValue<u16>(parts.at(1));
            u16 words = parseValue<u16>(parts.at(2));

            if (words > mvlc::ReadLocalBlockMaxWords)
            {
                throw QString("read_local_block max words exceeded (max=%1)")
                    .arg(mvlc::ReadLocalBlockMaxWords);
            }

            result.value = words;
        }
        else if (cmd == "write_local")
        {
            if (parts.size() != 3)
                throw "Usage: write_local <address> <value>";

            result.type = CommandType::WriteLocal;
            result.address = parseValue<u32>(parts.at(1));
            result.value = parseValue<u32>(parts.at(2));
        }
        else if (cmd == "write_reset")
        {
            if (parts.size() > 1)
                throw "write_reset does not take any arguments";
            result.type = CommandType::WriteReset;
        }
    }
    catch (const char *message)
    {
        throw ParseError(message, line.lineNumber);
    }
    catch (const QString &message)
    {
        throw ParseError(message, line.lineNumber);
    }

    return result;
}

QVector<Command> parse(QTextStream &input)
{
    QVector<Command> result;

    QVector<PreparsedLine> splitLines = pre_parse(input);

    int lineIndex = 0;

    while (lineIndex < splitLines.size())
    {
        auto &sl = splitLines.at(lineIndex);

        assert(!sl.parts.isEmpty());

        // special handling for embedded stacks containing VMEScript commands
        if (sl.parts[0] == "stack_start")
        {
            int stackStartIndex = lineIndex;
            int stackEndIndex   = find_index_of_next_command(
                        "stack_end", splitLines, stackStartIndex);

            if (stackEndIndex < 0)
                throw ParseError("No matching \"stack_end\" found.", sl.lineNumber);

            assert(stackEndIndex > stackStartIndex);
            assert(stackEndIndex < splitLines.size());

            auto stackCommand = handle_stack_command(splitLines, stackStartIndex, stackEndIndex);

            result.push_back(stackCommand);
            lineIndex = stackEndIndex + 1;
        }
        else
        {
            auto cmd = handle_single_line_command(sl);
            result.push_back(cmd);
            lineIndex++;
        }
    }

    return result;
}

QVector<Command> parse(QFile *input)
{
    QTextStream stream(input);
    return parse(stream);
}

QVector<Command> parse(const QString &input)
{
    QTextStream stream(const_cast<QString *>(&input), QIODevice::ReadOnly);
    return parse(stream);
}

QVector<Command> parse(const std::string &input)
{
    auto qStr = QString::fromStdString(input);
    return parse(qStr);
}

void MVLCCommandListBuilder::addReferenceWord(u16 refValue)
{
    Command cmd;
    cmd.type = CommandType::ReferenceWord;
    cmd.value = refValue;

    m_commands.push_back(cmd);
}

void MVLCCommandListBuilder::addReadLocal(u16 address)
{
    Command cmd;
    cmd.type = CommandType::ReadLocal;
    cmd.address = address;

    m_commands.push_back(cmd);
}

void MVLCCommandListBuilder::addReadLocalBlock(u16 address, u16 words)
{
    if (words > mvlc::ReadLocalBlockMaxWords)
        throw std::runtime_error("ReadLocalBlock max words exceeded");

    Command cmd;
    cmd.type = CommandType::ReadLocalBlock;
    cmd.address = address;
    cmd.value = words;

    m_commands.push_back(cmd);
}

void MVLCCommandListBuilder::addWriteLocal(u16 address, u32 value)
{
    Command cmd;
    cmd.type = CommandType::WriteLocal;
    cmd.address = address;
    cmd.value = value;

    m_commands.push_back(cmd);
}

void MVLCCommandListBuilder::addWriteReset()
{
    Command cmd;
    cmd.type = CommandType::WriteReset;

    m_commands.push_back(cmd);
}

void MVLCCommandListBuilder::addStack(u8 outputPipe, u16 offset,
                                      const vme_script::VMEScript &contents)
{
    Command cmd;
    cmd.type = CommandType::Stack;
    cmd.stack.outputPipe = outputPipe;
    cmd.stack.offset = offset;
    cmd.stack.contents = contents;

    m_commands.push_back(std::move(cmd));
}

static const u8 DefaultOutputPipe = mesytec::mvlc::CommandPipe;
static const u8 DefaultOffset = 0;

void MVLCCommandListBuilder::addVMERead(u32 address, u8 amod, mvlc::VMEDataWidth dataWidth)
{
    if (is_block_mode(amod))
        throw std::runtime_error("Invalid address modifier for single read operation");

    vme_script::Command command;
    command.type = vme_script::CommandType::Read;
    command.address = address;
    command.addressMode = amod;
    command.dataWidth = convert_data_width(dataWidth);

    addStack(DefaultOutputPipe, DefaultOffset, { command });
}

void MVLCCommandListBuilder::addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers)
{
    if (!is_block_mode(amod))
        throw std::runtime_error("Invalid address modifier for block read operation");

    vme_script::Command command;

    switch (amod)
    {
        case vme_address_modes::BLT32:
            command.type = vme_script::CommandType::BLTFifo;
            break;

        case vme_address_modes::MBLT64:
            command.type = vme_script::CommandType::MBLTFifo;
            break;

        default:
            assert(false);
    }

    command.addressMode = amod;
    command.address = address;
    command.transfers = maxTransfers;

    addStack(DefaultOutputPipe, DefaultOffset, { command });
}

//void MVLCCommandListBuilder::add2eSST64Read(u32 address, u16 maxTransfers, Blk2eSSTRate rate)
//{
//    assert(!"not implemented");
//}

void MVLCCommandListBuilder::addVMEWrite(u32 address, u32 value, u8 amod,
                                         mvlc::VMEDataWidth dataWidth)
{
    if (is_block_mode(amod))
        throw std::runtime_error("Invalid address modifier for single write operation");

    const u32 Mask = (dataWidth == mvlc::VMEDataWidth::D16 ? 0x0000FFFF : 0xFFFFFFFF);

    vme_script::Command command;
    command.type = vme_script::CommandType::Write;
    command.address = address;
    command.value = value & Mask;
    command.addressMode = amod;
    command.dataWidth = convert_data_width(dataWidth);

    addStack(DefaultOutputPipe, DefaultOffset, { command });
}

CommandList MVLCCommandListBuilder::getCommandList() const
{
    return m_commands;
}

void MVLCCommandListBuilder::append(const MVLCCommandListBuilder &other)
{
    m_commands.append(other.getCommandList());
}

std::vector<u32> to_mvlc_buffer(const Command &cmd)
{
    using namespace mvlc::super_commands;
    using SuperCT = SuperCommandType;

    switch (cmd.type)
    {
        case CommandType::Invalid:
            break;

        case CommandType::ReferenceWord:
            return std::vector<u32>
            {
                (static_cast<u32>(SuperCT::ReferenceWord) << SuperCmdShift)
                    | (cmd.value & SuperCmdArgMask),
            };

        case CommandType::ReadLocal:
            return std::vector<u32>
            {
                (static_cast<u32>(SuperCT::ReadLocal) << SuperCmdShift)
                    | (cmd.address & SuperCmdArgMask),
            };

        case CommandType::ReadLocalBlock:
            return std::vector<u32>
            {
                (static_cast<u32>(SuperCT::ReadLocalBlock) << SuperCmdShift)
                    | (cmd.address & SuperCmdArgMask),
                cmd.value
            };

        case CommandType::WriteLocal:
            return std::vector<u32>
            {
                (static_cast<u32>(SuperCT::WriteLocal) << SuperCmdShift)
                    | (cmd.address & SuperCmdArgMask),
                cmd.value
            };

        case CommandType::WriteReset:
            return std::vector<u32>
            {
                static_cast<u32>(SuperCT::WriteReset) << SuperCmdShift,
            };

        case CommandType::Stack:
            {
                auto stackBuilder = build_mvlc_stack(cmd.stack.contents);


                auto uploadSuperCommands = mvlc::make_stack_upload_commands(
                    cmd.stack.outputPipe,
                    cmd.stack.offset,
                    stackBuilder);

                auto uploadBuffer = mvlc::make_command_buffer(uploadSuperCommands);

                std::vector<u32> result;
                result.reserve(uploadBuffer.size());

                for (u32 word: uploadBuffer)
                    result.push_back(word);

                return result;
            }
    }

    return {};
}

std::vector<u32> to_mvlc_command_buffer(const CommandList &cmdList)
{
    using namespace mvlc::super_commands;
    using SuperCT = SuperCommandType;

    std::vector<u32> result;

    result.push_back(static_cast<u32>(SuperCT::CmdBufferStart) << SuperCmdShift);
    for (const auto &cmd: cmdList)
    {
        auto cmdBuffer = to_mvlc_buffer(cmd);
        std::copy(std::begin(cmdBuffer), std::end(cmdBuffer),
                  std::back_inserter(result));
    }
    result.push_back(static_cast<u32>(SuperCT::CmdBufferEnd) << SuperCmdShift);

    return result;
}

} // end namespace script
} // end namespace mvme_mvlc
} // end namespace mesytec
