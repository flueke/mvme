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

#include "libmvme_export.h"
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
};

enum class AddressMode
{
    A16 = 1,
    A24,
    A32
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

struct Command
{
    CommandType type = CommandType::Invalid;
    AddressMode addressMode = AddressMode::A32;
    u8 addressModeRaw = 0x0D; // a32 priv // XXX: refactor mvme to also carry the raw mode
                              // and later on get rid of the AddressMode num and all the conversions
                              // done in different layers.
    DataWidth dataWidth = DataWidth::D16;
    uint32_t address = 0;
    uint32_t value = 0;
    uint32_t transfers = 0;
    uint32_t delay_ms = 0;
    uint32_t countMask = 0;
    AddressMode blockAddressMode = AddressMode::A32;
    uint32_t blockAddress = 0;
    Blk2eSSTRate blk2eSSTRate = Blk2eSSTRate::Rate160MB;

    QString warning;
    s32 lineNumber = 0;
};

QString to_string(CommandType commandType);
CommandType commandType_from_string(const QString &str);
QString to_string(AddressMode addressMode);
QString to_string(DataWidth dataWidth);
QString to_string(const Command &cmd);
QString format_hex(uint32_t value);

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

VMEScript LIBMVME_EXPORT parse(QFile *input, uint32_t baseAddress = 0);
VMEScript LIBMVME_EXPORT parse(const QString &input, uint32_t baseAddress = 0);
VMEScript LIBMVME_EXPORT parse(QTextStream &input, uint32_t baseAddress = 0);
VMEScript LIBMVME_EXPORT parse(const std::string &input, uint32_t baseAddress = 0);

class LIBMVME_EXPORT SyntaxHighlighter: public QSyntaxHighlighter
{
    using QSyntaxHighlighter::QSyntaxHighlighter;

    protected:
        virtual void highlightBlock(const QString &text) override;
};

uint8_t LIBMVME_EXPORT amod_from_AddressMode(AddressMode mode, bool blt=false, bool mblt=false);

struct LIBMVME_EXPORT Result
{
    VMEError error;
    uint32_t value;
    QVector<uint32_t> valueVector;
    Command command;
};

typedef QVector<Result> ResultList;
typedef std::function<void (const QString &)> LoggerFun;

LIBMVME_EXPORT ResultList run_script(VMEController *controller,
                                     const VMEScript &script,
                                     LoggerFun logger = LoggerFun(), bool logEachResult=false);

LIBMVME_EXPORT Result run_command(VMEController *controller,
                                  const Command &cmd,
                                  LoggerFun logger = LoggerFun());

QString format_result(const Result &result);

} // namespace vme_script

#endif /* __VME_SCRIPT_QT_H__ */

