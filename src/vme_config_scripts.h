#ifndef __VME_CONFIG_SCRIPTS_H__
#define __VME_CONFIG_SCRIPTS_H__

#include "vme_config.h"
#include "vme_script.h"
#include "libmvme_export.h"

#include <utility>

namespace mesytec
{
namespace mvme
{

// Bridges between / combines vme_config and vme_script

vme_script::VMEScript LIBMVME_EXPORT parse(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress = 0);

using VMEScriptAndVars = std::pair<vme_script::VMEScript, vme_script::SymbolTables>;

VMEScriptAndVars LIBMVME_EXPORT parse_and_return_symbols(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress = 0);

// Collects the symbol tables from the given ConfigObject and all parent
// ConfigObjects. The first table in the return value is the one belonging to
// the given ConfigObject (the most local one).
vme_script::SymbolTables LIBMVME_EXPORT collect_symbol_tables(const ConfigObject *co);

// Same as collect_symbol_tables for a ConfigObject except that a script-local
// symbol table is prepended to the result.
// TODO: do we need the script-local symboltable at all? Is the 'set' command any good?
//vme_script::SymbolTables LIBMVME_EXPORT collect_symbol_tables(const VMEScriptConfig *scriptConfig);

// SymbolTable and the ConfigObject from which the table was created.
using SymbolTableWithSourceObject = std::pair<vme_script::SymbolTable, const ConfigObject *>;

// Collects the symbol tables from the given ConfigObject and all parent
// ConfigObjects. The first table in the return value is the one belonging to
// the given ConfigObject (the most local one).
QVector<SymbolTableWithSourceObject> LIBMVME_EXPORT collect_symbol_tables_with_source(
    const ConfigObject *co);

vme_script::SymbolTables LIBMVME_EXPORT convert_to_symboltables(
    const QVector<SymbolTableWithSourceObject> &input);

} // end namespace mvme
} // end namespace mesytec


#endif /* __VME_CONFIG_SCRIPTS_H__ */
