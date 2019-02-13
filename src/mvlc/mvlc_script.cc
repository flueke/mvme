#include "mvlc_script.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <cassert>

#include "mvlc_constants.h"
#include "mvlc_util.h"

#ifndef QSL
#define QSL(str) QStringLiteral(str)
#endif

namespace mesytec
{
namespace mvlc
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

static long find_index_of_next_command(const QString &cmd, const QVector<PreparsedLine> splitLines,
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

        PreparsedLine cmdLine = lines.at(0);

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
                if (value == "command")
                    result.stack.outputPipe = mesytec::mvlc::CommandPipe;
                else if (value == "data")
                    result.stack.outputPipe = mesytec::mvlc::DataPipe;
                else
                {
                    u8 outputPipe = parseValue<u8>(value);

                    if (outputPipe > DataPipe)
                        throw QString("invalid output pipe specified "
                                      "(must be 0/1 or 'command'/'data')");

                    result.stack.outputPipe = outputPipe;
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

void MVLCCommandListBuilder::addStack(u8 outputPipe, u16 offset, const vme_script::VMEScript &contents)
{
    Command cmd;
    cmd.type = CommandType::Stack;
    cmd.stack.outputPipe = outputPipe;
    cmd.stack.offset = offset;
    cmd.stack.contents = contents;

    m_commands.push_back(cmd);
}

static const u8 DefaultOutputPipe = mesytec::mvlc::CommandPipe;
static const u8 DefaultOffset = 0;

void MVLCCommandListBuilder::addVMERead(u32 address, AddressMode amod, VMEDataWidth dataWidth)
{
    if (is_block_amod(amod))
        throw std::runtime_error("Invalid address modifier for single read operation");

    vme_script::Command command;
    command.type = vme_script::CommandType::Read;
    command.address = address;
    command.addressMode = convert_amod(amod);
    command.dataWidth = convert_data_width(dataWidth);

    addStack(DefaultOutputPipe, DefaultOffset, { command });
}

void MVLCCommandListBuilder::addVMEBlockRead(u32 address, AddressMode amod, u16 maxTransfers)
{
    if (!is_block_amod(amod))
        throw std::runtime_error("Invalid address modifier for block read operation");

    vme_script::Command command;

    switch (amod)
    {
        case BLT32:
            command.type = vme_script::CommandType::BLTFifo;
            break;

        case MBLT64:
            command.type = vme_script::CommandType::MBLTFifo;
            break;

        default:
            assert(false);
    }

    command.addressMode = vme_script::AddressMode::A32;
    command.address = address;
    command.transfers = maxTransfers;

    addStack(DefaultOutputPipe, DefaultOffset, { command });
}

void MVLCCommandListBuilder::add2eSST64Read(u32 address, u16 maxTransfers, Blk2eSSTRate rate)
{
    assert(!"not implemented");
}

void MVLCCommandListBuilder::addVMEWrite(u32 address, u32 value, AddressMode amod,
                                         VMEDataWidth dataWidth)
{
    if (is_block_amod(amod))
        throw std::runtime_error("Invalid address modifier for single write operation");

    vme_script::Command command;
    command.type = vme_script::CommandType::Write;
    command.address = address;
    command.value = value;
    command.addressMode = convert_amod(amod);
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

QVector<u32> to_mvlc_buffer(const Command &cmd)
{
    using namespace super_commands;

    switch (cmd.type)
    {
        case CommandType::Invalid:
            break;

        case CommandType::ReferenceWord:
            return QVector<u32>
            {
                (ReferenceWord << SuperCmdShift) | (cmd.value & SuperCmdArgMask),
            };

        case CommandType::ReadLocal:
            return QVector<u32>
            {
                (ReadLocal << SuperCmdShift) | (cmd.address & SuperCmdArgMask),
            };

        case CommandType::WriteLocal:
            return QVector<u32>
            {
                (WriteLocal << SuperCmdShift) | (cmd.address & SuperCmdArgMask),
                cmd.value
            };

        case CommandType::WriteReset:
            return QVector<u32>
            {
                WriteReset << SuperCmdShift,
            };

        case CommandType::Stack:
            {
                auto uploadStack = build_upload_commands(
                    cmd.stack.contents,
                    cmd.stack.outputPipe,
                    stacks::StackMemoryBegin + cmd.stack.offset);

                QVector<u32> result;
                result.reserve(uploadStack.size());
                //result.append(commands::StackStart << CmdShift);

                for (u32 word: uploadStack)
                    result.append(word);

                //result.append(commands::StackEnd << CmdShift);

                return result;
            }
    }

    return {};
}

QVector<u32> to_mvlc_command_buffer(const CommandList &cmdList)
{
    using namespace super_commands;

    QVector<u32> result;

    result.append(CmdBufferStart << SuperCmdShift);
    for (const auto &cmd: cmdList)
    {
        result.append(to_mvlc_buffer(cmd));
    }
    result.append(CmdBufferEnd << SuperCmdShift);

    return result;
}

} // end namespace script
} // end namespace mvlc
} // end namespace mesytec
