/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "mvme_exprtk.h"

namespace mvme_exprtk
{

bool SymbolTable::add_scalar(const std::string &name, double &value)
{
    if (entries.count(name))
        return false;

    Entry entry;
    entry.type   = Entry::Scalar;
    entry.scalar = &value;

    entries[name] = entry;
    return true;
}

bool SymbolTable::add_string(const std::string &name, std::string &str)
{
    if (entries.count(name))
        return false;

    Entry entry;
    entry.type   = Entry::String;
    entry.string = &str;

    entries[name] = Entry{ Entry::String, nullptr, &str, nullptr };
    return true;
}

bool SymbolTable::add_vector(const std::string &name, std::vector<double> &vec)
{
    if (entries.count(name))
        return false;

    Entry entry;
    entry.type   = Entry::Scalar;
    entry.scalar = &value;

    entries[name] = Entry{ Entry::Vector, nullptr, nullptr, &vec };
    return true;
}

} // namespace mvme_exprtk
