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
#ifndef __MVME_VME_SCRIPT_UTIL_H__
#define __MVME_VME_SCRIPT_UTIL_H__

#include <QMultiMap>
#include <QSyntaxHighlighter>
#include "vme_script.h"
#include "libmvme_core_export.h"

namespace vme_script
{

inline bool is_block_read_command(const CommandType &cmdType)
{
    switch (cmdType)
    {
        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
        case CommandType::Blk2eSST64:
            return true;
        default: break;
    }
    return false;
}


using WritesCollection = QMultiMap<u32, u32>;

inline WritesCollection collect_writes(const VMEScript &script)
{
    WritesCollection result;

    for (const auto &cmd: script)
    {
        if (cmd.type == CommandType::Write || cmd.type == CommandType::WriteAbs)
            result.insert(cmd.address, cmd.value);
    }

    return result;
}

inline WritesCollection collect_writes(const QString &scriptText, u32 baseAddress = 0)
{
    return collect_writes(parse(scriptText, baseAddress));
}

inline WritesCollection collect_writes(const QString &scriptText, SymbolTables &symtabs, u32 baseAddress)
{
    return collect_writes(parse(scriptText, symtabs, baseAddress));
}

class LIBMVME_CORE_EXPORT SyntaxHighlighter: public QSyntaxHighlighter
{
    using QSyntaxHighlighter::QSyntaxHighlighter;

    protected:
        virtual void highlightBlock(const QString &text) override;
};

} // end namespace vme_script

#endif /* __MVME_VME_SCRIPT_UTIL_H__ */
