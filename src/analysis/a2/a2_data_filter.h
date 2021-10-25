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
#ifndef __A2_DATA_FILTER__
#define __A2_DATA_FILTER__

#include "util/bits.h"

#include <string>
#include <array>

// Speedup if bmi2 is enabled, penalty otherwise.
#ifdef __BMI2__
#define A2_DATA_FILTER_ALWAYS_GATHER
#endif

namespace a2
{
namespace data_filter
{

static const s32 FilterSize = 32;

struct DataFilter
{
    std::array<char, FilterSize> filter;
    u32 matchMask  = 0;
    u32 matchValue = 0;
    s32 matchWordIndex = -1;

    // bool operator==(const DataFilter &o) const = default; // c++20
    inline bool operator==(const DataFilter &o) const
    {
        return (filter == o.filter
                && matchMask == o.matchMask
                && matchValue == o.matchValue
                && matchWordIndex == o.matchWordIndex);
    }
};

struct CacheEntry
{
    u32 extractMask = 0;
    u8 extractBits  = 0;
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
    bool needGather = false;
    u8 extractShift = 0;
#endif
};

DataFilter make_filter(const std::string &filter, s32 wordIndex = -1);

inline bool matches(const DataFilter &filter, u32 value, s32 wordIndex = -1)
{
    return ((filter.matchWordIndex < 0) || (filter.matchWordIndex == wordIndex))
        && ((value & filter.matchMask) == filter.matchValue);
}

CacheEntry make_cache_entry(const DataFilter &filter, char marker);

// Note: a match is assumed.
inline u32 extract(const CacheEntry &cache, u32 value)
{
#ifdef A2_DATA_FILTER_ALWAYS_GATHER
    u32 result = bit_gather(value, cache.extractMask);
#else
    u32 result = ((value & cache.extractMask) >> cache.extractShift);

    if (cache.needGather)
    {
        result = bit_gather(result, cache.extractMask >> cache.extractShift);
    }
#endif
    return result;
}

// Note: a match is assumed.
inline u32 extract(const DataFilter &filter, u32 value, char marker)
{
    auto cache = make_cache_entry(filter, marker);
    return extract(cache, value);
}

inline u8 get_extract_bits(const DataFilter &filter, char marker)
{
    return make_cache_entry(filter, marker).extractBits;
}

inline u32 get_extract_mask(const DataFilter &filter, char marker)
{
    return make_cache_entry(filter, marker).extractMask;
}

inline u8 get_extract_shift(const DataFilter &filter, char marker)
{
    return make_cache_entry(filter, marker).extractShift;
}

std::string to_string(const DataFilter &filter);

} // namespace data_filter
} // namespace a2

#endif /* __A2_DATA_FILTER__ */
