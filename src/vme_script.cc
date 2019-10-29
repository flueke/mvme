/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "vme_script.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRegExp>
#include <QRegularExpression>
#include <QTextEdit>
#include <QApplication>
#include <QThread>

#include "util.h"
#include "vme_controller.h"
#include "vmusb.h"

#include "mvlc/mvlc_vme_controller.h"


namespace vme_script
{

u8 parseAddressMode(const QString &str)
{
    if (str.compare(QSL("a16"), Qt::CaseInsensitive) == 0)
        return vme_address_modes::A16;

    if (str.compare(QSL("a24"), Qt::CaseInsensitive) == 0)
        return vme_address_modes::A24;

    if (str.compare(QSL("a32"), Qt::CaseInsensitive) == 0)
        return vme_address_modes::A32;

    throw "invalid address mode";
}

DataWidth parseDataWidth(const QString &str)
{
    if (str.compare(QSL("d16"), Qt::CaseInsensitive) == 0)
        return DataWidth::D16;

    if (str.compare(QSL("d32"), Qt::CaseInsensitive) == 0)
        return DataWidth::D32;

    throw "invalid data width";
}

uint32_t parseAddress(const QString &str)
{
    bool ok;
    uint32_t ret = str.toUInt(&ok, 0);

    if (!ok)
        throw "invalid address";

    return ret;
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
        throw QString("given numeric value is out of range. max=%1")
            .arg(maxValue, 0, 16);

    return val;
}

// Full specialization of parseValue for type QString
template<>
QString parseValue(const QString &str)
{
    return str;
}

void maybe_set_warning(Command &cmd, int lineNumber)
{
    cmd.lineNumber = lineNumber;

    switch (cmd.type)
    {
        case CommandType::SetBase:
            {
                if ((cmd.address & 0xffff) != 0)
                {
                    cmd.warning = QSL("Given base address has some of the low 16-bits set");
                }
            } break;

        default:
            break;
    }

    if (cmd.warning.isEmpty())
    {
        switch (cmd.type)
        {
            case CommandType::BLTCount:
            case CommandType::BLTFifoCount:
            case CommandType::MBLTCount:
            case CommandType::MBLTFifoCount:
                {
                    if (cmd.address >= (1 << 16))
                    {
                        cmd.warning = QSL("Given block_address exceeds 0xffff");
                    }
                } break;

            default:
                break;
        }
    }
}

Command parseRead(const QStringList &args, int lineNumber)
{
    auto usage = QSL("read <address_mode> <data_width> <address>");

    if (args.size() != 4)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = CommandType::Read;

    result.addressMode = parseAddressMode(args[1]);
    result.dataWidth = parseDataWidth(args[2]);
    result.address = parseAddress(args[3]);

    result.lineNumber = lineNumber;

    return result;
}

Command parseWrite(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1 <address_mode> <data_width> <address> <value>").arg(args[0]);

    if (args.size() != 5)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = commandType_from_string(args[0]);
    result.addressMode = parseAddressMode(args[1]);
    result.dataWidth = parseDataWidth(args[2]);
    result.address = parseAddress(args[3]);
    result.value = parseValue<u32>(args[4]);

    return result;
}

Command parseWait(const QStringList &args, int lineNumber)
{
    auto usage = QSL("wait <delay>");

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = CommandType::Wait;

    static const QRegularExpression re("^(\\d+)([[:alpha:]]*)$");

    auto match = re.match(args[1]);

    if (!match.hasMatch())
        throw "Invalid delay";

    bool ok;
    result.delay_ms = match.captured(1).toUInt(&ok);

    auto unitString = match.captured(2);

    if (unitString == QSL("s"))
        result.delay_ms *= 1000;
    else if (unitString == QSL("ns"))
        result.delay_ms /= 1000;
    else if (!unitString.isEmpty() && unitString != QSL("ms"))
        throw "Invalid delay";

    return result;
}

Command parseMarker(const QStringList &args, int lineNumber)
{
    auto usage = QSL("marker <value>");

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = CommandType::Marker;
    result.value = parseValue<u32>(args[1]);

    return result;
}

Command parseBlockTransfer(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1 <address_mode> <address> <transfer_count>").arg(args[0]);

    if (args.size() != 4)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);
    result.addressMode = parseAddressMode(args[1]);
    result.address = parseAddress(args[2]);
    result.transfers = parseValue<u32>(args[3]);

