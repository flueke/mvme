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

// Note: there's no way to specify additional symbol tables and thus inject
// variables prior to parsing. This will be added if needed.

// Collects the symbol tables from the given ConfigObject and all parent
// ConfigObjects. The first table in the return value is the one belonging to
// the given ConfigObject (the most local one).
vme_script::SymbolTables collect_symbol_tables(const ConfigObject *co);

// Same as collect_symbol_tables for a ConfigObject except that a script-local
// symbol table is prepended to the result.
vme_script::SymbolTables collect_symbol_tables(const VMEScriptConfig *scriptConfig);

} // end namespace mvme
} // end namespace mesytec


#endif /* __VME_CONFIG_SCRIPTS_H__ */
