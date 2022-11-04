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
#ifndef __VME_SCRIPT_QT_H__
#define __VME_SCRIPT_QT_H__

#include <cstdint>
#include <functional>
#include <memory>
#include <QVector>

#include "libmvme_core_export.h"

#include "typedefs.h"
#include "util/qt_str.h"
#include "vme.h"
#include "vme_script_variables.h"
#include "vme_error.h"

class QFile;
class QTextStream;
class QString;

class VMEController;

namespace vme_script
{

struct PreparsedLine
{
    QString line;               // A copy of the original line
    QStringList parts;          // The line trimmed of whitespace and split at word boundaries.
    int lineNumber;             // The original line number
    QSet<QString> varRefs;      // The names of the variables referenced by this line.
};

static const QString MetaBlockBegin = "meta_block_begin";
static const QString MetaBlockEnd = "meta_block_end";

static const QString MVLC_CustomBegin = "mvlc_custom_begin";
static const QString MVLC_CustomEnd = "mvlc_custom_end";

static const QString MVLC_StackBegin = "mvlc_stack_begin";
static const QString MVLC_StackEnd = "mvlc_stack_end";

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

    // VME reads and writes.
    Read,
    ReadAbs,
    Write,
    WriteAbs,

    // Delay when directly executing a script.
    Wait,

    // Marker word to be inserted into the data stream by the controller.
    Marker,

    // VME block transfers
    BLT,
    BLTFifo,
    MBLT,
    MBLTFifo,
    MBLTSwapped,

    // fast source synchronous block transfers
    Blk2eSST64,
    Blk2eSST64Swapped,

    // Meta commands to temporarily use a different base address for the
    // following commands and then reset back to the default base address.
    SetBase,
    ResetBase,

    // Low-level VMUSB specific register write and read commands.
    VMUSB_WriteRegister,
    VMUSB_ReadRegister,

    // Low-level MVLC instruction to insert a special word into the data
    // stream. Currently 'timestamp' and 'stack_triggers' are implemented. The
    // special word code can also be given as a numeric value.
    // The type of the special word is stored in Command.value.
    MVLC_WriteSpecial,

    // A meta block enclosed in meta_block_begin and meta_block_end.
    MetaBlock,

    // Meta command to set a variable value. The variable is inserted into the
    // first (most local) symbol table given to the parser.
    SetVariable,

    // Prints its arguments to the log output on a separate line. Arguments are
    // separated by a space by default which means string quoting is not
    // strictly required.
    Print,

    MVLC_Custom,
    MVLC_InlineStack,
    MVLC_SetAddressIncMode,
    MVLC_Wait,
    MVLC_SignalAccu,
    MVLC_MaskShiftAccu,
    MVLC_SetAccu,
    MVLC_ReadToAccu,
    MVLC_CompareLoopAccu,
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
    Rate320MB,
};

enum class MVLCSpecialWord: u8
{
    Timestamp     = 0x0,
    Accu          = 0x1,
};

struct Command;

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
    //u8 blockAddressMode = vme_address_modes::A32;
    //uint32_t blockAddress = 0;
    Blk2eSSTRate blk2eSSTRate = Blk2eSSTRate::Rate160MB;

    QString warning;
    s32 lineNumber = 0;

    MetaBlock metaBlock = {};
    QStringList printArgs;
    std::vector<u32> mvlcCustomStack;
    // vme script parsed from a mvlc_stack_begin block
    std::vector<std::shared_ptr<Command>> mvlcInlineStack;
    bool mvlcSlowRead = false;
};

inline bool is_valid(const Command &cmd)
{
    return cmd.type != CommandType::Invalid;
}

LIBMVME_CORE_EXPORT QString to_string(CommandType commandType);
LIBMVME_CORE_EXPORT CommandType commandType_from_string(const QString &str);
LIBMVME_CORE_EXPORT QString amod_to_string(u8 addressMode);
LIBMVME_CORE_EXPORT QString to_string(DataWidth dataWidth);
LIBMVME_CORE_EXPORT QString to_string(const Command &cmd);
LIBMVME_CORE_EXPORT QString format_hex(uint32_t value);