    return result;
}

Command parseBlockTransferCountRead(const QStringList &args, int lineNumber)
{
    auto usage = (QString("%1 <register_address_mode> <register_data_width> <register_address>"
                         "<count_mask> <block_address_mode> <block_address>")
                  .arg(args[0]));

    if (args.size() != 7)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);
    result.addressMode = parseAddressMode(args[1]);
    result.dataWidth = parseDataWidth(args[2]);
    result.address = parseAddress(args[3]);
    result.countMask = parseValue<u32>(args[4]);
    result.blockAddressMode = parseAddressMode(args[5]);
    result.blockAddress = parseAddress(args[6]);

    return result;
}

Command parseSetBase(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1 <address>").arg(args[0]);

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = commandType_from_string(args[0]);
    result.address = parseAddress(args[1]);

    return result;
}

Command parseResetBase(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1").arg(args[0]);

    if (args.size() != 1)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = commandType_from_string(args[0]);

    return result;
}

static const QMap<QString, u32> VMUSB_RegisterNameToAddress =
{
    { QSL("dev_src"),       DEVSrcRegister },
    { QSL("dgg_a"),         DGGARegister },
    { QSL("dgg_b"),         DGGBRegister },
    { QSL("dgg_ext"),       DGGExtended },
    { QSL("sclr_a"),        ScalerA },
    { QSL("sclr_b"),        ScalerB },
    { QSL("daq_settings"),  DAQSetRegister },
};

Command parse_VMUSB_write_reg(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1 (%2) <value>")
        .arg(args.value(0))
        .arg(VMUSB_RegisterNameToAddress.keys().join("|") + "|<regAddress>")
        ;

    if (args.size() != 3)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);

    bool conversionOk = false;
    u32 regAddress = args[1].toUInt(&conversionOk, 0);

    if (!conversionOk)
    {
        if (!VMUSB_RegisterNameToAddress.contains(args[1]))
        {
            throw ParseError(QString("Invalid VMUSB register name or address given given. Usage: %1").arg(usage),
                             lineNumber);
        }

        regAddress = VMUSB_RegisterNameToAddress.value(args[1]);
    }

    result.address = regAddress;
    result.value   = parseValue<u32>(args[2]);

    return result;
}

Command parse_VMUSB_read_reg(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1 (%2)")
        .arg(args.value(0))
        .arg(VMUSB_RegisterNameToAddress.keys().join("|") + "|<regAddress>")
        ;

    if (args.size() != 2)
    {
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);
    }

    Command result;
    result.type = commandType_from_string(args[0]);

    bool conversionOk = false;
    u32 regAddress = args[1].toUInt(&conversionOk, 0);

    if (!conversionOk)
    {
        if (!VMUSB_RegisterNameToAddress.contains(args[1]))
        {
            throw ParseError(QString("Invalid VMUSB register name given. Usage: %1").arg(usage),
                             lineNumber);
        }

        regAddress = VMUSB_RegisterNameToAddress.value(args[1]);
    }

    result.address = regAddress;

    return result;
}

Command parse_mvlc_writespecial(const QStringList &args, int lineNumber)
{
    auto usage = QSL("mvlc_writespecial ('timestamp'|'stack_triggers'|<numeric_special_value>)");

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);

    if (args[1] == "timestamp")
    {
        result.value = static_cast<u32>(MVLCSpecialWord::Timestamp);
    }
    else if (args[1] == "stack_triggers")
    {
        result.value = static_cast<u32>(MVLCSpecialWord::StackTriggers);
    }
    else
    {
        // try reading a numeric value and assign it directly
        try
        {
            result.value = parseValue<u32>(args[1]);
        }
        catch (...)
        {
            throw ParseError(QString("Could not parse type argument to mvlc_writespecial"),
                             lineNumber);
        }
    }

    return result;
}

typedef Command (*CommandParser)(const QStringList &args, int lineNumber);

