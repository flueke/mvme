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
#include "vme_script.h"
#include "vme_script_p.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "mvlc/mvlc_vme_controller.h"
#include "util.h"
#include "util/qt_container.h"
#include "vmusb.h"

// FIXME: the exprtk abstraction should be moved to a standalone library. also
// the a2 structure as a standalone project is useless.
#include "analysis/a2/a2_exprtk.h"

using namespace mesytec;


namespace vme_script
{

u8 parseAddressMode(const QString &str, bool acceptNumericValues)
{
    // Standard user address modes.
    if (str.compare(QSL("a16"), Qt::CaseInsensitive) == 0)
        return vme_address_modes::A16;

    if (str.compare(QSL("a24"), Qt::CaseInsensitive) == 0)
        return vme_address_modes::A24;

    if (str.compare(QSL("a32"), Qt::CaseInsensitive) == 0)
        return vme_address_modes::A32;

    // CR/CSR address space
    if (str.compare(QSL("cr"), Qt::CaseInsensitive) == 0)
        return vme_address_modes::cr;

    // Parse numeric values
    if (acceptNumericValues)
    {
        bool couldParse = false;
        u32 parsedValue = str.toUInt(&couldParse, 0);

        if (couldParse && parsedValue < std::numeric_limits<u8>::max())
            return static_cast<u8>(parsedValue);
    }

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

    // Parse the value as binary hex or decimal. If that fails and there is a
    // '.' in the string attempt to interpret it as a floating point value and
    // round the result.
    //
    // Note: To avoid confusion we do not accept octal values prefixed by 0
    // anymore. Octal will be used very rarely and users expect numbers not
    // starting with 0x to be interpreted as decimal.

    if (str.toLower().startsWith(QSL("0b")))
        return parseBinaryLiteral<T>(str);

    bool ok = false;
    qulonglong val = 0;

    if (str.toLower().startsWith(QSL("0x")))
    {
        val = str.toULongLong(&ok, 0);

        if (!ok)
            throw QSL("invalid hex value");
    }
    else if (!str.contains('.'))
    {
        val = str.toULongLong(&ok, 10);

        if (!ok)
            throw QSL("invalid decimal value");
    }
    else
    {
        // Try parsing as a floating point value.
        auto fval = std::round(str.toFloat(&ok));

        if (!ok)
            throw QSL("invalid floating point value");

        if (fval < 0.0)
            throw QSL("given numeric value is negative");

        val = fval;
    }

    assert(ok);

    constexpr auto maxValue = std::numeric_limits<T>::max();

    if (val > maxValue)
        throw QSL("given numeric value is out of range. max=%1").arg(maxValue, 0, 16);

    return val;
}

// Full specialization of parseValue for type QString
template<>
QString parseValue(const QString &str)
{
    return str;
}

Command parseRead(const QStringList &args, int lineNumber)
{
    auto usage = QSL("read/readabs <address_mode> <data_width> <address> ['slow'|'late'] ['fifo'|'mem']");

    if (args.size() < 4 || args.size() > 6)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    auto argIter = std::begin(args);

    Command result;
    result.type = commandType_from_string(*argIter++);
    result.addressMode = parseAddressMode(*argIter++);
    result.dataWidth = parseDataWidth(*argIter++);
    result.address = parseAddress(*argIter++);
    result.lineNumber = lineNumber;

    while (argIter != std::end(args))
    {
        auto arg = argIter->toLower();

        if (arg == "slow" || arg == "late")
            result.mvlcSlowRead = true;
        else if (arg == "fifo")
            result.mvlcFifoMode = true;
        else if (arg == "mem")
            result.mvlcFifoMode = false;
        else
            throw ParseError(QSL("Unknown argument to '%1': '%2'. Expected 'slow'|'late'|'fifo'|'mem' or no extra argument")
                .arg(to_string(result.type)).arg(arg));

        ++argIter;
    }

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
    result.lineNumber = lineNumber;

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

    result.lineNumber = lineNumber;

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
    result.lineNumber = lineNumber;

    return result;
}

Command parseBlockTransfer(const QStringList &args, int lineNumber)
{
    namespace vme_amods = vme_address_modes;

    auto usage = QString("%1 <address_mode> <address> <transfer_count>").arg(args[0]);

    if (args.size() != 4)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);

    try
    {
        // Parse non-numeric address modes only and translate them to the
        // correct address mode for the transfer type.
        auto amod = parseAddressMode(args[1], false);

        switch (result.type)
        {
        case CommandType::BLT:
        case CommandType::BLTFifo:
            if (amod == vme_amods::A24)
                amod = vme_amods::a24UserBlock;
            else if (amod == vme_amods::A32)
                amod = vme_amods::a32UserBlock;
            break;

        case CommandType::MBLT:
        case CommandType::MBLTFifo:
        case CommandType::MBLTSwapped:
        case CommandType::MBLTSwappedFifo:
            if (amod == vme_amods::A32)
                amod = vme_amods::a32UserBlock64;
            break;

        default:
            break;
        }

        result.addressMode = amod;
    }
    catch(const char *)
    {
        // Try parsing again but this time accepting raw numeric values too.
        result.addressMode = parseAddressMode(args[1], true);
    }

    switch (result.type)
    {
        case CommandType::BLT:
        case CommandType::MBLT:
        case CommandType::MBLTSwapped:
            result.mvlcFifoMode = false;
            break;
    }

    result.address = parseAddress(args[2]);
    result.transfers = parseValue<u32>(args[3]);
    result.lineNumber = lineNumber;

    return result;
}

static Blk2eSSTRate parse2esstRate(const QString &arg)
{
    if (arg == "0" || arg == "160" || arg.toLower() == "160mb")
        return Blk2eSSTRate::Rate160MB;

    if (arg == "1" || arg == "276" || arg.toLower() == "276mb")
        return Blk2eSSTRate::Rate276MB;

    if (arg == "2" || arg == "320" || arg.toLower() == "320mb")
        return Blk2eSSTRate::Rate320MB;

   throw "invalid 2eSST rate";
}

