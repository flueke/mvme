/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_VME_SCRIPT_VARIABLES_H__
#define __MVME_VME_SCRIPT_VARIABLES_H__

#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

#include "libmvme_core_export.h"

namespace vme_script
{

static const QString SystemVariablePrefix = "sys_";
static const QString MesytecVariablePrefix = "mesy_";

struct LIBMVME_CORE_EXPORT Variable
{
    // The variables value. No special handling is done. Variable expansion
    // means simple text replacement.
    QString value;

    // Optional free form string containing information about where the variable was
    // defined. Could simply be a line number.
    QString definitionLocation;

    // Optional free form comment or description of the variable.
    QString comment;

    Variable()
    {}

    // Constructor taking the variable value and an optional definition
    // location string.
    explicit Variable(const QString &value_, const QString &definitionLocation_ = {},
             const QString &comment_ = {})
        : value(value_)
        , definitionLocation(definitionLocation_)
        , comment(comment_)
    { }

    // This constructor takes a lineNumber, converts it to a string and uses it
    // as the definition location.
    Variable(const QString &v, int lineNumber)
        : Variable(v, QString::number(lineNumber), {})
    { }

    // Variables with a null (default constructed) value are considered invalid.
    // Empty and non-empty values are considered valid.
    explicit operator bool() const { return !value.isNull(); }
};

bool LIBMVME_CORE_EXPORT operator==(const Variable &va, const Variable &vb);
bool LIBMVME_CORE_EXPORT operator!=(const Variable &va, const Variable &vb);

struct LIBMVME_CORE_EXPORT SymbolTable
{
    QString name;
    QMap<QString, Variable> symbols;

    bool contains(const QString &varName) const
    {
        return symbols.contains(varName);
    }

    Variable value(const QString &varName) const
    {
        return symbols.value(varName);
    }

    bool isEmpty() const
    {
        return symbols.isEmpty();
    }

    Variable &operator[](const QString &varName)
    {
        return symbols[varName];
    }

    const Variable operator[](const QString &varName) const
    {
        return symbols[varName];
    }

    int size() const
    {
        return symbols.size();
    }

    QStringList symbolNames() const
    {
        return symbols.keys();
    }

    QSet<QString> symbolNameSet() const
    {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        auto names = symbolNames();
        return QSet<QString>(std::begin(names), std::end(names));
#else
        return QSet<QString>::fromList(symbolNames());
#endif
    }

    bool remove(const QString &varName)
    {
        return symbols.remove(varName);
    }
};

bool LIBMVME_CORE_EXPORT operator==(const SymbolTable &sta, const SymbolTable &stb);
bool LIBMVME_CORE_EXPORT operator!=(const SymbolTable &sta, const SymbolTable &stb);

// Vector of SymbolTables. The first table in the vector is the innermost
// scope and is written to by the 'set' command.
using SymbolTables = QVector<SymbolTable>;

// Lookup a variable in a list of symbol tables.
// Visits symbol tables in order and returns the first Variable stored under
// varName.
LIBMVME_CORE_EXPORT Variable lookup_variable(const QString &varName, const SymbolTables &symtabs);

// JSON Serialization
QJsonObject LIBMVME_CORE_EXPORT to_json(const Variable &var);
Variable LIBMVME_CORE_EXPORT variable_from_json(const QJsonObject &json);

QJsonObject LIBMVME_CORE_EXPORT to_json(const SymbolTable &symtab);
SymbolTable LIBMVME_CORE_EXPORT symboltable_from_json(const QJsonObject &tableJson);

inline bool is_system_variable_name(const QString &varName)
{
    return varName.startsWith(SystemVariablePrefix);
}

inline bool is_mesytec_variable_name(const QString &varName)
{
    return varName.startsWith(MesytecVariablePrefix);
}

inline bool is_internal_variable_name(const QString &varName)
{
    return is_system_variable_name(varName)
        || is_mesytec_variable_name(varName);
}

// Comparator for sorting names of variables.
// Variables names starting with SystemVariablePrefix are grouped before
// non-system variables.
inline bool variable_name_cmp_sys_first(const QString &na, const QString &nb)
{
    if (is_system_variable_name(na) && is_system_variable_name(nb))
        return na < nb;
    else if (is_system_variable_name(na))
        return true;
    else if (is_system_variable_name(nb))
        return false;

    return na < nb;
}

} // end namespace vme_script

#endif /* __MVME_VME_SCRIPT_VARIABLES_H__ */