static const QMap<QString, CommandParser> commandParsers =
{
    { QSL("read"),              parseRead },
    { QSL("write"),             parseWrite },
    { QSL("writeabs"),          parseWrite },
    { QSL("wait"),              parseWait },
    { QSL("marker"),            parseMarker },

    { QSL("blt"),               parseBlockTransfer },
    { QSL("bltfifo"),           parseBlockTransfer },
    { QSL("mblt"),              parseBlockTransfer },
    { QSL("mbltfifo"),          parseBlockTransfer },

    { QSL("bltcount"),          parseBlockTransferCountRead },
    { QSL("bltfifocount"),      parseBlockTransferCountRead },
    { QSL("mbltcount"),         parseBlockTransferCountRead },
    { QSL("mbltfifocount"),     parseBlockTransferCountRead },

    { QSL("setbase"),           parseSetBase },
    { QSL("resetbase"),         parseResetBase },

    { QSL("vmusb_write_reg"),    parse_VMUSB_write_reg },
    { QSL("vmusb_read_reg"),     parse_VMUSB_read_reg },

    { QSL("mvlc_writespecial"),     parse_mvlc_writespecial },
};

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

// Get rid of comment parts and empty lines and split each of the remaining
// lines into space separated parts while keeping track of the correct input
// line numbers.
static QVector<PreparsedLine> pre_parse(QTextStream &input)
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

        auto trimmed = line.trimmed();

        if (trimmed.isEmpty())
            continue;

        auto parts = trimmed.split(reWordSplit, QString::SkipEmptyParts);

        if (parts.isEmpty())
            continue;

        parts[0] = parts[0].toLower();

        result.push_back({ line, parts, lineNumber });
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

static Command handle_single_line_command(const PreparsedLine &line)
{
    assert(!line.parts.isEmpty());

    Command result;

    const QStringList &parts = line.parts;

    if (parts.at(0).at(0).isDigit() && parts.size() == 2)
    {
        /* Try to parse an address followed by a value. This is the short form
         * of a write command. */
        bool ok1;
        uint32_t v1 = parts[0].toUInt(&ok1, 0);
        uint32_t v2 = 0;

        if (!ok1)
        {
            throw ParseError(QString(QSL("Invalid short-form address \"%1\""))
                             .arg(parts[0]), line.lineNumber);
        }

        try
        {
            v2 = parseValue<u32>(parts[1]);
        }
        catch (const char *message)
        {
            throw ParseError(QString(QSL("Invalid short-form value \"%1\" (%2)"))
                             .arg(parts[1], message), line.lineNumber);
        }

        result.type = CommandType::Write;
        result.addressMode = vme_address_modes::A32;
        result.dataWidth = DataWidth::D16;
        result.address = v1;
        result.value = v2;

        maybe_set_warning(result, line.lineNumber);
        return result;
    }

    auto parseFun = commandParsers.value(parts[0].toLower(), nullptr);

    if (!parseFun)
        throw ParseError(QString(QSL("No such command \"%1\"")).arg(parts[0]), line.lineNumber);

    try
    {
        Command result = parseFun(parts, line.lineNumber);
        maybe_set_warning(result, line.lineNumber);
        return result;
    }
    catch (const char *message)
    {
        throw ParseError(message, line.lineNumber);
    }

    return result;
}

static Command handle_meta_block_command(
    const QVector<PreparsedLine> &lines,
    int blockStartIndex, int blockEndIndex)
{
    assert(lines.size() > 1);

    Command result;
    result.type = CommandType::MetaBlock;
    result.metaBlock.blockBegin = lines.at(blockStartIndex);

    std::copy(lines.begin() + blockStartIndex + 1,
              lines.begin() + blockEndIndex,
              std::back_inserter(result.metaBlock.preparsedLines));

    QStringList plainLineBuffer;

    std::transform(
        result.metaBlock.preparsedLines.begin(),
        result.metaBlock.preparsedLines.end(),
        std::back_inserter(plainLineBuffer),
        [] (const auto &preparsedLine) { return preparsedLine.line; });

    result.metaBlock.textContents = plainLineBuffer.join("\n");

    return result;
}

VMEScript parse(QFile *input, uint32_t baseAddress)
{
    QTextStream stream(input);
    return parse(stream, baseAddress);
}

VMEScript parse(const QString &input, uint32_t baseAddress)
{
    QTextStream stream(const_cast<QString *>(&input), QIODevice::ReadOnly);
    return parse(stream, baseAddress);
}

