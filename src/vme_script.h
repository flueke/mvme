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
#ifndef __VME_SCRIPT_QT_H__
#define __VME_SCRIPT_QT_H__

#include "libmvme_core_export.h"
#include "vme_controller.h"

#include <cstdint>
#include <QVector>
#include <QSyntaxHighlighter>
#include <functional>

class QFile;
class QTextStream;
class QString;

class VMEController;

namespace vme_script
{

struct PreparsedLine
{
    QString line;       // A copy of the original line
    QStringList parts;  // The line trimmed of whitespace and split at word boundaries
    u32 lineNumber;     // The original line number
};

static const QString MetaBlockBegin = "meta_block_begin";
static const QString MetaBlockEnd = "meta_block_end";
//static const QString SetVariable = "set";

struct MetaBlock
{
    // The line containing the MetaBlockBegin instruction. May be used to parse
    // additional arguments if desired.
    PreparsedLine blockBegin;

    // The contents of the meta block in the form of PreparsedLine structures.
    // Does neither contain the MetaBlockBegin nor the MetaBlockEnd lines.
    QVector<PreparsedLine> preparsedLines;

    // The original block contents as a string.
    // Note: completely empty lines are not present anymore in this variable.
    QString textContents;

    // Returns the first argument after the MetaBlockBegin keyword. This should
    // be used as a tag type to identify which kind of meta block this is.
    // The UI will use this to determine if a specialized editor should be
    // launched when editing the script. Subsystems will use this to locate
    // their meta block.
    QString tag() const
    {
        return blockBegin.parts.value(1);
    }
};

enum class CommandType
{
    Invalid,
    Read,
    Write,
    WriteAbs,
    Wait,
    Marker,

    BLT,
    BLTFifo,
    MBLT,
    MBLTFifo,
    Blk2eSST64,

    BLTCount,
    BLTFifoCount,
    MBLTCount,
    MBLTFifoCount,

    SetBase,
    ResetBase,

    VMUSB_WriteRegister,
    VMUSB_ReadRegister,

    MVLC_WriteSpecial, // type is stored in Command.value (of type MVLCSpecialWord)

    MetaBlock,
    SetVariable,
};

enum class DataWidth
{
    D16 = 1,
    D32
};

enum Blk2eSSTRate: u8
{
    Rate160MB,
    Rate276MB,
    Rate300MB,
};

enum class MVLCSpecialWord: u8
{
    Timestamp     = 0x0,
    StackTriggers = 0x1,
};

struct Command
{
    CommandType type = CommandType::Invalid;
    u8 addressMode = vme_address_modes::A32;
    DataWidth dataWidth = DataWidth::D16;
    uint32_t address = 0;
    uint32_t value = 0;
    uint32_t transfers = 0;
    uint32_t delay_ms = 0;
    uint32_t countMask = 0;
    u8 blockAddressMode = vme_address_modes::A32;
    uint32_t blockAddress = 0;
    Blk2eSSTRate blk2eSSTRate = Blk2eSSTRate::Rate160MB;

    QString warning;
    s32 lineNumber = 0;

    MetaBlock metaBlock = {};
};

LIBMVME_CORE_EXPORT QString to_string(CommandType commandType);
LIBMVME_CORE_EXPORT CommandType commandType_from_string(const QString &str);
LIBMVME_CORE_EXPORT QString to_string(u8 addressMode);
LIBMVME_CORE_EXPORT QString to_string(DataWidth dataWidth);
LIBMVME_CORE_EXPORT QString to_string(const Command &cmd);
LIBMVME_CORE_EXPORT QString format_hex(uint32_t value);

using VMEScript = QVector<Command>;

Command add_base_address(Command cmd, uint32_t baseAddress);

struct ParseError
{
    ParseError(const QString &message, int lineNumber = -1)
        : message(message)
        , lineNumber(lineNumber)
    {}

    QString what() const
    {
        if (lineNumber >= 0)
            return QString("%1 on line %2").arg(message).arg(lineNumber);
        return message;
    }

    QString toString() const { return what(); }

    QString message;
    int lineNumber;
};

struct Variable
{
    QString value;
    s32 lineNumber;

    Variable()
    {}

    Variable(const QString &v, s32 lineNumber = 0)
        : value(v)
        , lineNumber(lineNumber)
    { }

