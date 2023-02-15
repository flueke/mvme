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
#ifndef __A2_MULTIWORD_DATAFILTER_H__
#define __A2_MULTIWORD_DATAFILTER_H__

#include "a2_data_filter.h"
#include <cassert>
#include <initializer_list>
#include <system_error>

namespace a2
{
namespace data_filter
{

static const int MaxFilters = 16;

struct MultiWordFilter;

u32 add_subfilter(MultiWordFilter *filter, DataFilter subfilter);

struct MultiWordFilter
{
    enum CacheType { CacheA, CacheD, CacheCount };

    std::array<DataFilter, MaxFilters> filters;
    std::array<u32, MaxFilters> results;
    std::array<std::array<CacheEntry, MaxFilters>, CacheCount> caches;
    s16 filterCount = 0;
    u16 completionMask = 0;

    // FIXME: This static_assert() doesn't actually work (g++ 6.3.0)
    static_assert(sizeof(completionMask) * 8 >= MaxFilters, "completionMask type too small");

    MultiWordFilter()
    {}

    MultiWordFilter(std::initializer_list<DataFilter> initFilters)
    {
        for (auto f: initFilters)
        {
            add_subfilter(this, f);
        }
    }
};

inline void clear_completion(MultiWordFilter *filter)
{
    filter->completionMask = 0;
}

inline u32 add_subfilter(MultiWordFilter *filter, DataFilter subfilter)
{
    if (filter->filterCount >= MaxFilters)
        throw std::out_of_range("filter count exceeded");

    filter->filters[filter->filterCount] = subfilter;
    filter->caches[MultiWordFilter::CacheA][filter->filterCount] = make_cache_entry(subfilter, 'A');
    filter->caches[MultiWordFilter::CacheD][filter->filterCount] = make_cache_entry(subfilter, 'D');
    filter->filterCount++;
    clear_completion(filter);

    return filter->filterCount;
}

inline bool is_complete(MultiWordFilter *filter)
{
    return number_of_set_bits(filter->completionMask) == filter->filterCount;
}

inline bool process_data(MultiWordFilter *filter, u32 dataWord, s32 wordIndex = -1)
{
    for (int i = 0; i < filter->filterCount; i++)
    {
        u16 partMask = 1u << i;

        static_assert(sizeof(partMask) >= sizeof(filter->filterCount),
                      "partMask type too small for max filterCount");

        if (!(filter->completionMask & partMask)
            && matches(filter->filters[i], dataWord, wordIndex))
        {
            filter->results[i] = dataWord;
            filter->completionMask |= partMask;
            break;
        }
    }

    return is_complete(filter);
}

inline u64 extract(MultiWordFilter *filter, MultiWordFilter::CacheType cacheType)
{
    u64 result = 0;
    u32 shift = 0;

    for (int i = 0; i < filter->filterCount; i++)
    {
        u32 value = extract(filter->caches[cacheType][i], filter->results[i]);
        result |= static_cast<u64>(value) << shift;
        shift += filter->caches[cacheType][i].extractBits;
    }

    return result;
}

inline u16 get_extract_bits(const MultiWordFilter *filter, MultiWordFilter::CacheType cacheType)
{
    u16 result = 0;

    for (int i = 0; i < filter->filterCount; i++)
    {
        result += filter->caches[cacheType][i].extractBits;
    }

    return result;
}

} // namespace data_filter
} // namespace a2

#endif /* __A2_MULTIWORD_DATAFILTER_H__ */