VMEScript parse(const std::string &input, uint32_t baseAddress)
{
    auto qStr = QString::fromStdString(input);
    return parse(qStr, baseAddress);
}

VMEScript parse(QTextStream &input, uint32_t baseAddress)
{
    VMEScript result;

    QVector<PreparsedLine> splitLines = pre_parse(input);

    const u32 originalBaseAddress = baseAddress;
    int lineIndex = 0;

    while (lineIndex < splitLines.size())
    {
        auto &sl = splitLines.at(lineIndex);

        assert(!sl.parts.isEmpty());

        // Handling of special meta blocks enclosed in MetaBlockBegin and
        // MetaBlockEnd
        if (sl.parts[0] == MetaBlockBegin)
        {
            int blockStartIndex = lineIndex;
            int blockEndIndex   = find_index_of_next_command(
                MetaBlockEnd, splitLines, blockStartIndex);

            if (blockEndIndex < 0)
            {
                throw ParseError(
                    QString("No matching \"%1\" found.").arg(MetaBlockEnd),
                    sl.lineNumber);
            }

            assert(blockEndIndex > blockStartIndex);
            assert(blockEndIndex < splitLines.size());

            auto metaBlockCommand = handle_meta_block_command(
                splitLines, blockStartIndex, blockEndIndex);

            result.push_back(metaBlockCommand);

            lineIndex = blockEndIndex + 1;
        }
        else
        {
            auto cmd = handle_single_line_command(sl);

            /* FIXME: CommandTypes SetBase and ResetBase are handled directly in
             * here by modifying other commands before they are pushed onto result.
             * To make warnings generated when parsing any of SetBase/ResetBase
             * available to the outside I needed to return them in the result
             * although they have already been handled in here!
             * This is confusing and will lead to errors...
             *
             * An alternative would be to store the original base address inside
             * VMEScript and implement SetBase/ResetBase in run_script().
             */

            switch (cmd.type)
            {
                case CommandType::Invalid:
                    break;

                case CommandType::SetBase:
                    {
                        baseAddress = cmd.address;
                        result.push_back(cmd);
                    } break;

                case CommandType::ResetBase:
                    {
                        baseAddress = originalBaseAddress;
                        result.push_back(cmd);
                    } break;

                default:
                    {
                        cmd = add_base_address(cmd, baseAddress);
                        result.push_back(cmd);
                    } break;
            }

            lineIndex++;
        }
    }

    return result;
}

static const QMap<CommandType, QString> commandTypeToString =
{
    { CommandType::Read,                QSL("read") },
    { CommandType::Write,               QSL("write") },
    { CommandType::WriteAbs,            QSL("writeabs") },
    { CommandType::Wait,                QSL("wait") },
    { CommandType::Marker,              QSL("marker") },
    { CommandType::BLT,                 QSL("blt") },
    { CommandType::BLTFifo,             QSL("bltfifo") },
    { CommandType::MBLT,                QSL("mblt") },
    { CommandType::MBLTFifo,            QSL("mbltfifo") },
    { CommandType::BLTCount,            QSL("bltcount") },
    { CommandType::BLTFifoCount,        QSL("bltfifocount") },
    { CommandType::MBLTCount,           QSL("mbltcount") },
    { CommandType::MBLTFifoCount,       QSL("mbltfifocount") },
    { CommandType::SetBase,             QSL("setbase") },
    { CommandType::ResetBase,           QSL("resetbase") },
    { CommandType::VMUSB_WriteRegister, QSL("vmusb_write_reg") },
    { CommandType::VMUSB_ReadRegister,  QSL("vmusb_read_reg") },
    { CommandType::MVLC_WriteSpecial,   QSL("mvlc_writespecial") },
};

QString to_string(CommandType commandType)
{
    return commandTypeToString.value(commandType, QSL("unknown"));
}

CommandType commandType_from_string(const QString &str)
{
    static bool reverseMapInitialized = false;
    static QMap<QString, CommandType> stringToCommandType;

    if (!reverseMapInitialized)
    {
        for (auto it = commandTypeToString.begin();
             it != commandTypeToString.end();
             ++it)
        {
            stringToCommandType[it.value()] = it.key();
        }
        reverseMapInitialized = true;
    }

    return stringToCommandType.value(str.toLower(), CommandType::Invalid);
}

