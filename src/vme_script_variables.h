#ifndef __MVME_VME_SCRIPT_VARIABLES_H__
#define __MVME_VME_SCRIPT_VARIABLES_H__

#include <QMap>
#include <QString>
#include <QVector>

#include "libmvme_core_export.h"


namespace vme_script
{

struct LIBMVME_CORE_EXPORT Variable
{
    // The variables value. No special handling is done. Variable expansion
    // means simple text replacement.
    QString value;

    // Free form string containing information about where the variable was
    // defined. Could simply be a line number.
    QString definitionLocation;

    Variable()
    {}

    Variable(const QString &v, const QString &definitionLocation_ = {})
        : value(v)
        , definitionLocation(definitionLocation_)
    { }

    Variable(const QString &v, int lineNumber)
        : Variable(v, QString::number(lineNumber))
    { }

    // Variables with a null (default constructed) value are considered invalid.
    // Empty values and non-empty values are considered valid.
    explicit operator bool() const { return !value.isNull(); }
};

using SymbolTable = QMap<QString, Variable>;

// Vector of SymbolTables. The first table in the vector is the innermost
// scope and is written to by the 'set' command.
using SymbolTables = QVector<SymbolTable>;

LIBMVME_CORE_EXPORT Variable lookup_variable(const QString &varName, const SymbolTables &symtabs);

} // end namespace vme_script

#endif /* __MVME_VME_SCRIPT_VARIABLES_H__ */
