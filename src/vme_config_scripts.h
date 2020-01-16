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

vme_script::VMEScript parse(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress = 0);

using VMEScriptAndVars = std::pair<vme_script::VMEScript, vme_script::SymbolTables>;

VMEScriptAndVars LIBMVME_EXPORT parse_return_symbols(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress = 0);

// Note: there's no way to specify additional symbol tables and thus inject
// variables prior to parsing. This will be added if needed.

// Gathering symbol tables
// -----------------------
// - superglobal scripts: symbols from DAQGlobal config (this level does not exist yet)
//
// - VMEConfig scripts: symbols from DAQGlobal config and VMEConfig
// - Event scripts: symbols from DAQGlobal and VMEConfig
//
// - Module scripts: symbols from event, vmeconfig and superglobals
//
// -> 3 levels to implement right now
// get symbols from vmeconfig
// get symbols from eventconfig
// get symbols from moduleconfig
//
// from VMEScriptConfig perspective:
// SymbolTables = { localTable } + parent->getScriptSymbols()
// VMEConfig: return empty table for now
// EventConfig: populate table with:
// - irq: zero if not using irq, otherwise the irq value
// - mcst: the mcst byte in hex without the 0x prefix, e.g. "bb" for mcst 0xbb000000
vme_script::SymbolTables build_symbol_tables(const VMEScriptConfig *scriptConfig);

} // end namespace mvme
} // end namespace mesytec


#endif /* __VME_CONFIG_SCRIPTS_H__ */