QString to_string(u8 addressMode)
{
    static const QMap<u8, QString> addressModeToString =
    {
        { vme_address_modes::A16,         QSL("a16") },
        { vme_address_modes::A24,         QSL("a24") },
        { vme_address_modes::A32,         QSL("a32") },
    };

    return addressModeToString.value(addressMode, QSL("unknown"));
}

QString to_string(DataWidth dataWidth)
{
    static const QMap<DataWidth, QString> dataWidthToString =
    {
        { DataWidth::D16,           QSL("d16") },
        { DataWidth::D32,           QSL("d32") },
    };

    return dataWidthToString.value(dataWidth, QSL("unknown"));
}

QString to_string(MVLCSpecialWord sw)
{
    switch (sw)
    {
        case MVLCSpecialWord::Timestamp: return "Timestamp";
        case MVLCSpecialWord::StackTriggers: return "StackTriggers";
    }

    return "Unrecognized MVLC special word";
}

QString to_string(const Command &cmd)
{
    QString buffer;
    QString cmdStr = to_string(cmd.type);
    switch (cmd.type)
    {
        case CommandType::Invalid:
        case CommandType::ResetBase:
            return cmdStr;

        case CommandType::Read:
            {
                buffer = QString(QSL("%1 %2 %3 %4"))
                    .arg(cmdStr)
                    .arg(to_string(cmd.addressMode))
                    .arg(to_string(cmd.dataWidth))
                    .arg(format_hex(cmd.address));
            } break;

        case CommandType::Write:
        case CommandType::WriteAbs:
            {
                buffer = QString(QSL("%1 %2 %3 %4 %5"))
                    .arg(cmdStr)
                    .arg(to_string(cmd.addressMode))
                    .arg(to_string(cmd.dataWidth))
                    .arg(format_hex(cmd.address))
                    .arg(format_hex(cmd.value));
            } break;

        case CommandType::Wait:
            {
                buffer = QString("wait %1ms")
                    .arg(cmd.delay_ms);
            } break;

        case CommandType::Marker:
            {
                buffer = QString("marker 0x%1")
                    .arg(cmd.value, 8, 16, QChar('0'));
            } break;

        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
            {
                buffer = QString(QSL("%1 %2 %3 %4"))
                    .arg(cmdStr)
                    .arg(to_string(cmd.addressMode))
                    .arg(format_hex(cmd.address))
                    .arg(cmd.transfers);
            } break;

        case CommandType::BLTCount:
        case CommandType::BLTFifoCount:
        case CommandType::MBLTCount:
        case CommandType::MBLTFifoCount:
            {
                buffer = QString(QSL("%1 %2 %3 %4 %5 %6 %7"))
                    .arg(cmdStr)
                    .arg(to_string(cmd.addressMode))
                    .arg(to_string(cmd.dataWidth))
                    .arg(format_hex(cmd.address))
                    .arg(format_hex(cmd.countMask))
                    .arg(to_string(cmd.blockAddressMode))
                    .arg(format_hex(cmd.blockAddress));
            } break;

        case CommandType::Blk2eSST64:
            {
                assert(false);
            }

        case CommandType::SetBase:
            {
                buffer = QString(QSL("%1 %2"))
                    .arg(cmdStr)
                    .arg(format_hex(cmd.address));
            } break;

        case CommandType::VMUSB_WriteRegister:
            {
                buffer = QString(QSL("%1 %2 %3 (%4)"))
                    .arg(cmdStr)
                    .arg(format_hex(cmd.address))
                    .arg(format_hex(cmd.value))
                    .arg(getRegisterName(cmd.address));
            } break;

        case CommandType::VMUSB_ReadRegister:
            {
                buffer = QString(QSL("%1 %2 (%3)"))
                    .arg(cmdStr)
                    .arg(format_hex(cmd.address))
                    .arg(getRegisterName(cmd.address));
            } break;

        case CommandType::MVLC_WriteSpecial:
            {
                auto specialStr = to_string(static_cast<MVLCSpecialWord>(cmd.value));
                buffer = QSL("%1 %2")
                    .arg(cmdStr)
                    .arg(specialStr);
            } break;

        case CommandType::MetaBlock:
            {
                buffer = QString("meta_block with %1 lines")
                    .arg(cmd.metaBlock.preparsedLines.size());
            } break;
    }

    return buffer;
}

