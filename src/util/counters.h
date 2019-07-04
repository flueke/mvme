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

template<typename T>
double calc_rate0(const T &cur, const T&prev, double dt)
{
    double rate = calc_delta0(cur, prev) / dt;

    if (std::isnan(rate))
        rate = 0.0;

    return rate;
}

// Returns a pair of formatted (delta, rate) strings.
template<typename T>
std::pair<QString, QString> format_delta_and_rate(
    T counter, T prevCounter, double dt_s,
    QString unitLabel, UnitScaling unitScaling = UnitScaling::Decimal)
{
    T delta = calc_delta0(counter, prevCounter);
    double rate = calc_rate0(counter, prevCounter, dt_s);

    return std::make_pair(
        format_number(delta, unitLabel, unitScaling),
        format_number(rate, unitLabel + QStringLiteral("/s"), unitScaling));
}

#endif /* __MVME_UTIL_COUNTERS_H__ */
