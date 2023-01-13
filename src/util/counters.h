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
#ifndef __MVME_UTIL_COUNTERS_H__
#define __MVME_UTIL_COUNTERS_H__

#include <algorithm>
#include <cmath>
#include "util/strings.h"

template<typename T>
T calc_delta0(T cur, T prev)
{
    if (cur < prev)
        return T(0);
    return cur - prev;
}

template<typename Container>
Container calc_deltas0(const Container &cur, const Container &prev)
{
    assert(cur.size() == prev.size());

    Container result;
    result.reserve(cur.size());

    std::transform(cur.begin(), cur.end(), prev.begin(), std::back_inserter(result),
                   calc_delta0<typename Container::value_type>);

    return result;
}

template<typename T>
double calc_rate0(const T &cur, const T&prev, double dt)
{
    double rate = calc_delta0(cur, prev) / dt;

    if (std::isnan(rate))
        rate = 0.0;

    return rate;
}

// From https://stackoverflow.com/a/32119279
#define TYPE_AND_VAL(foo) decltype(foo), foo

// Templated version of calc_rate0() to be used like
// calc_rate0<TYPE_AND_VAL(&MyCounters::fooCounter)>(curCounters, prevCounters, dt);
template<typename Member, Member member, typename Base>
double calc_rate0(const Base &cur, const Base &prev, double dt)
{
    double ret = calc_rate0(cur.*member, prev.*member, dt);
    return ret;
};

// Calculate rates from two sequences of current and previous values. The
// results are stored and returned in a container of type ResultType.
// Usage example:
// auto rates = calc_rates0<std::vector<double>>(
//      counters.cbegin(), counters.cend(), prevCounters.cbegin(), dt)
template<typename ResultType, typename InputIterator>
ResultType calc_rates0(InputIterator curBegin, InputIterator curEnd,
                       InputIterator prevBegin,
                       double dt)
{
    ResultType result;

    using ValueType = typename std::iterator_traits<InputIterator>::value_type;

    auto calc_func = [dt] (const ValueType &curVal, const ValueType &prevVal)
    {
        return calc_rate0(curVal, prevVal, dt);
    };

    std::transform(curBegin, curEnd, prevBegin, std::back_inserter(result), calc_func);

    return result;
}

#endif /* __MVME_UTIL_COUNTERS_H__ */
