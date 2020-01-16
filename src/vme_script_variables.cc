#include "vme_script_variables.h"

namespace vme_script
{

Variable lookup_variable(const QString &varName, const SymbolTables &symtabs)
{
    for (const auto &symtab: symtabs)
        if (symtab.contains(varName))
            return symtab.value(varName);

    return {};
}

} // end namespace vme_script