Command parse2esstTransfer(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1 <address> <rate> <transfer_count>").arg(args[0]);

    if (args.size() != 4)
        throw ParseError(QSL("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);
    result.addressMode = mesytec::mvlc::vme_amods::Blk2eSST64;
    result.address = parseAddress(args[1]);
    result.blk2eSSTRate = parse2esstRate(args[2]);
    result.transfers = parseValue<u32>(args[3]);
    result.lineNumber = lineNumber;

    switch (result.type)
    {
        case CommandType::Blk2eSST64:
        case CommandType::Blk2eSST64Swapped:
            result.mvlcFifoMode = false;
            break;
    }

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
    result.lineNumber = lineNumber;

    return result;
}

Command parseResetBase(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1").arg(args[0]);

    if (args.size() != 1)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = commandType_from_string(args[0]);
    result.lineNumber = lineNumber;

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
    result.lineNumber = lineNumber;

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
    result.lineNumber = lineNumber;

    return result;
}

Command parse_mvlc_writespecial(const QStringList &args, int lineNumber)
{
    auto usage = QSL("mvlc_writespecial ('timestamp'|'accu'|<numeric_special_value>)");

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);

    auto special = args[1].toLower();

    if (special == "timestamp")
    {
        result.value = static_cast<u32>(MVLCSpecialWord::Timestamp);
    }
    else if (special == "accu")
    {
        result.value = static_cast<u32>(MVLCSpecialWord::Accu);
    }
    else
    {
        // try reading a numeric value and assign it directly
        try
        {
            result.value = parseValue<u32>(special);
        }
        catch (...)
        {
            throw ParseError(
                QSL("Could not parse type argument '%' to mvlc_writespecial").arg(special),
                lineNumber);
        }
    }

    result.lineNumber = lineNumber;

    return result;
}

Command parse_write_float_word(const QStringList &args, int lineNumber)
{
    auto usage = QSL("write_float_word <address_mode> <address> <part> <value>");

    if (args.size() != 5)
        throw ParseError(QSL("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = CommandType::Write;
    result.addressMode = parseAddressMode(args[1]);
    result.address = parseAddress(args[2]);
    result.dataWidth = DataWidth::D16;

    unsigned part = 0;

    {
        const QString &partStr = args[3].toLower();

        if (partStr == "lower" || partStr == "0")
            part = 0;
        else if (partStr == "upper" || partStr == "1")
            part = 1;
        else
        {
            throw ParseError(
                QSL("Invalid float part specification '%1'."
                    " Valid values: 'lower', 'upper', '0', '1'").arg(partStr),
                lineNumber);
        }
    }
    assert(part == 0 || part == 1);

    const QString &floatStr = args[4];
    float floatValue = 0.0;
    bool floatOk = false;
    floatValue = floatStr.toFloat(&floatOk);

    if (!floatOk)
        throw ParseError(QSL("Could not parse '%1' as a float.").arg(floatStr),
                         lineNumber);

    u32 regValue = 0u;
    std::memcpy(&regValue, &floatValue, sizeof(regValue));

    if (part == 1)
        regValue >>= 16;
    regValue &= 0xffff;

    result.value = regValue;

    result.lineNumber = lineNumber;

    return result;
}

Command parse_print(const QStringList &args, int lineNumber)
{
    Command result = {};
    result.type = CommandType::Print;

    if (args.size() > 1)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        result.printArgs = QStringList(args.begin() + 1 , args.end());
#else
        result.printArgs = args;
        result.printArgs.pop_front();
#endif
    }

    result.lineNumber = lineNumber;

    return result;
}

Command parse_mvlc_wait(const QStringList &args, int lineNumber)
{
    auto usage = QSL("mvlc_wait <clocks>");

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);
    result.value = parseValue<u32>(args[1]);
    result.lineNumber = lineNumber;
    return result;
}

Command parse_mvlc_signal_accu(const QStringList &args, int lineNumber)
{
    auto usage = QSL("mvlc_signal_accu");

    if (args.size() != 1)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);
    result.lineNumber = lineNumber;
    return result;
}

Command parse_mvlc_mask_shift_accu(const QStringList &args, int lineNumber)
{
    auto usage = QSL("mvlc_mask_shift_accu <mask> <shift>");

    if (args.size() != 3)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);
    result.address = parseValue<u32>(args[1]);
    result.value = parseValue<u32>(args[2]);
    result.lineNumber = lineNumber;
    return result;
}

Command parse_mvlc_set_accu(const QStringList &args, int lineNumber)
{
    auto usage = QSL("mvlc_set_accu <value>");

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);
    result.value = parseValue<u32>(args[1]);
    result.lineNumber = lineNumber;
    return result;
}

Command parse_mvlc_read_to_accu(const QStringList &args, int lineNumber)
{
    // same as parseRead()
    auto usage = QSL("mvlc_read_to_accu <address_mode> <data_width> <address> ['slow'|'late']");

    if (args.size() < 4 || args.size() > 5)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = commandType_from_string(args[0]);
    result.addressMode = parseAddressMode(args[1]);
    result.dataWidth = parseDataWidth(args[2]);
    result.address = parseAddress(args[3]);

    if (args.size() == 5)
    {
        if (args[4].toLower() != "slow" && args[4].toLower() != "late")
        {
            throw ParseError(QSL("Unknown argument '%1', expected 'slow'|'late' or no argument")
                             .arg(args[4]));
        }

        result.mvlcSlowRead = true;
    }

    result.lineNumber = lineNumber;

    return result;
}

Command parse_mvlc_compare_loop_accu(const QStringList &args, int lineNumber)
{
    auto usage = QSL("mvlc_compare_loop_accu ('eq'|'lt'|'gt') <value>");

    if (args.size() != 3)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    try
    {
        Command result;
        result.type = commandType_from_string(args[0]);
        result.value = static_cast<u32>(mvlc::accu_comparator_from_string(args[1].toStdString()));
        result.address = parseValue<u32>(args[2]);
        result.lineNumber = lineNumber;
        return result;
    } catch (const std::runtime_error &e)
    {
        throw ParseError(e.what(), lineNumber);
    }
}

