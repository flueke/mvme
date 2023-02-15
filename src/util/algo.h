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
#ifndef __MVME_UTIL_ALGO_H__
#define __MVME_UTIL_ALGO_H__

#include <cstddef>
#include <iterator>
#include <map>
#include <vector>

template<typename BS>
void copy_bitset(const BS &in, BS &dest)
{
    for (size_t i = 0; i < in.size(); i++)
        dest.set(i, in.test(i));
}

template<typename M, typename K>
bool map_contains(const M &theMap, const K &theKey)
{
    return theMap.find(theKey) != std::end(theMap);
}

template<typename K, typename V>
std::vector<K> map_keys(const std::map<K, V> &theMap)
{
    std::vector<K> result;

    for (const auto &kv: theMap)
        result.push_back(kv.first);

    return result;
}

#endif /* __MVME_UTIL_ALGO_H__ */
