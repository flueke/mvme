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
