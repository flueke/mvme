#ifndef __MVME_UTIL_COUNTERS_H__
#define __MVME_UTIL_COUNTERS_H__

#include <algorithm>

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


#endif /* __MVME_UTIL_COUNTERS_H__ */