QString format_hex(uint32_t value)
{
    return QString("0x%1").arg(value, 8, 16, QChar('0'));
}

Command add_base_address(Command cmd, uint32_t baseAddress)
{
    switch (cmd.type)
    {
        case CommandType::Invalid:
        case CommandType::Wait:
        case CommandType::Marker:
        case CommandType::WriteAbs:
        case CommandType::SetBase:
        case CommandType::ResetBase:
        case CommandType::VMUSB_WriteRegister:
        case CommandType::VMUSB_ReadRegister:
        case CommandType::MVLC_WriteSpecial:
        case CommandType::MetaBlock:
            break;

        case CommandType::Read:
        case CommandType::Write:
        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
        case CommandType::Blk2eSST64:

            cmd.address += baseAddress;
            break;

        case CommandType::BLTCount:
        case CommandType::BLTFifoCount:
        case CommandType::MBLTCount:
        case CommandType::MBLTFifoCount:

            cmd.address += baseAddress;
            cmd.blockAddress += baseAddress;
            break;
    }
    return cmd;
}

/* Adapted from the QSyntaxHighlighter documentation. */
void SyntaxHighlighter::highlightBlock(const QString &text)
{
    static const QRegularExpression reComment("#.*$");
    static const QRegExp reMultiStart("/\\*");
    static const QRegExp reMultiEnd("\\*/");

    QTextCharFormat commentFormat;
    commentFormat.setForeground(Qt::blue);

    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
    {
        startIndex = text.indexOf(reMultiStart);
    }

    while (startIndex >= 0)
    {
        int endIndex = text.indexOf(reMultiEnd, startIndex);
        int commentLength;
        if (endIndex == -1)
        {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        }
        else
        {
            commentLength = endIndex - startIndex + reMultiEnd.matchedLength() + 3;
        }
        setFormat(startIndex, commentLength, commentFormat);
        startIndex = text.indexOf(reMultiStart, startIndex + commentLength);
    }

    QRegularExpressionMatch match;
    int index = text.indexOf(reComment, 0, &match);
    if (index >= 0)
    {
        int length = match.capturedLength();
        setFormat(index, length, commentFormat);
    }
}

ResultList run_script(VMEController *controller, const VMEScript &script,
                      LoggerFun logger, bool logEachResult)
{
    int cmdNumber = 1;
    ResultList results;

    for (auto cmd: script)
    {
        if (cmd.type != CommandType::Invalid)
        {
            if (!cmd.warning.isEmpty())
            {
                logger(QString("Warning: %1 on line %2 (cmd=%3)")
                       .arg(cmd.warning)
                       .arg(cmd.lineNumber)
                       .arg(to_string(cmd.type))
                      );
            }


#if 0
            if (auto mvlc = qobject_cast<mesytec::mvlc::MVLC_VMEController *>(controller))
            {
                qDebug() << __FUNCTION__
                    << "mvlc cmd read timeout="
                    << mvlc->getMVLCObject()->getReadTimeout(mesytec::mvlc::Pipe::Command)
                    << "mvlc cmd write timeout="
                    << mvlc->getMVLCObject()->getWriteTimeout(mesytec::mvlc::Pipe::Command);
            }
#endif

            auto tStart = QDateTime::currentDateTime();

            qDebug() << __FUNCTION__
                << tStart << "begin run_command" << cmdNumber << "of" << script.size();

            auto result = run_command(controller, cmd, logger);

            auto tEnd = QDateTime::currentDateTime();
            results.push_back(result);

            qDebug() << __FUNCTION__
                << tEnd
                << "  " << cmdNumber << "of" << script.size() << ":"
                << format_result(result)
                << "duration:" << tStart.msecsTo(tEnd) << "ms";

            if (logEachResult)
                logger(format_result(result));
        }

        ++cmdNumber;
    }

    return results;
}

