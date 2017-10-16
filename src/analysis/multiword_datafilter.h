#ifndef __MULTIWORD_DATAFILTER_H__
#define __MULTIWORD_DATAFILTER_H__

#include "../data_filter_c_style.h"

namespace data_filter
{

static const s32 MaxFilters = 16;

struct MultiWordFilter
{
    enum CacheType { CacheA, CacheD, CacheCount };

    std::array<DataFilter, MaxFilters> filters;
    std::array<u32, MaxFilters> results;
    std::array<std::array<CacheEntry, MaxFilters>, CacheCount> caches;
    s32 filterCount = 0;
    u16 completionMask = 0;

    static_assert(sizeof(completionMask) * 8 >= MaxFilters, "completionMask type too small");
};

inline void clear_completion(MultiWordFilter *filter)
{
    filter->completionMask = 0;
}

u32 add_subfilter(MultiWordFilter *filter, DataFilter subfilter)
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
    for (int i = number_of_set_bits(filter->completionMask);
         i < filter->filterCount;
         i++)
    {
        if (matches(filter->filters[i], dataWord, wordIndex))
        {
            filter->results[i] = dataWord;
            filter->completionMask |= 1 << i;
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

};

#endif /* __MULTIWORD_DATAFILTER_H__ */
