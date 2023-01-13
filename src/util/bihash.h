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
#ifndef __MVME_UTIL_BIHASH_H__
#define __MVME_UTIL_BIHASH_H__

#include <QHash>
#include "typedefs.h"

template<typename T1, typename T2>
struct BiHash
{
    using hash_type = QHash<T1, T2>;
    using reverse_hash_type = QHash<T2, T1>;
    using first_value_type = T1;
    using second_value_type = T2;

    hash_type hash;
    reverse_hash_type reverse_hash;

    inline void insert(const T1 &t1,  const T2 &t2)
    {
        hash.insert(t1, t2);
        reverse_hash.insert(t2, t1);
    }

    inline void insertMulti(const T1 &t1,  const T2 &t2)
    {
        hash.insertMulti(t1, t2);
        reverse_hash.insertMulti(t2, t1);
    }

    inline T2 value(const T1 &t1, const T2 &t2 = T2()) const
    {
        return hash.value(t1, t2);
    }

    inline T1 value(const T2 &t2, const T1 &t1 = T1()) const
    {
        return reverse_hash.value(t2, t1);
    }

    inline void clear()
    {
        hash.clear();
        reverse_hash.clear();
    }

    inline size_t size()
    {
        assert(hash.size() == reverse_hash.size());
        assert(hash.size() >= 0);

        return static_cast<size_t>(hash.size());
    }

    inline bool contains(const T1 &t1)
    {
        return hash.contains(t1);
    }

    inline bool contains(const T2 &t2)
    {
        return reverse_hash.contains(t2);
    }
};

#endif /* __MVME_UTIL_BIHASH_H__ */