using VMEScript = QVector<Command>;

Command add_base_address(Command cmd, uint32_t baseAddress);

struct ParseError: std::exception
{
    ParseError(const QString &message, int lineNumber = -1)
        : message(message)
        , lineNumber(lineNumber)
    {}

    const char *what() const noexcept override
    {
        return "vme_script::ParseError";
    }

    QString toString() const
    {
        QString ret;

        if (lineNumber >= 0)
            ret = QSL("%1 on line %2").arg(message).arg(lineNumber);
        else
            ret = message;

        if (!scriptName.isEmpty())
            ret = QSL("Script '") + scriptName + QSL("': ") + ret;

        return ret;
    }

    QString message;
    int lineNumber;
    // Name of the input script. To be filled out by callers of the parse()
    // functions if the info is available.
    QString scriptName;
};

QString LIBMVME_CORE_EXPORT expand_variables(const QString &line, const SymbolTables &symtabs, s32 lineNumber);
void LIBMVME_CORE_EXPORT expand_variables(PreparsedLine &preparsed, const SymbolTables &symtabs);

QString LIBMVME_CORE_EXPORT evaluate_expressions(const QString &qline, s32 lineNumber);
void LIBMVME_CORE_EXPORT evaluate_expressions(PreparsedLine &preparsed);

// These versions of the parse function use an internal symbol table. Access to
// variables defined via the 'set' command is not possible.
VMEScript LIBMVME_CORE_EXPORT parse(QFile *input, uint32_t baseAddress = 0);
VMEScript LIBMVME_CORE_EXPORT parse(const QString &input, uint32_t baseAddress = 0);
VMEScript LIBMVME_CORE_EXPORT parse(QTextStream &input, uint32_t baseAddress = 0);
VMEScript LIBMVME_CORE_EXPORT parse(const std::string &input, uint32_t baseAddress = 0);

// Versions of the parse function taking a list of SymbolTables by reference.
// The first table in the list is used as the 'script local' symbol table. If
// the list is empty a single SymbolTable instance will be created and added.
VMEScript LIBMVME_CORE_EXPORT parse(QFile *input, SymbolTables &symtabs,
                                    uint32_t baseAddress = 0);

VMEScript LIBMVME_CORE_EXPORT parse(const QString &input, SymbolTables &symtabs,
                                    uint32_t baseAddress = 0);

VMEScript LIBMVME_CORE_EXPORT parse(QTextStream &input, SymbolTables &symtabs,
                                    uint32_t baseAddress = 0);

VMEScript LIBMVME_CORE_EXPORT parse(const std::string &input, SymbolTables &symtabs,
                                    uint32_t baseAddress = 0);

// Run a pre parse step on the input.
// This splits the input into lines, removing comments and leading and trailing
// whitespace. The line is then further split into atomic parts and the
// variable names referenced whithin the line are collected.
QVector<PreparsedLine> LIBMVME_CORE_EXPORT pre_parse(const QString &input);

QVector<PreparsedLine> LIBMVME_CORE_EXPORT pre_parse(QTextStream &input);

// These functions return the set of variable names references in the given vme
// script text.
QSet<QString> LIBMVME_CORE_EXPORT collect_variable_references(
    const QString &input);

QSet<QString> LIBMVME_CORE_EXPORT collect_variable_references(
    QTextStream &input);

LIBMVME_CORE_EXPORT Command get_first_meta_block(const VMEScript &vmeScript);
LIBMVME_CORE_EXPORT QString get_first_meta_block_tag(const VMEScript &vmeScript);

LIBMVME_CORE_EXPORT u8 parseAddressMode(const QString &str, bool acceptNumericValues = true);

} // namespace vme_script

#endif /* __VME_SCRIPT_QT_H__ */
