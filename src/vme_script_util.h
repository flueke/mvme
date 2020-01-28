#ifndef __MVME_VME_SCRIPT_UTIL_H__
#define __MVME_VME_SCRIPT_UTIL_H__

#include <QMultiMap>
#include "vme_script.h"

namespace vme_script
{

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

} // end namespace vme_script

#endif /* __MVME_VME_SCRIPT_UTIL_H__ */