Result run_command(VMEController *controller, const Command &cmd, LoggerFun logger)
{
    /*
    if (logger)
        logger(to_string(cmd));
    */

    Result result;

    result.command = cmd;

    switch (cmd.type)
    {
        case CommandType::Invalid:
            /* Note: SetBase and ResetBase have already been handled at parse time. */
        case CommandType::SetBase:
        case CommandType::ResetBase:
            break;

        case CommandType::Read:
            {
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        {
                            uint16_t value = 0;
                            result.error = controller->read16(cmd.address, &value, cmd.addressMode);
                            result.value = value;
                        } break;
                    case DataWidth::D32:
                        {
                            uint32_t value = 0;
                            result.error = controller->read32(cmd.address, &value, cmd.addressMode);
                            result.value = value;
                        } break;
                }
            } break;

        case CommandType::Write:
        case CommandType::WriteAbs:
            {
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        result.error = controller->write16(cmd.address, cmd.value, cmd.addressMode);
                        break;
                    case DataWidth::D32:
                        result.error = controller->write32(cmd.address, cmd.value, cmd.addressMode);
                        break;
                }
            } break;

        case CommandType::Wait:
            {
                QThread::msleep(cmd.delay_ms);
            } break;

        case CommandType::Marker:
            {
                result.value = cmd.value;
            } break;

        case CommandType::BLT:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    vme_address_modes::BLT32, false);
            } break;

        case CommandType::BLTFifo:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    vme_address_modes::BLT32, true);
            } break;

        case CommandType::MBLT:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    vme_address_modes::MBLT64, false);
            } break;

        case CommandType::MBLTFifo:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    vme_address_modes::MBLT64, true);
            } break;

#if 1
        /* There was no need to implement these in a generic way using the
         * VMEController interface yet. VMUSB does have direct support for
         * these types of commands (see CVMUSBReadoutList::addScriptCommand()).
         */
        case CommandType::BLTCount:
        case CommandType::BLTFifoCount:
        case CommandType::MBLTCount:
        case CommandType::MBLTFifoCount:
            {
                if (logger)
                {
                    logger(QSL("xBLT count read commands are only supported during readout."));
                }
            } break;

        case CommandType::Blk2eSST64:
            if (logger)
            {
                logger(QSL("Blk2eSST64 count read command is only supported during readout."));
            } break;

#else
        case CommandType::BLTCount:
            {
                u32 transfers = 0;
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        {
                            uint16_t value = 0;
                            controller->read16(cmd.address, &value, amod_from_AddressMode(cmd.addressMode));
                            transfers = value;
                        } break;
                    case DataWidth::D32:
                        {
                            uint32_t value = 0;
                            controller->read32(cmd.address, &value, amod_from_AddressMode(cmd.addressMode));
                            transfers = value;
                        } break;
                }

                transfers &= cmd.countMask;
                transfers >>= trailing_zeroes(cmd.countMask);

                QVector<u32> dest;
                controller->blockRead(cmd.blockAddress, transfers, &dest, amod_from_AddressMode(cmd.blockAddressMode, true), false);

            } break;

        case CommandType::BLTFifoCount:
            {
                u32 transfers = 0;
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        {
                            uint16_t value = 0;
                            controller->read16(cmd.address, &value, amod_from_AddressMode(cmd.addressMode));
                            transfers = value;
                        } break;
                    case DataWidth::D32:
                        {
                            uint32_t value = 0;
                            controller->read32(cmd.address, &value, amod_from_AddressMode(cmd.addressMode));
                            transfers = value;
                        } break;
                }

                transfers &= cmd.countMask;
                transfers >>= trailing_zeroes(cmd.countMask);

                QVector<u32> dest;
                controller->blockRead(cmd.blockAddress, transfers, &dest, amod_from_AddressMode(cmd.blockAddressMode, true), true);

            } break;

        case CommandType::MBLTCount:
            {
                u32 transfers = 0;
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        {
                            uint16_t value = 0;
                            controller->read16(cmd.address, &value, amod_from_AddressMode(cmd.addressMode));
                            transfers = value;
                        } break;
                    case DataWidth::D32:
                        {
                            uint32_t value = 0;
                            controller->read32(cmd.address, &value, amod_from_AddressMode(cmd.addressMode));
                            transfers = value;
                        } break;
                }

                transfers &= cmd.countMask;
                transfers >>= trailing_zeroes(cmd.countMask);

                QVector<u32> dest;
                controller->blockRead(cmd.blockAddress, transfers, &dest, amod_from_AddressMode(cmd.blockAddressMode, false, true), false);

            } break;

        case CommandType::MBLTFifoCount:
            {
                u32 transfers = 0;
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        {
                            uint16_t value = 0;
                            controller->read16(cmd.address, &value, amod_from_AddressMode(cmd.addressMode));
                            transfers = value;
                        } break;
                    case DataWidth::D32:
                        {
                            uint32_t value = 0;
                            controller->read32(cmd.address, &value, amod_from_AddressMode(cmd.addressMode));
                            transfers = value;
                        } break;
                }

                transfers &= cmd.countMask;
                transfers >>= trailing_zeroes(cmd.countMask);

                QVector<u32> dest;
                controller->blockRead(cmd.blockAddress, transfers, &dest, amod_from_AddressMode(cmd.blockAddressMode, false, true), true);

            } break;
