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
