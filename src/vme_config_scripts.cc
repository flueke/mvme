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
#include "vme_config_scripts.h"

#include <cassert>

namespace mesytec
{
namespace mvme
{

vme_script::VMEScript parse(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress)
{
    return parse_and_return_symbols(scriptConfig, baseAddress).first;
}

VMEScriptAndVars parse_and_return_symbols(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress)
{
    auto symtabs = collect_symbol_tables(scriptConfig);
    auto script = vme_script::parse(scriptConfig->getScriptContents(), symtabs, baseAddress);

    return std::make_pair(script, symtabs);
}

vme_script::SymbolTables collect_symbol_tables(const ConfigObject *co)
{
    assert(co);

    auto symbolsWithSources = collect_symbol_tables_with_source(co);

    return convert_to_symboltables(symbolsWithSources);
}

#if 0
vme_script::SymbolTables collect_symbol_tables(const VMEScriptConfig *scriptConfig)
{
    assert(scriptConfig);

    auto result = collect_symbol_tables(qobject_cast<const ConfigObject *>(scriptConfig));

    result.push_front({"local", {}});

    return result;
}
#endif

namespace
{

void collect_symbol_tables_with_source(
    const ConfigObject *co,
    QVector<SymbolTableWithSourceObject> &dest)
{
    assert(co);
    dest.push_back({co->getVariables(), co});

    if (auto parentObject = qobject_cast<ConfigObject *>(co->parent()))
        collect_symbol_tables_with_source(parentObject, dest);
}

} // end anon namespace

QVector<SymbolTableWithSourceObject> collect_symbol_tables_with_source(
    const ConfigObject *co)
{
    QVector<SymbolTableWithSourceObject> result;

    collect_symbol_tables_with_source(co, result);

    return result;
}

vme_script::SymbolTables LIBMVME_EXPORT convert_to_symboltables(
    const QVector<SymbolTableWithSourceObject> &input)
{
    vme_script::SymbolTables result;

    std::transform(
        std::begin(input), std::end(input),
        std::back_inserter(result),
        [] (const SymbolTableWithSourceObject &stso) { return stso.first; });

    return result;
}

} // end namespace mvme
} // end namespace mesytec
