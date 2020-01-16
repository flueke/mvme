#include "vme_config_utility.h"

namespace mesytec
{
namespace mvme
{

vme_script::VMEScript LIBMVME_EXPORT parse(
    VMEScriptConfig *scriptConfig,
    u32 baseAddress)
{
    return vme_script::parse(scriptConfig->getScriptContents(), baseAddress);
}

vme_script::VMEScript LIBMVME_EXPORT parse(
    VMEScriptConfig *scriptConfig,
    vme_script::SymbolTables &symtabs,
    u32 baseAddress)
{
    return vme_script::parse(scriptConfig->getScriptContents(), symtabs, baseAddress);
}


} // end namespace mvme
} // end namespace mesytec