#endif

        case CommandType::VMUSB_WriteRegister:
            if (auto vmusb = qobject_cast<VMUSB *>(controller))
            {
                result.error = vmusb->writeRegister(cmd.address, cmd.value);
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("VMUSB controller required"));
            } break;

        case CommandType::VMUSB_ReadRegister:
            if (auto vmusb = qobject_cast<VMUSB *>(controller))
            {
                result.value = 0u;
                result.error = vmusb->readRegister(cmd.address, &result.value);
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("VMUSB controller required"));
            } break;

        case CommandType::MVLC_WriteSpecial:
            {
                auto msg = QSL("mvlc_writespecial is not supported by vme_script::run_command().");
                result.error = VMEError(VMEError::UnsupportedCommand, msg);
                if (logger) logger(msg);
            }
            break;

        case CommandType::MetaBlock:
            break;
    }

    return result;
}

QString format_result(const Result &result)
{
    if (result.error.isError())
    {
        QString ret = QString("Error from \"%1\": %2")
            .arg(to_string(result.command))
            .arg(result.error.toString());

#if 0 // too verbose
        if (auto ec = result.error.getStdErrorCode())
        {
            ret += QString(" (std::error_code: msg=%1, value=%2, cat=%3)")
                .arg(ec.message().c_str())
                .arg(ec.value())
                .arg(ec.category().name());
        }
#endif

        return ret;
    }

    QString ret(to_string(result.command));

    switch (result.command.type)
    {
        case CommandType::Invalid:
        case CommandType::Wait:
        case CommandType::Marker:
        case CommandType::SetBase:
        case CommandType::ResetBase:
        case CommandType::MVLC_WriteSpecial:
        case CommandType::MetaBlock:
            break;

        case CommandType::Write:
        case CommandType::WriteAbs:
        case CommandType::VMUSB_WriteRegister:
            ret += QSL(", write ok");
            break;

        case CommandType::Read:
            ret += QString(" -> 0x%1, %2")
                .arg(result.value, 8, 16, QChar('0'))
                .arg(result.value)
                ;
            break;

        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
        case CommandType::BLTCount:
        case CommandType::BLTFifoCount:
        case CommandType::MBLTCount:
        case CommandType::MBLTFifoCount:
        case CommandType::Blk2eSST64:
            {
                ret += "\n";
                for (int i=0; i<result.valueVector.size(); ++i)
                {
                    ret += QString(QSL("%1: 0x%2\n"))
                        .arg(i, 2, 10, QChar(' '))
                        .arg(result.valueVector[i], 8, 16, QChar('0'));
                }
            } break;

        case CommandType::VMUSB_ReadRegister:
            ret += QSL(" -> 0x%1, %2")
                .arg(result.value, 8, 16, QChar('0'))
                .arg(result.value)
                ;
            break;
    }

    return ret;
}

Command get_first_meta_block(const VMEScript &commands)
{
    auto it = std::find_if(
        commands.begin(), commands.end(),
        [] (const vme_script::Command &cmd)
        {
            return cmd.type == vme_script::CommandType::MetaBlock;
        });

    if (it != commands.end())
        return *it;

    return {}; // return an invalid command by default
}

QString get_first_meta_block_tag(const VMEScript &commands)
{
    return get_first_meta_block(commands).metaBlock.tag();
}

}
