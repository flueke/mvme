#ifndef __VME_CONFIG_UTILITY_H__
#define __VME_CONFIG_UTILITY_H__

#include "vme_config.h"
#include "vme_script.h"
#include "libmvme_export.h"

namespace mesytec
{
namespace mvme
{

vme_script::VMEScript LIBMVME_EXPORT parse(
    VMEScriptConfig *scriptConfig,
    u32 baseAddress = 0);

vme_script::VMEScript LIBMVME_EXPORT parse(
    VMEScriptConfig *scriptConfig,
    vme_script::SymbolTables &symtabs,
    u32 baseAddress = 0);

} // end namespace mvme
} // end namespace mesytec


#endif /* __VME_CONFIG_UTILITY_H__ */
