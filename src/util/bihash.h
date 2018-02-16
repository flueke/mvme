#ifndef __MVME_UTIL_BIHASH_H__
#define __MVME_UTIL_BIHASH_H__

#include <QHash>

template<typename T1, typename T2>
struct BiHash
{
    using hash_type = QHash<T1, T2>;
    using reverse_hash_type = QHash<T2, T1>;

    hash_type map;
    reverse_hash_type reverse_map;

    inline void insert(const T1 &t1,  const T2 &t2)
    {
        map.insert(t1, t2);
        reverse_map.insert(t2, t1);
    }

    inline T2 value(const T1 &t1, const T2 &t2 = T2()) const
    {
        return map.value(t1, t2);
    }

    inline T1 value(const T2 &t2, const T1 &t1 = T1()) const
    {
        return reverse_map.value(t2, t1);
    }

    inline void clear()
    {
        map.clear();
        reverse_map.clear();
    }
};

#endif /* __MVME_UTIL_BIHASH_H__ */
