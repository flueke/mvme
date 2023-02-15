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
#ifndef __MVME_A2_UTIL_COUNTERS_H__
#define __MVME_A2_UTIL_COUNTERS_H__

#include <algorithm>
#include <cmath>

namespace a2
{

template<typename T>
T calc_delta0(T cur, T prev)
{
    if (cur < prev)
        return T(0);
    return cur - prev;
}

template<typename T>
T calc_deltas0(const T &cur, const T &prev)
{
    assert(cur.size() == prev.size());

    T result;
    result.reserve(cur.size());

    std::transform(cur.begin(), cur.end(), prev.begin(), std::back_inserter(result),
                   calc_delta0<typename T::value_type>);

    return result;
}

}

#endif /* __MVME_A2_UTIL_COUNTERS_H__ */
