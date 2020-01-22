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

namespace
{

void collect_symbol_tables(const ConfigObject *co, vme_script::SymbolTables &symtabs)
{
    assert(co);

    symtabs.push_back(co->getVariables());

    if (auto parentObject = qobject_cast<ConfigObject *>(co->parent()))
        collect_symbol_tables(parentObject, symtabs);
}

}

vme_script::SymbolTables collect_symbol_tables(const ConfigObject *co)
{
    assert(co);

    vme_script::SymbolTables result;

    collect_symbol_tables(co, result);

    return result;
}

vme_script::SymbolTables collect_symbol_tables(const VMEScriptConfig *scriptConfig)
{
    assert(scriptConfig);

    auto result = collect_symbol_tables(qobject_cast<const ConfigObject *>(scriptConfig));

    result.push_front({"local", {}});

    return result;
}


} // end namespace mvme
} // end namespace mesytec
