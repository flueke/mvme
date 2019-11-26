#ifndef __MVME_UTIL_ALGO_H__
#define __MVME_UTIL_ALGO_H__

#include <cstddef>

template<typename BS>
void copy_bitset(const BS &in, BS &dest)
{
    for (size_t i = 0; i < in.size(); i++)
        dest.set(i, in.test(i));
}

#endif /* __MVME_UTIL_ALGO_H__ */
