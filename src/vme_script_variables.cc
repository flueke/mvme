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
#include "vme_script_variables.h"

namespace vme_script
{

bool operator==(const Variable &va, const Variable &vb)
{
    return (va.value == vb.value
            && va.definitionLocation == vb.definitionLocation
            && va.comment == vb.comment);
}

bool operator!=(const Variable &va, const Variable &vb)
{
    return !(va == vb);
}

bool operator==(const SymbolTable &sta, const SymbolTable &stb)
{
    return (sta.name == stb.name
            && sta.symbols == stb.symbols);
}

bool operator!=(const SymbolTable &sta, const SymbolTable &stb)
{
    return !(sta == stb);
}

Variable lookup_variable(const QString &varName, const SymbolTables &symtabs)
{
    for (const auto &symtab: symtabs)
        if (symtab.contains(varName))
            return symtab.value(varName);

    return {};
}

//
// JSON Serialization
//
QJsonObject to_json(const vme_script::Variable &var)
{
    QJsonObject result;
    result["value"] = var.value;
    result["definitionLocation"] = var.definitionLocation;
    result["comment"] = var.comment;
    return result;
}

vme_script::Variable variable_from_json(const QJsonObject &json)
{
    vme_script::Variable result;

    result.value = json["value"].toString();
    result.definitionLocation = json["definitionLocation"].toString();
    result.comment = json["comment"].toString();

    return result;
}

QJsonObject to_json(const vme_script::SymbolTable &symtab)
{
    QJsonObject varsJson;

    for (const auto &varName: symtab.symbols.keys())
    {
        varsJson[varName] = to_json(symtab.symbols.value(varName));
    }

    QJsonObject tableJson;
    tableJson["name"] = symtab.name;
    tableJson["variables"] = varsJson;

    return tableJson;
}

vme_script::SymbolTable symboltable_from_json(const QJsonObject &tableJson)
{
    vme_script::SymbolTable symtab;

    symtab.name = tableJson["name"].toString();

    auto varsJson = tableJson["variables"].toObject();

    for (auto &varName: varsJson.keys())
    {
        symtab.symbols[varName] = variable_from_json(varsJson[varName].toObject());
    }

    return symtab;
}

} // end namespace vme_script