Command parse_accu_set(const QStringList &args, int lineNumber)
{
    auto usage = QSL("%1 <value>").arg(args[0]);

    if (args.size() != 2)
        throw ParseError(QSL("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    try
    {
        Command result;
        result.type = CommandType::Accu_Set;
        result.value = parseValue<u32>(args[1]);
        result.lineNumber = lineNumber;
        return result;
    } catch (const std::runtime_error &e)
    {
        throw ParseError(e.what(), lineNumber);
    }
}

Command parse_accu_mask_and_rotate(const QStringList &args, int lineNumber)
{
    auto usage = QSL("%1 <mask> <rotate_amount>").arg(args[0]);

    if (args.size() != 3)
        throw ParseError(QSL("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    try
    {
        Command result;
        result.type = CommandType::Accu_MaskAndRotate;
        result.accuMask = parseValue<u32>(args[1]);
        result.accuRotate = parseValue<u32>(args[2]);
        result.lineNumber = lineNumber;
        return result;
    } catch (const std::runtime_error &e)
    {
        throw ParseError(e.what(), lineNumber);
    }
}

AccuTestOp accu_test_op_from_string(const QString &s_)
{
    auto s = s_.toLower();

    if (s == "==" || s == "eq" || s == "=")
        return AccuTestOp::EQ;

    if (s == "!=" || s == "neq")
        return AccuTestOp::NEQ;

    if (s == "<" || s == "lt")
        return AccuTestOp::LT;

    if (s == "<=" || s == "lte")
        return AccuTestOp::LTE;

    if (s == ">" || s == "gt")
        return AccuTestOp::GT;

    if (s == ">=" || s == "gte")
        return AccuTestOp::GTE;

    throw std::runtime_error("invalid comparison operator");
}

Command parse_accu_test(const QStringList &args, int lineNumber)
{
    auto usage = QSL("%1 <compare_op> <compare_value> <message>").arg(args[0]);

    if (args.size() != 4)
        throw ParseError(QSL("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    try
    {
        Command result;
        result.type = CommandType::Accu_Test;
        result.accuTestOp = accu_test_op_from_string(args[1]);
        result.accuTestValue = parseValue<u32>(args[2]);
        result.accuTestMessage = args[3];
        result.lineNumber = lineNumber;
        return result;
    } catch (const std::runtime_error &e)
    {
        throw ParseError(e.what(), lineNumber);
    }
}

Command parse_mvme_require_version(const QStringList &args, int lineNumber)
{
    auto usage = QSL("%1 <min_version>").arg(args[0]);

    if (args.size() != 2)
        throw ParseError(QSL("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = CommandType::MvmeRequireVersion;
    result.stringData = args[1];
    //if (args.size() >= 3) // optional <message> arg
    //    result.printArgs.push_back(args[2]);
    result.lineNumber = lineNumber;
    return result;
}

typedef Command (*CommandParser)(const QStringList &args, int lineNumber);

static const QMap<QString, CommandParser> commandParsers =
{
    { QSL("read"),                  parseRead },
    { QSL("readabs"),               parseRead },
    { QSL("write"),                 parseWrite },
    { QSL("writeabs"),              parseWrite },
    { QSL("wait"),                  parseWait },
    { QSL("marker"),                parseMarker },

    { QSL("blt"),                   parseBlockTransfer },
    { QSL("bltfifo"),               parseBlockTransfer },
    { QSL("mblt"),                  parseBlockTransfer },
    { QSL("mbltfifo"),              parseBlockTransfer },
    { QSL("mblts"),                 parseBlockTransfer },
    { QSL("mbltsfifo"),             parseBlockTransfer },
    { QSL("2esst"),                 parse2esstTransfer },
    { QSL("2esstfifo"),             parse2esstTransfer },
    { QSL("2esstmem"),              parse2esstTransfer },
    { QSL("2essts"),                parse2esstTransfer },
    { QSL("2esstsfifo"),            parse2esstTransfer },
    { QSL("2esstsmem"),             parse2esstTransfer },

    { QSL("setbase"),               parseSetBase },
    { QSL("resetbase"),             parseResetBase },

    { QSL("vmusb_write_reg"),       parse_VMUSB_write_reg },
    { QSL("vmusb_read_reg"),        parse_VMUSB_read_reg },

    { QSL("mvlc_writespecial"),     parse_mvlc_writespecial },

    { QSL("write_float_word"),      parse_write_float_word },

    { QSL("print"),                 parse_print },

    { QSL("mvlc_wait"),                 parse_mvlc_wait },
    { QSL("mvlc_signal_accu"),          parse_mvlc_signal_accu },
    { QSL("mvlc_mask_shift_accu"),      parse_mvlc_mask_shift_accu},
    { QSL("mvlc_set_accu"),             parse_mvlc_set_accu },
    { QSL("mvlc_read_to_accu"),         parse_mvlc_read_to_accu },
    { QSL("mvlc_compare_loop_accu"),    parse_mvlc_compare_loop_accu },

    { QSL("accu_set"),          parse_accu_set },
    { QSL("accu_mask_rotate"),  parse_accu_mask_and_rotate },
    { QSL("accu_test"),         parse_accu_test },

    { QSL("mvme_require_version"), parse_mvme_require_version },
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
                ++i; // skip over characters inside a comment block
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
                // append characters outside of a comment block to the result
                result.append(line.at(i));
                ++i;
            }
        }
    }

    return result;
}

// Also see
// https://stackoverflow.com/questions/27318631/parsing-through-a-csv-file-in-qt
// for a nice implementation of quoted string parsing.

std::pair<std::string, bool> read_atomic_variable_reference(std::istringstream &in)
{
    assert(in.peek() == '{');

    in.unsetf(std::ios_base::skipws);

    std::string result;
    char c;

    while (in >> c)
    {
        result.push_back(c);

        if (c == '}')
            return std::make_pair(result, true);
    }

    // Unterminated variable reference
    return std::make_pair(result, false);
}

std::pair<std::string, bool> read_atomic_variable_reference(const std::string &str)
{
    std::istringstream in(str);
    return read_atomic_variable_reference(in);
}

std::pair<std::string, bool> read_atomic_expression(std::istringstream &in)
{
    assert(in.peek() == '(');

    in.unsetf(std::ios_base::skipws);

    std::string result;
    char c;
    unsigned nParens = 0;

    while (in >> c)
    {
        result.push_back(c);

        switch (c)
        {
            case '(':
                ++nParens;
                break;

            case ')':
                if (--nParens == 0)
                    return std::make_pair(result, true);
                break;

            default:
                break;
        }
    }

    // Mismatched number of opening and closing parens.
    return std::make_pair(result, false);
}

std::pair<std::string, bool> read_atomic_expression(const std::string &str)
{
    std::istringstream in(str);
    return read_atomic_expression(in);
}

// Split the input line into atomic parts according to the following rules:
// - ${...} style variable references are considered parts
// - $(...) style math expressions are considered parts
// - Quoted strings are considered parts
// - Concatenations of the above form parts. The following input line is
//   considered to be a single part:
//   some"things"${that}slumber" should never be awoken"$(6 * 7)
// - If none of the above applies whitespace is used to separate parts.
std::vector<std::string> split_into_atomic_parts(const std::string &line, int lineNumber)
{
    std::vector<std::string> result;
    std::string part;
    std::istringstream in(line);
    in.unsetf(std::ios_base::skipws);


    while (!in.eof())
    {
        switch (in.peek())
        {
            case ' ':
            case '\t':
                in.get();

                if (!part.empty())
                {
                    result.push_back(part);
                    part.clear();
                }
                break;

            case '"':
                {
                    std::string quotedPart;
                    in >> std::quoted(quotedPart);
                    part.append(quotedPart);

                    // Extra handling to allow concatenating quoted and
                    // non-quoted parts if they are not separated by
                    // whitespace.
                    auto c = in.peek();
                    if (c == ' ' || c == '\t')
                    {
                        result.push_back(part);
                        part.clear();
                    }
                }
                break;

            case '$':
                {
                    part.push_back(in.get());

                    switch (in.peek())
                    {
                        case '(':
                            {
                                auto rr = read_atomic_expression(in);
                                part += rr.first;
                                if (!rr.second)
                                    throw ParseError(
                                        QSL("Unterminated expression string '%1'")
                                        .arg(part.c_str()), lineNumber);
                            }
                            break;

                        case '{':
                            {
                                auto rr = read_atomic_variable_reference(in);
                                part += rr.first;
                                if (!rr.second)
                                    throw ParseError(
                                        QSL("Unterminated variable reference '%1'")
                                        .arg(part.c_str()), lineNumber);
                            }
                            break;

                        default:
                            // Neither variable nor expression. Do nothing and
                            // let the default case handle it on the next
                            // iteration.
                            continue;
                    }


                }
                break;

            default:
                {
                    // Need to do an explicit check for the EOF condition as
                    // both peek() and get() can return an EOF even if in.eof()
                    // is not true. The reason is that for some stream types
                    // the EOF condition can only be determined when actually
                    // performing the read.
                    auto c = in.get();
                    if (c != std::istringstream::traits_type::eof())
                        part.push_back(c);
                }
        }
    }

    if (!part.empty())
        result.push_back(part);

    return result;
}

QString expand_variables(const QString &qline, const SymbolTables &symtabs, s32 lineNumber)
{
    QString result;
    auto line = qline.toStdString();
    std::istringstream in(line);
    in.unsetf(std::ios_base::skipws);
    char c;
    enum State { OutsideVar, InsideVar };
    State state = OutsideVar;
    QString varName;

    while (in >> c)
    {
        switch (state)
        {
            case OutsideVar:
                if (c == '$' && in.peek() == '{')
                {
                    in.get();
                    state = InsideVar;
                }
                else
                    result.push_back(c);

                break;

            case InsideVar:
                if (c == '}')
                {
                    if (auto var = lookup_variable(varName, symtabs))
                        result += var.value;
                    else
                        throw ParseError(QSL("Undefined variable '%1'")
                                         .arg(varName), lineNumber);
                    state = OutsideVar;
                    varName.clear();
                }
                else
                    varName.push_back(c);

                break;
        }
    }

    if (state == InsideVar)
    {
        throw ParseError(QSL("Unterminated variable reference '${%1'")
                         .arg(varName), lineNumber);
    }

    return result;
}

void expand_variables(PreparsedLine &preparsed, const SymbolTables &symtabs)
{
    for (auto &part: preparsed.parts)
        part = expand_variables(part, symtabs, preparsed.lineNumber);
}

QSet<QString> collect_variable_references(const QString &qline, s32 lineNumber)
{
    QSet<QString> result;
    auto line = qline.toStdString();
    std::istringstream in(line);
    in.unsetf(std::ios_base::skipws);
    char c;
    enum State { OutsideVar, InsideVar };
    State state = OutsideVar;
    QString varName;

    while (in >> c)
    {
        switch (state)
        {
            case OutsideVar:
                if (c == '$' && in.peek() == '{')
                {
                    in.get();
                    state = InsideVar;
                }

                break;

            case InsideVar:
                if (c == '}')
                {
                    result.insert(varName);
                    state = OutsideVar;
                    varName.clear();
                }
                else
                    varName.push_back(c);

                break;
        }
    }

    if (state == InsideVar)
    {
        throw ParseError(QSL("Unterminated variable reference '${%1'")
                         .arg(varName), lineNumber);
    }

    return result;
}

void collect_variable_references(PreparsedLine &preparsed)
{
    preparsed.varRefs = {};

    for (auto &part: preparsed.parts)
        preparsed.varRefs.unite(collect_variable_references(part, preparsed.lineNumber));
}

QSet<QString> collect_variable_references(const QString &input)
{
    QSet<QString> result;

    auto pp = pre_parse(input);
    for (const auto &preparsed: pp)
        result.unite(preparsed.varRefs);

    return result;
}

QSet<QString> collect_variable_references(QTextStream &input)
{
    QSet<QString> result;

    auto pp = pre_parse(input);
    for (const auto &preparsed: pp)
        result.unite(preparsed.varRefs);

    return result;
}

QVector<PreparsedLine> pre_parse(const QString &input)
{
    QTextStream stream(const_cast<QString *>(&input), QIODevice::ReadOnly);
    return pre_parse(stream);
}

QStringList split_into_atomic_parts_q(const QString &str, int lineNumber)
{
    auto stlParts = split_into_atomic_parts(str.toStdString(), lineNumber);

    if (stlParts.empty())
        return {};

    auto parts = to_qstrlist_from_std(stlParts);

    return parts;
}

// Pre-parse step for a single line that's not part of a multiline comment
// (passing the result of handle_multiline_comment() is ok).
// Returns the atomic parts contained in the input line. May return an empty
// list if the input is empty after handling single line ('#') comments.
PreparsedLine pre_parse_single_line(QString line, int lineNumber)
{
    int startOfComment = line.indexOf('#');

    if (startOfComment >= 0)
        line.resize(startOfComment);

    auto trimmed = line.trimmed();

    if (trimmed.isEmpty())
        return {};

    auto stlParts = split_into_atomic_parts(trimmed.toStdString(), lineNumber);

    if (stlParts.empty())
        return {};

    auto parts = to_qstrlist_from_std(stlParts);

    PreparsedLine ppl{ line, parts, lineNumber, {} };
    return ppl;
}

// Get rid of comment parts and empty lines and split each of the remaining
// lines into space separated (quoted string) parts while keeping track of the
// correct input line numbers.
QVector<PreparsedLine> pre_parse(QTextStream &input)
{
    QVector<PreparsedLine> result;
    int lineNumber = 0;
    bool in_multiline_comment = false;

    while (true)
    {
        auto line = input.readLine();
        ++lineNumber;

        if (line.isNull())
            break;

        line = handle_multiline_comment(line, in_multiline_comment);
        auto ppl = pre_parse_single_line(line, lineNumber);

        if (ppl.parts.isEmpty())
            continue;

        collect_variable_references(ppl);

        result.push_back(ppl);
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
        uint32_t addr = 0u, val = 0u;

        // address
        try
        {
            addr = parseValue<u32>(parts[0]);
        }
        catch (const QString &message)
        {
            throw ParseError(QString(QSL("Invalid short-form address \"%1\" (%2)"))
                             .arg(parts[0], message), line.lineNumber);
        }
        catch (const char *message)
        {
            throw ParseError(QString(QSL("Invalid short-form address \"%1\" (%2)"))
                             .arg(parts[0], message), line.lineNumber);
        }

        // value
        try
        {
            val = parseValue<u32>(parts[1]);
        }
        catch (const QString &message)
        {
            throw ParseError(QString(QSL("Invalid short-form value \"%1\" (%2)"))
                             .arg(parts[1], message), line.lineNumber);
        }
        catch (const char *message)
        {
            throw ParseError(QString(QSL("Invalid short-form value \"%1\" (%2)"))
                             .arg(parts[1], message), line.lineNumber);
        }

        result.type = CommandType::Write;
        result.addressMode = vme_address_modes::A32;
        result.dataWidth = DataWidth::D16;
        result.address = addr;
        result.value = val;

        return result;
    }

    auto parseFun = commandParsers.value(parts[0].toLower(), nullptr);

    if (!parseFun)
        throw ParseError(QString(QSL("No such command \"%1\"")).arg(parts[0]), line.lineNumber);

    try
    {
        Command result = parseFun(parts, line.lineNumber);
        return result;
    }
    catch (const QString &message)
    {
        throw ParseError(message, line.lineNumber);
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

// TODO: test and use this function in handle_mvlc_custom_command() and
#if 0
// Parses keyword arguments used by block read commands, e.g.
// "mvlc_custom_begin output_words=7" returns {{"output_words", "7"}}
// "mvlc_stack_begin suppress_output" return {{"suppress_output", ""}}
static std::vector<std::pair<QString, QString>> parse_keyword_arguments(const PreparsedLine &cmdLine)
{
    std::vector<std::pair<QString, QString>> result;

    for (auto it = cmdLine.parts.begin() + 1; it != cmdLine.parts.end(); it++)
    {
        if (it->contains('='))
        {
            QStringList argParts = (*it).split('=', QString::SkipEmptyParts);
            result.emplace_back(std::make_pair(argParts.value(0, {}), argParts.value(1, {})));
        }
        else
        {
            result.emplace_back(std::make_pair(*it, QString{}));
        }
    }

    return result;
}
#endif

static Command handle_mvlc_custom_command(
    const QVector<PreparsedLine> &lines,
    int blockStartIndex, int blockEndIndex)
{
    // mvlc_custom_begin [output_words=0]
    //   u32 value 0
    //   u32 value 1
    //   ...
    // mvlc_custom_end

    assert(lines.size() > 1);

    Command result;
    result.type = CommandType::MVLC_Custom;

    // parse first line arguments
    PreparsedLine cmdLine = lines.at(blockStartIndex);

    assert(cmdLine.parts.at(0) == MVLC_CustomBegin);

    for (auto it = cmdLine.parts.begin() + 1; it != cmdLine.parts.end(); it++)
    {
        QStringList argParts = (*it).split('=', QString::SkipEmptyParts);

        if (argParts.size() != 2)
            throw QString("invalid argument to '%1': %2").arg(MVLC_CustomBegin).arg(*it);

        const QString &keyword = argParts.at(0);
        const QString &value   = argParts.at(1);

        if (keyword == "output_words")
        {
            result.transfers = parseValue<u32>(value);
        }
        else
        {
            throw QString("unknown argument to '%1': %2").arg(MVLC_CustomBegin).arg(*it);
        }
    }

    // read the custom stack contents (one u32 value per line)
    for (auto ppl = lines.begin() + blockStartIndex + 1;
         ppl < lines.begin() + blockEndIndex;
         ++ppl)
    {
        if (!ppl->parts.empty())
            result.mvlcCustomStack.push_back(parseValue<u32>(ppl->parts[0]));
    }

    return result;
}

static Command handle_mvlc_inline_stack(
    const QVector<PreparsedLine> &lines,
    int blockStartIndex, int blockEndIndex,
    SymbolTables &symtabs, u32 baseAddress
    )
{
    assert(lines.size() > 1);

    Command result;
    result.type = CommandType::MVLC_InlineStack;

// TODO: figure out where to store the flag or maybe even move handling of
// the flag to the output function (format_result() in vme_script_exec.cc)
#if 0
    // parse first line arguments
    PreparsedLine cmdLine = lines.at(blockStartIndex);

    assert(cmdLine.parts.at(0) == MVLC_CustomBegin);

    auto keywordArgs = parse_keyword_arguments(cmdLine);

    for (const auto &kv: keywordArgs)
    {
        if (kv.first == "suppress_output")
            result.transfers = parse
        else
            throw QString("unknown argument to '%1': %2").arg(MVLC_CustomBegin).arg(kv.first);
    }
#endif

    QStringList plainLineBuffer;

    std::transform(
        lines.begin() + blockStartIndex + 1,
        lines.begin() + blockEndIndex,
        std::back_inserter(plainLineBuffer),
        [] (const auto &preparsedLine) { return preparsedLine.line; });

    auto inlineScriptText = plainLineBuffer.join("\n");

    auto inlineCommands = parse(inlineScriptText, symtabs, baseAddress);

    for (const auto &cmd: inlineCommands)
    {
        result.mvlcInlineStack.emplace_back(std::make_shared<Command>(cmd));
    }

    return result;
}

QString evaluate_expressions(const QString &qstrPart, s32 lineNumber)
{
    QString result;
    auto part = qstrPart.toStdString();
    std::istringstream in(part);
    in.unsetf(std::ios_base::skipws);
    char c;
    enum State { OutsideExpr, InsideExpr };
    State state = OutsideExpr;
    std::string exprString;
    unsigned nParens = 0;

    while (in >> c)
    {
        switch (state)
        {
            case OutsideExpr:
                if (c == '$' && in.peek() == '(')
                {
                    // copy the first opening paren and count it
                    exprString += in.get();
                    ++nParens;
                    state = InsideExpr;
                }
                else
                    result.push_back(c);

                break;

            case InsideExpr:
                if (c == ')')
                {
                    exprString.push_back(')');

                    if (--nParens == 0)
                    {
                        try
                        {
                            a2::a2_exprtk::Expression expr(exprString);
                            expr.compile();
                            double d = expr.eval();

                            result += QString::number(d);

                            state = OutsideExpr;
                            exprString.clear();
                        }
                        catch (const a2::a2_exprtk::ParserErrorList &el)
                        {
                            a2::a2_exprtk::ParserError pe = {};
                            if (!el.errors.empty())
                                pe = *el.errors.begin();

                            throw ParseError(
                                QSL("Embedded math expression parser error: %1")
                                .arg(pe.diagnostic.c_str()),
                                lineNumber);
                        }
                        catch (const a2::a2_exprtk::SymbolError &e)
                        {
                            throw ParseError(
                                QSL("Embedded math expression symbol error (symbol name: %1)")
                                .arg(e.symbolName.c_str()),
                                lineNumber);
                        }
                    }
                }
                else
                {
                    if (c == '(')
                        ++nParens;
                    exprString.push_back(c);
                }

                break;
        }
    }

    if (state == InsideExpr)
    {
        throw ParseError(QSL("Unterminated expression string '$(%1'")
                         .arg(exprString.c_str()), lineNumber);
    }

    return result;
}

void evaluate_expressions(PreparsedLine &preparsed)
{
    auto eval = [&preparsed] (QString &part)
    {
        part = evaluate_expressions(part, preparsed.lineNumber);
    };

    std::for_each(preparsed.parts.begin(), preparsed.parts.end(), eval);
}

// Overloads without SymbolTables arguments. These will create a single symbol
// table for internal use. Symbols set from within the script are not
// accessible from the outside.
VMEScript parse(QFile *input, uint32_t baseAddress, const QString &sourceId)
{
    SymbolTables symtabs;
    return parse(input, symtabs, baseAddress, sourceId);
}

VMEScript parse(const QString &input, uint32_t baseAddress, const QString &sourceId)
{
    SymbolTables symtabs;
    return parse(input, symtabs, baseAddress, sourceId);
}

VMEScript parse(const std::string &input, uint32_t baseAddress, const QString &sourceId)
{
    SymbolTables symtabs;
    return parse(input, symtabs, baseAddress, sourceId);
}

VMEScript parse(QTextStream &input, uint32_t baseAddress, const QString &sourceId)
{
    SymbolTables symtabs;
    return parse(input, symtabs, baseAddress, sourceId);
}

// Overloads taking a SymbolTables instances.
// Variables set from within the script are written to the first symbol table
// symtabs[0]. If symtabs is empty a single fresh SymbolTable will be created
// and added to symtabs.

VMEScript parse(QFile *input, SymbolTables &symtabs, uint32_t baseAddress, const QString &sourceId)
{
    QTextStream stream(input);
    return parse(stream, symtabs, baseAddress, sourceId);
}

VMEScript parse(const QString &input, SymbolTables &symtabs, uint32_t baseAddress, const QString &sourceId)
{
    QTextStream stream(const_cast<QString *>(&input), QIODevice::ReadOnly);
    return parse(stream, symtabs, baseAddress, sourceId);
}

VMEScript parse(const std::string &input, SymbolTables &symtabs, uint32_t baseAddress, const QString &sourceId)
{
    auto qStr = QString::fromStdString(input);
    return parse(qStr, symtabs, baseAddress, sourceId);
}

VMEScript parse(
    QTextStream &input,
    SymbolTables &symtabs,
    uint32_t baseAddress,
    const QString &sourceId)
{
    int lineIndex = 0;

    try
    {
        if (symtabs.isEmpty())
            symtabs.push_back(SymbolTable{});

        VMEScript result;

        QVector<PreparsedLine> splitLines = pre_parse(input);

        const u32 originalBaseAddress = baseAddress;

        while (lineIndex < splitLines.size())
        {
            auto &preparsed = splitLines[lineIndex];

            assert(!preparsed.parts.isEmpty());

            // Handling of special meta blocks enclosed in MetaBlockBegin and
            // MetaBlockEnd. These can span multiple lines.
            // Variable references are not replaced within meta blocks.
            if (preparsed.parts[0] == MetaBlockBegin)
            {
                int blockStartIndex = lineIndex;
                int blockEndIndex = find_index_of_next_command(
                    MetaBlockEnd, splitLines, blockStartIndex);

                if (blockEndIndex < 0)
                {
                    throw ParseError(
                        QString("No matching \"%1\" found.").arg(MetaBlockEnd),
                        preparsed.lineNumber);
                }

                assert(blockEndIndex > blockStartIndex);
                assert(blockEndIndex < splitLines.size());

                auto metaBlockCommand = handle_meta_block_command(
                    splitLines, blockStartIndex, blockEndIndex);

                result.push_back(std::move(metaBlockCommand));

                lineIndex = blockEndIndex + 1;
            }
            // Special blocks for inserting custom data into MVLC readout stacks.
            else if (preparsed.parts[0] == MVLC_CustomBegin)
            {
                int blockStartIndex = lineIndex;
                int blockEndIndex = find_index_of_next_command(
                    MVLC_CustomEnd, splitLines, blockStartIndex);

                if (blockEndIndex < 0)
                {
                    throw ParseError(
                        QString("No matching \"%1\" found.").arg(MVLC_CustomEnd),
                        preparsed.lineNumber);
                }

                assert(blockEndIndex > blockStartIndex);
                assert(blockEndIndex < splitLines.size());

                auto customCommand = handle_mvlc_custom_command(
                    splitLines, blockStartIndex, blockEndIndex);

                result.push_back(std::move(customCommand));

                lineIndex = blockEndIndex + 1;
            }
            // MVLC inline stack blocks. This is mostly for debugging purposes.
            else if (preparsed.parts[0] == MVLC_StackBegin)
            {
                int blockStartIndex = lineIndex;
                int blockEndIndex = find_index_of_next_command(
                    MVLC_StackEnd, splitLines, blockStartIndex);

                if (blockEndIndex < 0)
                {
                    throw ParseError(
                        QString("No matching \"%1\" found.").arg(MVLC_StackEnd),
                        preparsed.lineNumber);
                }

                assert(blockEndIndex > blockStartIndex);
                assert(blockEndIndex < splitLines.size());

                auto inlineStack = handle_mvlc_inline_stack(
                    splitLines, blockStartIndex, blockEndIndex,
                    symtabs, baseAddress);

                result.push_back(std::move(inlineStack));

                lineIndex = blockEndIndex + 1;
            }
            else // Not a block
            {
                expand_variables(preparsed, symtabs);
                evaluate_expressions(preparsed);

                // Lowercase the first part (the command name or for the shortcut form
                // of the write command the address value).
                preparsed.parts[0] = preparsed.parts[0].toLower();

                // To allow parsing of full command lines which were
                // expanded from a single variable a second parsing pass has to happen.
                // Example: set ${myvar} "mbltfifo a32 0x4321 12345"
                //          -> preparsed_parts: ("mbltfifo a32 0x4321 12345")
                // This string has to be split into atomics parts again.
                //
                // Do not perform this step for commands taking a fixed number
                // of arguments where one is a string potentially containing
                // quotes. This is the big drawback of performing double
                // evaluation. Maybe need to introduce single quoted strings
                // which are not double evaluated.
                if (preparsed.parts[0] != "set" && preparsed.parts[0] != "accu_test")
                {
                    //qDebug() << "preparsed.parts" << preparsed.parts;
                    QStringList reparsedParts;
                    for (auto &part: preparsed.parts)
                        reparsedParts.append(split_into_atomic_parts_q(part, preparsed.lineNumber));
                    preparsed.parts = reparsedParts;
                    //qDebug() << "re-preparsed.parts (before expansions)" << preparsed.parts;
                    expand_variables(preparsed, symtabs);
                    evaluate_expressions(preparsed);
                    //qDebug() << "re-preparsed.part (after expansions)" << preparsed.parts;
                }

                if (preparsed.parts[0] == "set")
                {
                    if (preparsed.parts.size() != 3)
                    {
                        throw ParseError(
                            QString("Invalid arguments to 'set' command. Usage: set <var> <value>."),
                            preparsed.lineNumber);
                    }

                    const auto &varName = preparsed.parts[1];
                    const auto &varValue = preparsed.parts[2];

                    // Set the variable in the first/innermost symbol table.
                    symtabs[0][varName] = Variable{varValue, preparsed.lineNumber};
                }
                else
                {
                    auto cmd = handle_single_line_command(preparsed);

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
                            result.push_back(std::move(cmd));
                        } break;

                        case CommandType::ResetBase:
                        {
                            baseAddress = originalBaseAddress;
                            result.push_back(std::move(cmd));
                        } break;

                        default:
                        {
                            cmd = add_base_address(cmd, baseAddress);
                            result.push_back(std::move(cmd));
                        } break;
                    }
                }

                lineIndex++;
            }
        }

        for (auto &cmd: result)
            cmd.source = sourceId;

        return result;
    }
    catch (const QString &err)
    {
        throw ParseError(err, lineIndex);
    }
}

static const QMultiMap<CommandType, QString> commandTypeToString =
{
    { CommandType::Read,                    QSL("read") },
    { CommandType::ReadAbs,                 QSL("readabs") },
    { CommandType::Write,                   QSL("write") },
    { CommandType::WriteAbs,                QSL("writeabs") },
    { CommandType::Wait,                    QSL("wait") },
    { CommandType::Marker,                  QSL("marker") },
    { CommandType::BLT,                     QSL("blt") },
    { CommandType::BLTFifo,                 QSL("bltfifo") },
    { CommandType::MBLT,                    QSL("mblt") },
    { CommandType::MBLTFifo,                QSL("mbltfifo") },
    { CommandType::MBLTSwapped,             QSL("mblts") },
    { CommandType::MBLTSwappedFifo,         QSL("mbltsfifo") },
    { CommandType::Blk2eSST64Fifo,          QSL("2esst") },
    { CommandType::Blk2eSST64Fifo,          QSL("2esstfifo") },
    { CommandType::Blk2eSST64,              QSL("2esstmem") },
    { CommandType::Blk2eSST64SwappedFifo,   QSL("2essts") },
    { CommandType::Blk2eSST64SwappedFifo,   QSL("2esstsfifo") },
    { CommandType::Blk2eSST64Swapped,       QSL("2esstsmem") },
    { CommandType::SetBase,                 QSL("setbase") },
    { CommandType::ResetBase,               QSL("resetbase") },
    { CommandType::VMUSB_WriteRegister,     QSL("vmusb_write_reg") },
    { CommandType::VMUSB_ReadRegister,      QSL("vmusb_read_reg") },
    { CommandType::MVLC_WriteSpecial,       QSL("mvlc_writespecial") },
    { CommandType::MetaBlock,               QSL("meta_block") },
    { CommandType::SetVariable,             QSL("set_variable") },
    { CommandType::Print,                   QSL("print") },
    { CommandType::MVLC_Wait,               QSL("mvlc_wait") },
    { CommandType::MVLC_SignalAccu,         QSL("mvlc_signal_accu") },
    { CommandType::MVLC_MaskShiftAccu,      QSL("mvlc_mask_shift_accu") },
    { CommandType::MVLC_SetAccu,            QSL("mvlc_set_accu") },
    { CommandType::MVLC_ReadToAccu,         QSL("mvlc_read_to_accu") },
    { CommandType::MVLC_CompareLoopAccu,    QSL("mvlc_compare_loop_accu") },
    { CommandType::MVLC_Custom,             QSL("mvlc_custom") },
    { CommandType::MVLC_InlineStack,        QSL("mvlc_stack") },

    { CommandType::Accu_Set,                QSL("accu_set") },
    { CommandType::Accu_MaskAndRotate,      QSL("accu_mask_rotate") },
    { CommandType::Accu_Test,               QSL("accu_test") },
    { CommandType::MvmeRequireVersion,      QSL("mvme_require_version") },
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

QString amod_to_string(u8 addressMode, bool pretty)
{
    static const QMap<u8, QString> addressModeToString =
    {
        { vme_address_modes::a16User,         QSL("a16") },
        { vme_address_modes::a16Priv,         QSL("a16") },

        { vme_address_modes::a24UserData    , QSL("a24") },
        { vme_address_modes::a24UserProgram , QSL("a24") },
        { vme_address_modes::a24UserBlock   , QSL("a24") },

        { vme_address_modes::a24PrivData    , QSL("a24") },
        { vme_address_modes::a24PrivProgram , QSL("a24") },
        { vme_address_modes::a24PrivBlock   , QSL("a24") },

        { vme_address_modes::a32UserData    , QSL("a32") },
        { vme_address_modes::a32UserProgram , QSL("a32") },
        { vme_address_modes::a32UserBlock   , QSL("a32") },
        { vme_address_modes::a32UserBlock64 , QSL("a32") },

        { vme_address_modes::a32PrivData    , QSL("a32") },
        { vme_address_modes::a32PrivProgram , QSL("a32") },
        { vme_address_modes::a32PrivBlock   , QSL("a32") },
        { vme_address_modes::a32PrivBlock64 , QSL("a32") },

        { vme_address_modes::cr             , QSL("cr") },
    };

    auto result = addressModeToString.value(addressMode);
    if (pretty)
    {
        // When pretty printing the resulting string is not parseable as a vme
        // script command.
        if (!result.isEmpty())
            result += " ";
        result += QSL("(amod=0x%1)").arg(static_cast<u32>(addressMode), 2, 16, QLatin1Char('0'));
    }
    return result;
}

QString to_qstring(DataWidth dataWidth)
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
        case MVLCSpecialWord::Accu: return "Accu";
    }

    return "Unrecognized MVLC special word";
}

QString to_string(const Blk2eSSTRate &rate)
{
    switch (rate)
    {
        case Blk2eSSTRate::Rate160MB:
            return "160MB";
        case Blk2eSSTRate::Rate276MB:
            return "276MB";
        case Blk2eSSTRate::Rate320MB:
            return "320MB";
    }

    return "Unknown 2eSST transfer rate";
}

QString to_string(const Command &cmd, bool pretty)
{
    QString buffer;
    QString cmdStr = to_string(cmd.type);

    switch (cmd.type)
    {
        case CommandType::Invalid:
        case CommandType::ResetBase:
            return cmdStr;

        case CommandType::Read:
        case CommandType::ReadAbs:
        case CommandType::MVLC_ReadToAccu:
            {
                buffer = QString(QSL("%1 %2 %3 %4"))
                    .arg(cmdStr)
                    .arg(amod_to_string(cmd.addressMode, pretty))
                    .arg(to_qstring(cmd.dataWidth))
                    .arg(format_hex(cmd.address));
                if (cmd.mvlcSlowRead)
                    buffer += " late";
                if (!cmd.mvlcFifoMode)
                    buffer += " mem";
            } break;

        case CommandType::Write:
        case CommandType::WriteAbs:
            {
                buffer = QString(QSL("%1 %2 %3 %4 %5"))
                    .arg(cmdStr)
                    .arg(amod_to_string(cmd.addressMode, pretty))
                    .arg(to_qstring(cmd.dataWidth))
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
        case CommandType::MBLTSwapped:
        case CommandType::MBLTSwappedFifo:
            {
                buffer = QString(QSL("%1 %2 %3 %4"))
                    .arg(cmdStr)
                    .arg(amod_to_string(cmd.addressMode, pretty))
                    .arg(format_hex(cmd.address))
                    .arg(cmd.transfers);
            } break;

        case CommandType::Blk2eSST64:
        case CommandType::Blk2eSST64Fifo:
        case CommandType::Blk2eSST64Swapped:
        case CommandType::Blk2eSST64SwappedFifo:
            {
                buffer = QString(QSL("%1 %2 %3 %4"))
                    .arg(cmdStr)
                    .arg(format_hex(cmd.address))
                    .arg(to_string(cmd.blk2eSSTRate))
                    .arg(cmd.transfers);
            } break;

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

        case CommandType::SetVariable:
            break;

        case CommandType::Print:
            buffer = cmdStr + cmd.printArgs.join(" ");
            break;

        case CommandType::MVLC_Custom:
            buffer = QString(QSL("%1 with %2 lines"))
                .arg(cmdStr)
                .arg(cmd.mvlcCustomStack.size());
            break;

        case CommandType::MVLC_Wait:
            buffer = QSL("%1 %2")
                .arg(cmdStr)
                .arg(cmd.value);
            break;

        case CommandType::MVLC_SignalAccu:
            buffer = QSL("%1").arg(cmdStr);
            break;

        case CommandType::MVLC_MaskShiftAccu:
            buffer = QSL("%1 %2 %3")
                .arg(cmdStr)
                .arg(format_hex(cmd.address)) // mask
                .arg(cmd.value); // shift
            break;

        case CommandType::MVLC_SetAccu:
            buffer = QSL("%1 %2")
                .arg(cmdStr)
                .arg(cmd.value);
            break;

        case CommandType::MVLC_CompareLoopAccu:
            buffer = QSL("%1 %2 %3")
                .arg(cmdStr)
                .arg(mvlc::accu_comparator_to_string(
                        static_cast<mvlc::AccuComparator>(cmd.value)).c_str())
                .arg(cmd.address);
            break;

        case CommandType::MVLC_InlineStack:
            buffer = QString(QSL("%1 with %2 commands"))
                .arg(cmdStr)
                .arg(cmd.mvlcInlineStack.size());
            break;

        case CommandType::Accu_Set:
            buffer = QSL("%1 accu=0x%2 (%3 dec)")
                .arg(cmdStr)
                .arg(cmd.value, 8, 16, QLatin1Char('0'))
                .arg(cmd.value)
                ;
            break;
        case CommandType::Accu_MaskAndRotate:
            buffer = QSL("%1 mask=0x%2 rotate=%3")
                .arg(cmdStr)
                .arg(cmd.accuMask, 8, 16, QLatin1Char('0'))
                .arg(cmd.accuRotate);
            break;
        case CommandType::Accu_Test:
            buffer = QSL("%1 accu %2 0x%3 \"%4\"")
                .arg(cmdStr)
                // op value msg
                .arg(to_string(cmd.accuTestOp))
                .arg(cmd.accuTestValue, 8, 16, QLatin1Char('0'))
                .arg(cmd.accuTestMessage);
            break;

        case CommandType::MvmeRequireVersion:
            buffer = QSL("%1 %2").arg(cmdStr).arg(cmd.stringData);
            break;
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
        case CommandType::ReadAbs:
        case CommandType::WriteAbs:
        case CommandType::SetBase:
        case CommandType::ResetBase:
        case CommandType::VMUSB_WriteRegister:
        case CommandType::VMUSB_ReadRegister:
        case CommandType::MVLC_WriteSpecial:
        case CommandType::MetaBlock:
        case CommandType::SetVariable:
        case CommandType::Print:
        case CommandType::MVLC_Custom:
        case CommandType::MVLC_Wait:
        case CommandType::MVLC_SignalAccu:
        case CommandType::MVLC_MaskShiftAccu:
        case CommandType::MVLC_SetAccu:
        case CommandType::MVLC_CompareLoopAccu:
        case CommandType::MVLC_InlineStack:
        case CommandType::Accu_Set:
        case CommandType::Accu_MaskAndRotate:
        case CommandType::Accu_Test:
        case CommandType::MvmeRequireVersion:
            break;

        case CommandType::Read:
        case CommandType::Write:
        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
        case CommandType::MBLTSwapped:
        case CommandType::MBLTSwappedFifo:
        case CommandType::Blk2eSST64:
        case CommandType::Blk2eSST64Fifo:
        case CommandType::Blk2eSST64Swapped:
        case CommandType::Blk2eSST64SwappedFifo:
        case CommandType::MVLC_ReadToAccu:
            cmd.address += baseAddress;
            break;
    }
    return cmd;
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