    explicit operator bool() const { return !value.isEmpty(); }
};

using SymbolTable = QMap<QString, Variable>;
using SymbolTables = QVector<SymbolTable>;

Variable lookup_variable(const QString &varName, const SymbolTables &symtabs);
QString expand_variables(const QString &line, const SymbolTables &symtabs, s32 lineNumber);
void expand_variables(PreparsedLine &preparsed, const SymbolTables &symtabs);

QString evaluate_expressions(const QString &qline, s32 lineNumber);
void evaluate_expressions(PreparsedLine &preparsed);

// These versions of the parse function use an internal symbol table. Access to
// variables defined via the 'set' command is not possible.
VMEScript LIBMVME_CORE_EXPORT parse(QFile *input, uint32_t baseAddress = 0);
VMEScript LIBMVME_CORE_EXPORT parse(const QString &input, uint32_t baseAddress = 0);
VMEScript LIBMVME_CORE_EXPORT parse(QTextStream &input, uint32_t baseAddress = 0);
VMEScript LIBMVME_CORE_EXPORT parse(const std::string &input, uint32_t baseAddress = 0);

// Versions of the parse function taking a list of SymbolTables by reference.
// The first table in the list is used as the 'script local' symbolt able. If
// the list is empty a single SymbolTable instance will be created and added.
VMEScript LIBMVME_CORE_EXPORT parse(QFile *input, SymbolTables &symtabs,
                                    uint32_t baseAddress = 0);

VMEScript LIBMVME_CORE_EXPORT parse(const QString &input, SymbolTables &symtabs,
                                    uint32_t baseAddress = 0);

VMEScript LIBMVME_CORE_EXPORT parse(QTextStream &input, SymbolTables &symtabs,
                                    uint32_t baseAddress = 0);

VMEScript LIBMVME_CORE_EXPORT parse(const std::string &input, SymbolTables &symtabs,
                                    uint32_t baseAddress = 0);

class LIBMVME_CORE_EXPORT SyntaxHighlighter: public QSyntaxHighlighter
{
    using QSyntaxHighlighter::QSyntaxHighlighter;

    protected:
        virtual void highlightBlock(const QString &text) override;
};

struct LIBMVME_CORE_EXPORT Result
{
    VMEError error;
    uint32_t value;
    QVector<uint32_t> valueVector;
    Command command;
};

typedef QVector<Result> ResultList;
typedef std::function<void (const QString &)> LoggerFun;

namespace run_script_options
{
    using Flag = u8;
    static const Flag LogEachResult = 1u << 0;
    static const Flag AbortOnError  = 1u << 1;
}

LIBMVME_CORE_EXPORT ResultList run_script(
    VMEController *controller,
    const VMEScript &script,
    LoggerFun logger = LoggerFun(),
    const run_script_options::Flag &options = 0);

#if 0
inline ResultList run_script(
    VMEController *controller,
    const VMEScript &script,
    LoggerFun logger = LoggerFun(),
    bool logEachResult=false)
{
    run_script_options::Flag opts = (logEachResult ? run_script_options::LogEachResult : 0u);

    return run_script(controller, script, logger, opts);
}
#endif

inline bool has_errors(const ResultList &results)
{
    return std::any_of(
        std::begin(results), std::end(results),
        [] (const Result &r) { return r.error.isError(); });
}

LIBMVME_CORE_EXPORT Result run_command(VMEController *controller,
                                  const Command &cmd,
                                  LoggerFun logger = LoggerFun());

LIBMVME_CORE_EXPORT QString format_result(const Result &result);

inline bool is_block_read_command(const CommandType &cmdType)
{
    switch (cmdType)
    {
        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
        case CommandType::Blk2eSST64:
        case CommandType::BLTCount:
        case CommandType::BLTFifoCount:
        case CommandType::MBLTCount:
        case CommandType::MBLTFifoCount:
            return true;
        default: break;
    }
    return false;
}

LIBMVME_CORE_EXPORT Command get_first_meta_block(const VMEScript &vmeScript);
LIBMVME_CORE_EXPORT QString get_first_meta_block_tag(const VMEScript &vmeScript);

} // namespace vme_script

#endif /* __VME_SCRIPT_QT_H__ */

